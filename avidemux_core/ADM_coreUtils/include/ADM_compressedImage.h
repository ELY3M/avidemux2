//
// C++ Interface: ADM_compressedImage.h
//
// Description:
//
//  Describe a compressed packet coming from the demuxer
//
//
#pragma once
#include "ADM_default.h"

#define ADM_COMPRESSED_NO_PTS ADM_NO_PTS
#define ADM_COMPRESSED_MAX_DATA_LENGTH 0x2000000
class ADMCompressedImage
{

  public:
    /* Our datas */
    uint8_t *data;
    uint32_t dataLength;
    /* Associated flags, in most cases filled by decoder */
    uint32_t flags;
    /* Some interesting informations */
    uint32_t demuxerFrameNo; /* In bitstream order i.e. decoding order */
    uint64_t demuxerPts;     /* In us !*/
    uint64_t demuxerDts;     /* In us */
    /*         */
    void cleanup(uint32_t demuxerNo)
    {
        flags = 0;
        demuxerFrameNo = demuxerNo;
        demuxerPts = ADM_COMPRESSED_NO_PTS;
        demuxerDts = ADM_COMPRESSED_NO_PTS;
    }
};
