/**
    \fn ComputeTimeStamp
    \brief Compute and fill the missing PTS/DTS

*/

#include "ADM_default.h"
#include "fourcc.h"
#include "DIA_coreToolkit.h"
#include "ADM_coreUtils.h"
#include "ADM_indexFile.h"
#include "ADM_ts.h"

#include <math.h>

#define DMX_RECOMPUTE_PTS_DTS_MAX 20

#if 1
#define aprintf(...) {}
#else
#define aprintf printf
#endif
/**
        \fn updatePtsDts
        \brief Update the PTS/DTS

TODO / FIXME : Handle PTS reordering 
*/
static uint64_t wrapIt(uint64_t val, uint64_t start, uint64_t &last, uint32_t &laps)
{
    if(val==ADM_NO_PTS) return val;

#define WRAPLEN (1LL<<32)
#define HALFWRAPLEN (1LL<<31)
    if(val<start)
        val += WRAPLEN;
    val-=start;
    if(last>val && last-val >= HALFWRAPLEN)
        laps++;
    if(laps && val>last && val-last > HALFWRAPLEN)
        laps--;
    last=val;
    val += laps * WRAPLEN;
    return val;
}
bool tsHeader::updatePtsDts(void)
{
        uint32_t i;
        uint64_t lastDts=0,lastPts=0,dtsIncrement=0;

#if 0
        // For audio, The first packet in the seekpoints happens a bit after the action
        // It means some audio may have been seen since we locked on video.
        // So we compute the DTS of the first real packet
        // by using the size field and byterate and arbitrarily make it begins
        // at video
        for(i=0;i<listOfAudioTracks.size();i++)
        {
            vector          <ADM_mpgAudioSeekPoint > *seekPoints=&(listOfAudioTracks[i]->access->seekPoints);
            if(!((*seekPoints).size())) continue;
            uint64_t secondDts=(*seekPoints)[0].dts;
            uint64_t secondSize=(*seekPoints)[0].size;
            if(secondSize && listOfAudioTracks[i]->header.byterate)
            {
                    // it is actually an offset in time to the 2nd packet Dts
                double  firstFloatDts=secondSize*1000*1000.; 
                        firstFloatDts/=listOfAudioTracks[i]->header.byterate; // in us
                uint64_t firstDts=(uint64_t)firstFloatDts;

                        if(firstDts>secondDts) firstDts=0;
                            else firstDts=secondDts-firstDts;

                // Now add our seek point
                ADM_mpgAudioSeekPoint sk;
                sk.dts=firstDts;
                sk.size=0;
                sk.position=ListOfFrames[0]->startAt;
                (*seekPoints).insert( (*seekPoints).begin(),sk);

            }
        }
#endif

        // Make sure everyone starts at 0
        // Search first timestamp (audio/video)
        if(fieldEncoded) // Set fps to field rate, necessary for copy mode
        {
            if(_videostream.dwRate<=45000)
                _videostream.dwRate*=2;
            else if(!(_videostream.dwScale%2))
                _videostream.dwScale/=2;
            _mainaviheader.dwMicroSecPerFrame/=2;
            printf("[processVideoIndex] Doubling fps for field-encoded video, new time base: %d / %d\n",_videostream.dwScale,_videostream.dwRate);
        }
        if(_mainaviheader.dwMicroSecPerFrame)
        {
            dtsIncrement=_mainaviheader.dwMicroSecPerFrame;
        }else
        {
            if(_videostream.dwScale==1000)
                dtsIncrement=ADM_UsecFromFps1000(_videostream.dwRate);
            else if(_videostream.dwScale && _videostream.dwRate)
            {
                double f=_videostream.dwScale;
                f*=1000.*1000.;
                f/=_videostream.dwRate;
                f+=0.49;
                dtsIncrement=(uint64_t)f;
            }
        }
        uint64_t startDts=ListOfFrames[0]->dts;
        uint64_t startPts=ListOfFrames[0]->pts;
        // Special case when we dont have DTS but only PTS
        if(startDts==ADM_NO_PTS) // Do we have DTS ?
        {
            if(startPts!=ADM_NO_PTS)
            {
#define MAX_REORDER_DEPTH 32
                uint64_t minPts = startPts;
                for (i = 0; i < MAX_REORDER_DEPTH; i++)
                {
                    if (i >= ListOfFrames.size())
                        break;
                    dmxFrame *f = ListOfFrames[i];
                    if (f->pts == ADM_NO_PTS)
                        continue;
                    if (f->pts < minPts)
                    {
                        if(minPts - f->pts > HALFWRAPLEN)
                            break; // catch timestamps wrapping around
                        minPts = f->pts;
                    }
                }
                startDts = minPts;
            }
        }
        for(i=0;i<listOfAudioTracks.size();i++)
        {
            if(!listOfAudioTracks[i]->access->seekPoints.size()) continue;
            uint64_t a=listOfAudioTracks[i]->access->seekPoints[0].dts;
            if(a<startDts) startDts=a;
        }
        if (startDts == ADM_NO_PTS)
        {
            ADM_warning("No DTS for the first frame of video, setting to zero.\n");
            startDts = 0;
        }
        if (ListOfFrames[0]->dts == ADM_NO_PTS)
        {
            ListOfFrames[0]->dts = startDts;
        }
        // Rescale all so that it starts ~ 0
        // Video..
        uint32_t ptsWrapCount=0;
        uint32_t dtsWrapCount=0;
        for(i=0;i<ListOfFrames.size();i++)
        {
            dmxFrame *f=ListOfFrames[i];
            f->pts=wrapIt(f->pts,startDts,lastPts,ptsWrapCount);
            f->dts=wrapIt(f->dts,startDts,lastDts,dtsWrapCount);
        }
        lastPts=lastDts=0;
        // Audio start at 0 too
        for(i=0;i<listOfAudioTracks.size();i++)
        {
            ADM_tsTrackDescriptor *track=listOfAudioTracks[i];
            ADM_tsAccess    *access=track->access;
            access->setTimeOffset(startDts);
        }

        // Now fill in the missing timestamp and convert to us
        // for video
        // We are sure to have both PTS & DTS for 1st image
        // Guess missing DTS/PTS for video
        /*int noUpdate=0;*/
        startDts=ListOfFrames[0]->dts;
        ListOfFrames[0]->dts=0;
        for(i=0;i<ListOfFrames.size();i++)
        {
            dmxFrame *frame=ListOfFrames[i];
            aprintf("[psUpdate] frame:%d raw DTS: %" PRId64" PTS:%" PRId64"\n",i,frame->dts,frame->pts);
#if 0
            if(frame->pts==ADM_NO_PTS || frame->dts==ADM_NO_PTS)
            {
                noUpdate++;
                // We have PTS or DTS and it has been a long time we did not have both
                if(noUpdate>DMX_RECOMPUTE_PTS_DTS_MAX && frame->type==1 && (frame->pts!=ADM_NO_PTS || frame->dts!=ADM_NO_PTS))
                {
                    if(frame->pts!=ADM_NO_PTS)
                    {
                        frame->dts=frame->pts-2*dtsIncrement;
                    }else   
                    {
                        frame->pts=frame->dts+2*dtsIncrement;
                    }
                    uint64_t oldDts=lastDts;
                    frame->dts=lastDts=timeConvert(frame->dts);
                    frame->pts=lastPts=timeConvert(frame->pts);
                    if(oldDts>lastDts) printf("[tsRead] Warning DTS going backward frame %d, old:%" PRId64" new:%" PRId64" delta=%" PRId64"\n",
                                                       i,oldDts/1000,lastDts/1000,(oldDts-lastDts)/1000);

                }else
                {
                    lastDts+=dtsIncrement;
                    lastPts+=dtsIncrement;
                    frame->dts=lastDts;
                    frame->pts=ADM_NO_PTS;
                }
            }else    // We got both, use them  
#endif
            {
                if(i) // We may not modify the dts of the first frame yet.
                    frame->dts=lastDts=timeConvert(frame->dts);
                frame->pts=lastPts=timeConvert(frame->pts);
            }
        }
        // Now take care of the first frame dts.
        ListOfFrames[0]->dts=timeConvert(startDts);
        // convert to us for Audio tracks (seek points)
        for(i=0;i<listOfAudioTracks.size();i++)
        {
            ADM_tsTrackDescriptor *track=listOfAudioTracks[i];
            ADM_tsAccess    *access=track->access;
            uint32_t j;
            for(j=0;j<access->seekPoints.size();j++)
            {
                if( access->seekPoints[j].dts!=ADM_NO_PTS) 
                    access->seekPoints[j].dts=access->timeConvert( access->seekPoints[j].dts);
            }
        }
#if 0
        for(int i=0;i<ListOfFrames.size();i++)
        {
            dmxFrame *frame=ListOfFrames[i];
            int64_t pts,dts;
            pts=frame->pts;
            dts=frame->dts;
            if(pts!=ADM_NO_PTS) pts/=1000;
            if(dts!=ADM_NO_PTS) dts/=1000;
            printf("[psVideo] Framex %d PTS:%" PRId64" ms DTS:%" PRId64" ms, delta %" PRId64" ms\n",i,pts,dts,pts-dts);
        }
#endif
        return 1;
                    
}
