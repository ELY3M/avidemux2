/***************************************************************************
                          \file gtk_gui.cpp
                          \brief Main UI even loop

    copyright            : (C) 2001-2009 by mean, fixounet@free.fr
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "ADM_cpp.h"
#include "ADM_default.h"
#include <errno.h>
#include <math.h>
#include <sstream>

#include "fourcc.h"

#include "DIA_coreToolkit.h"
#include "DIA_fileSel.h"

#include "gtkgui.h"
#include "gui_action.hxx"

#include "GUI_render.h"
#include "GUI_ui.h"
#include "prefs.h"

#include "DIA_factory.h"
#include "DIA_working.h"

#include "ADM_coreVideoEncoder.h"
#include "ADM_muxerProto.h"
#include "ADM_preview.h"
#include "ADM_vidMisc.h"
#include "ADM_videoEncoderApi.h"

#include "ADM_audioFilter/include/ADM_audioFilterInterface.h"
#include "ADM_coreVideoFilterFunc.h"

#include "ADM_edScriptGenerator.h"
#include "ADM_script2/include/ADM_script.h"
#include "avi_vars.h"
#include "prototype.h" // FIXME

#include "ADM_edAudioTrackExternal.h"
#include "ADM_muxerProto.h"
#include "ADM_threads.h"

#include "ADM_filterChain.h"

static admMutex singleThread("actionHandlerMutex");
static int cutsNotOnIntraWarned;

#include "DIA_audioTracks.h"
//***********************************
//******** A Function ***************
//***********************************
#include "A_functions.h"
//***********************************
//******** GUI Function**************
//***********************************
extern void call_scriptEngine(const char *scriptFile);
extern int GUI_handleVFilter(void);
extern int GUI_handleVPartialFilter(void);
// Debug functions
void GUI_showCurrentFrameHex(void);
void GUI_showSize(void);

void GUI_avsProxy(void);
uint8_t GUI_close(void);
extern bool GUI_GoToTime(uint64_t time);
extern bool GUI_infiniteForward(uint64_t pts);
//***********************************
//******** DIA Function**************
//***********************************
extern uint8_t DIA_about(void);
extern void DIA_properties(void);
extern uint8_t DIA_Preferences(void);
extern uint8_t DIA_builtin(void);
extern uint8_t DIA_pluginsInfo(void);
extern void DIA_ScriptShortcutConfig(void);

static void ReSync(void);
static void A_RunScript(const char *a);
void cleanUp(void);
void updateLoaded(bool resetMarker = true);

extern void GUI_OpenApplicationLog();
extern void GUI_OpenApplicationDataFolder();

// extern bool ADM_mux_configure(int index);
void brokenAct(void);
//
//  Sub gui files...
//
void HandleAction(Action action);
void HandleAction_Navigate(Action action);
void HandleAction_Save(Action action);
void HandleAction_Staged(Action action);

static std::string getDefaultSettingsFilePath(void);
static std::string getLastSessionFilePath(void);
static std::string getCrashRecoveryFilePath(void);

static bool parseScript(IScriptEngine *engine, const char *name, IScriptEngine::RunMode mode);

// Hacky functions because we currently don't have versatile
// file dialogs
static IScriptEngine *tempEngine;

static void RunScript(const char *name)
{
    parseScript(tempEngine, name, IScriptEngine::DebugOnError);
    A_Resync();
}

static void DebugScript(const char *name)
{
    parseScript(tempEngine, name, IScriptEngine::Debug);
}

static void SaveScript(const char *name)
{
    A_saveScript(tempEngine, name);
}
//
//
/**
    \fn HandleAction
    \brief  serialization of user event through gui

*/
typedef const char *(*getName)(uint32_t nb);
bool getScriptName(int action, int base, getName name, const char *ext, string &out)
{
    if (action < base)
        return false;
    action = action - base;
    const char *p = name(action);
    if (!p)
        return false;
    out = string(p) + string(".") + string(ext);
    return true;
}
void HandleAction(Action action)
{
    admScopedMutex autolock(&singleThread); // make sure only one thread at a time calls this

    ADM_warning("************ %s **************\n", getActionName(action));

    // handle out of band actions
    // independant load not loaded
    //------------------------------------------------
    if (action == ACT_RUN_SCRIPT)
    {
        GUI_FileSelRead(QT_TRANSLATE_NOOP("adm", "Select script/project to run"), A_RunScript);
        return;
    }
    if (action == ACT_SaveAsDefault)
    {
        A_saveDefaultSettings();
        return;
    }
    if (action == ACT_LoadDefault)
    {
        A_loadDefaultSettings();
        return;
    }
    if (action >= ACT_SCRIPT_ENGINE_FIRST && action < ACT_SCRIPT_ENGINE_LAST)
    {
        int engineIndex = (action - ACT_SCRIPT_ENGINE_FIRST) / 3;
        int actionId = (action - ACT_SCRIPT_ENGINE_FIRST) % 3;

        tempEngine = getScriptEngines()[engineIndex];
        std::string ext = tempEngine->defaultFileExtension();

        switch (actionId)
        {
        case 0:
            GUI_FileSelReadExtension(QT_TRANSLATE_NOOP("adm", "Select script to run"), ext.c_str(), RunScript);
            break;

        case 1:
            GUI_FileSelReadExtension(QT_TRANSLATE_NOOP("adm", "Select script to debug"), ext.c_str(), DebugScript);
            break;

        case 2:
            GUI_FileSelWriteExtension(QT_TRANSLATE_NOOP("adm", "Select script to save"), ext.c_str(), SaveScript);
            UI_refreshCustomMenu();
            break;
        }

        return;
    }

    if (action >= ACT_SCRIPT_ENGINE_SHELL_FIRST && action < ACT_SCRIPT_ENGINE_SHELL_LAST)
    {
        IScriptEngine *shellEngine = getScriptEngines()[action - ACT_SCRIPT_ENGINE_SHELL_FIRST];

        if ((shellEngine->capabilities() & IScriptEngine::DebuggerShell) == IScriptEngine::DebuggerShell)
        {
            shellEngine->openDebuggerShell();
        }
        else
        {
            interactiveScript(shellEngine);
        }

        return;
    }

    switch (action)
    {
    case ACT_TimeShift:
        A_TimeShift();
        return;
    case ACT_Goto:
        brokenAct();
        return;
    case ACT_AVS_PROXY:
        GUI_avsProxy();
        return;
    case ACT_BUILT_IN:
        DIA_builtin();
        return;
    case ACT_RECENT0:
    case ACT_RECENT1:
    case ACT_RECENT2:
    case ACT_RECENT3: {
        std::vector<std::string> name;
        int rank;

        name = prefs->get_lastfiles();
        rank = (int)action - ACT_RECENT0;
        ADM_assert(name[rank].size());
        A_openVideo(name[rank].c_str());
        return;
    }
    case ACT_RECENT_PROJECT0:
    case ACT_RECENT_PROJECT1:
    case ACT_RECENT_PROJECT2:
    case ACT_RECENT_PROJECT3: {
        std::vector<std::string> name = prefs->get_lastprojectfiles();
        int rank = (int)action - ACT_RECENT_PROJECT0;

        ADM_assert(name[rank].size());
        call_scriptEngine(name[rank].c_str());
        A_Resync();
        return;
    }
    case ACT_CLEAR_RECENT: {
        if (GUI_Question(QT_TRANSLATE_NOOP(
                "adm", "You are about to clear the list of recent files and projects. This can't be undone. Proceed?")))
        {
            prefs->clear_lastfiles();
            prefs->clear_lastprojects();
            prefs->save();
            UI_updateRecentProjectMenu();
            UI_updateRecentMenu(); // the order matters here
            if (ADM_fileExist(getLastSessionFilePath().c_str()))
            {
                if (!ADM_eraseFile(getLastSessionFilePath().c_str()))
                    ADM_warning("Could not delete last editing state %s\n", getLastSessionFilePath().c_str());
            }
        }
        return;
    }
    case ACT_VIDEO_CODEC_CONFIGURE:
        videoEncoder6Configure();
        return;
    case ACT_ContainerConfigure: {
        if (!ADM_mx_getNbMuxers())
            return;
        int index = UI_GetCurrentFormat();
        ADM_mux_configure(index);
        return;
    }
    case ACT_ScriptShortcutConfig: {
        DIA_ScriptShortcutConfig();
        UI_refreshCustomMenu();
        return;
    }
    case ACT_VIDEO_CODEC_CHANGED: {
        int nw = UI_getCurrentVCodec();
        videoEncoder6_SetCurrentEncoder(nw);
        return;
    }
    case ACT_AUDIO_CODEC_CHANGED: {
        int nw = UI_getCurrentACodec();
        audioCodecSetByIndex(0, nw);
        return;
    }
    case ACT_PLUGIN_INFO:
        DIA_pluginsInfo();
        return;
    case ACT_OPEN_APP_LOG:
        GUI_OpenApplicationLog();
        return;
    case ACT_OPEN_APP_FOLDER:
        GUI_OpenApplicationDataFolder();
        return;

    case ACT_ABOUT:
        DIA_about();
        return;
    case ACT_AUDIO_CODEC_CONFIGURE:
        audioCodecConfigure(0);
        return;
    case ACT_AUDIO_FILTERS: {
        EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
        if (ed)
        {
            double tempoHint = 1.0;
            if (UI_getCurrentVCodec())
            {
                ADM_videoFilterChain *videoChain = createVideoFilterChain(0, ADM_NO_PTS);
                ADM_coreVideoFilter *videoFilter = (*videoChain)[0];
                FilterInfo *info = videoFilter->getInfo();
                tempoHint = info->totalDuration;
                int nb = videoChain->size();
                videoFilter = (*videoChain)[nb - 1];
                info = videoFilter->getInfo();
                if (info->totalDuration)
                {
                    tempoHint /= info->totalDuration;
                } else
                {
                    tempoHint = 1.0;
                }
                destroyVideoFilterChain(videoChain);
                // printf("[audioFilterConfigure] tempo hint: %f\n",tempoHint);
                if (tempoHint < AUDIO_FILTER_STRETCH_MIN || tempoHint > AUDIO_FILTER_STRETCH_MAX)
                {
                    ADM_warning("Tempo hint %f out of range, using default ratio of 1.\n", tempoHint);
                    tempoHint = 1.0;
                }
            }
            ed->audioEncodingConfig.audioFilterConfigure(tempoHint);
            UI_setTimeShift(ed->audioEncodingConfig.shiftEnabled, ed->audioEncodingConfig.shiftInMs);
        }
    }
        return;
    case ACT_PREFERENCES:
        if (playing)
            return;
        if (DIA_Preferences())
        {
            ADM_info("Saving prefs\n");
            prefs->save();
            UI_applySettings();
        }
        return;
    case ACT_SavePref:
        prefs->save();
        return;
    case ACT_EXIT:
        if (playing)
        {
            ADM_info("Stopping playback...\n");
            GUI_PlayAvi(true);
        }
        ADM_info("Closing ui\n");
        UI_closeGui();
        if (video_body && avifileinfo)
            A_saveSession();
        if (video_body && video_body->canUndo())
            video_body->clearUndoQueue();
        return;
    case ACT_RESTORE_SESSION:
        if (playing)
            break;
        A_checkSavedSession(true);
        return;
    default:
        break;
    }

    if (playing) // only allow some action
    {
        switch (action)
        {
        case ACT_PlayAvi:
        case ACT_StopAvi:
            break;
        default:
            return;
        }
    }
    // not playing,
    // restict disabled uncoded actions
    if ((int)action >= ACT_DUMMY)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Not coded in this version"), NULL);
        return;
    }
    // allow only if avi loaded
    if (!avifileinfo)
    {
        switch (action)
        {
        case ACT_JOG:
            break;
        case ACT_OPEN_VIDEO:
            GUI_FileSelRead(QT_TRANSLATE_NOOP("adm", "Select Video File..."), (SELFILE_CB *)A_openVideo);
            break;
        default:
            break;
        }
        return;
    }

    // Dispatch actions, we have a file loaded
    if (action > ACT_NAVIGATE_BEGIN && action < ACT_NAVIGATE_END)
    {
        return HandleAction_Navigate(action);
    }
    if (action > ACT_SAVE_BEGIN && action < ACT_SAVE_END)
    {
        return HandleAction_Save(action);
    }
    if (action > ACT_STAGED_BEGIN && action < ACT_STAGED_END)
    {
        return HandleAction_Staged(action);
    }

    switch (action)
    {
    case ACT_SAVE_PY_SCRIPT: {
        IScriptEngine *engine = getPythonScriptEngine();
        if (!engine)
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "No engine"),
                          QT_TRANSLATE_NOOP("adm", "tinyPy script is not enabled in this build"));
            break;
        }
        char fileName[1024];
        if (FileSel_SelectWrite("Saving tinypy project", fileName, 1000, NULL))
        {
            int l = strlen(fileName);
            if (l > 3)
            {
                char *tail = fileName + l - 3;
                if (tail[0] != '.' || tail[1] != 'p' || tail[2] != 'y')
                    strcat(fileName, ".py");
            }
            A_saveScript(engine, fileName);
        }
        break;
    }
    break;
    case ACT_JOG:
        A_jog();
        break;

    case ACT_CLOSE:
        GUI_close();
        break;

    case ACT_ZOOM_1_4:
    case ACT_ZOOM_1_2:
    case ACT_ZOOM_1_1:
    case ACT_ZOOM_2_1:
        // case ACT_ZOOM_4_1:
        {
            float currentZoom = (float)(2 << (action - ACT_ZOOM_1_4)) / 8;
            UI_setBlockZoomChangesFlag(true);
            admPreview::changePreviewZoom(currentZoom);
            UI_setBlockZoomChangesFlag(false);
            UI_resetZoomThreshold();
            admPreview::samePicture();
            break;
        }
    case ACT_ZOOM_FIT_IN:
        UI_setZoomToFitIntoWindow();
        break;
    case ACT_AUDIO_SELECT_TRACK:
        A_audioTrack();
        break;

    case ACT_OPEN_VIDEO:
        GUI_FileSelRead(QT_TRANSLATE_NOOP("adm", "Select Video File..."), (SELFILE_CB *)A_openVideo);
        break;
    case ACT_APPEND_VIDEO:
        GUI_FileSelRead(QT_TRANSLATE_NOOP("adm", "Select Video File to Append..."), (SELFILE_CB *)A_appendVideo);
        break;
    case ACT_VIDEO_PROPERTIES:
        DIA_properties();
        break;
    case ACT_PlayAvi:
        GUI_PlayAvi();
        break;

#define TOGGLE_PREVIEW ADM_PREVIEW_OUTPUT
    case ACT_PreviewChanged: {
        ADM_PREVIEW_MODE oldpreview = admPreview::getPreviewMode(),
                         newpreview = (ADM_PREVIEW_MODE)UI_getCurrentPreview();
        printf("Old preview %d, New preview mode : %d\n", oldpreview, newpreview);

        if (oldpreview == newpreview)
        {
            return;
        }
        admPreview::stop();
        admPreview::setPreviewMode(newpreview);
        admPreview::start();
        //            admPreview::update(curframe);
    }
    break;
    case ACT_StopAvi:
        if (playing)
            GUI_PlayAvi();
        break;
    case ACT_MarkA:
    case ACT_MarkB: {
        bool swapit = false;
        uint64_t pts, markA, markB;
        pts = admPreview::getCurrentPts();
        if (!prefs->get(FEATURES_SWAP_IF_A_GREATER_THAN_B, &swapit))
            swapit = true;

        markA = video_body->getMarkerAPts();
        markB = video_body->getMarkerBPts();
        if (action == ACT_MarkA)
            markA = pts;
        else
            markB = pts;
        if (markA > markB)
        {
            if (swapit) // auto swap
            {
                uint64_t y = markA;
                markA = markB;
                markB = y;
            }
            else
            {
                if (action == ACT_MarkA)
                    markB = video_body->getVideoDuration(); // reset B
                else
                    markA = 0; // reset A
            }
        }
        video_body->addToUndoQueue();
        video_body->setMarkerAPts(markA);
        video_body->setMarkerBPts(markB);
        UI_setMarkers(markA, markB);
        break;
    }
    case ACT_ResetMarkerA:
        if (0 == video_body->getMarkerAPts())
            break;
        video_body->addToUndoQueue();
        video_body->setMarkerAPts(0);
        UI_setMarkers(0, video_body->getMarkerBPts());
        break;
    case ACT_ResetMarkerB: {
        uint64_t end = video_body->getVideoDuration();
        if (end == video_body->getMarkerBPts())
            break;
        video_body->addToUndoQueue();
        video_body->setMarkerBPts(end);
        UI_setMarkers(video_body->getMarkerAPts(), end);
        break;
    }
    case ACT_ResetMarkers: {
        if (!video_body->getMarkerAPts() && video_body->getMarkerBPts() == video_body->getVideoDuration())
            break; // do nothing if the markers were not moved
        video_body->addToUndoQueue();
        A_ResetMarkers();
        break;
    }
    case ACT_Copy: {
        uint64_t markA, markB;
        markA = video_body->getMarkerAPts();
        markB = video_body->getMarkerBPts();
        if (markA > markB)
        {
            uint64_t p = markA;
            markA = markB;
            markB = p;
        }
        video_body->copyToClipBoard(markA, markB);
        break;
    }
    case ACT_Paste: {
        uint64_t currentPts = video_body->getCurrentFramePts();
        uint64_t d = video_body->getVideoDuration();
        uint64_t markA, markB;
        markA = video_body->getMarkerAPts();
        markB = video_body->getMarkerBPts();
        if (markA > markB)
        {
            uint64_t p = markA;
            markA = markB;
            markB = p;
        }
        video_body->addToUndoQueue();
        // Special case if we are at the last frame
        bool lastFrame = false;
        uint64_t pts = video_body->getLastKeyFramePts();
        if (pts != ADM_NO_PTS && currentPts >= pts) // save time if we are not at or beyond the last keyframe
        {
            GUI_infiniteForward(pts);
            uint64_t lastFramePts = video_body->getCurrentFramePts();
            if (currentPts == lastFramePts)
                lastFrame = true;
        }
        if (lastFrame)
        {
            video_body->appendFromClipBoard();
        }
        else
        {
            video_body->pasteFromClipBoard(currentPts);
        }
        ADM_cutPointType chk = ADM_EDITOR_CUT_POINT_KEY;
        if (!UI_getCurrentVCodec())
            chk = video_body->checkCutsAreOnIntra();
        if (cutsNotOnIntraWarned != (int)chk && chk != ADM_EDITOR_CUT_POINT_KEY)
        {
            const char *alert;
            bool ask = true;
            switch (chk)
            {
            case ADM_EDITOR_CUT_POINT_NON_KEY:
                alert = QT_TRANSLATE_NOOP("adm", "The cut points of the pasted video are not on keyframes.\n"
                                                 "Video saved in copy mode will be corrupted at these points.\n"
                                                 "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_BAD_POC:
                alert = QT_TRANSLATE_NOOP(
                    "adm", "This video uses non-IDR recovery points instead of IDR as keyframes. "
                           "Picture reordering information in the video stream is not reset at non-IDR frames. "
                           "The cut points of the pasted selection may result in playback interruption "
                           "due to reversed display order of frames if saved in copy mode.\n"
                           "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_MISMATCH:
                alert = QT_TRANSLATE_NOOP(
                    "adm", "Codec or codec settings across a cut point of the pasted video do not match.\n"
                           "Playback of the video saved in copy mode may stop at this point.\n"
                           "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_UNCHECKED:
                alert = QT_TRANSLATE_NOOP(
                    "adm", "Cut points of the pasted video could not be checked. "
                           "This indicates an issue with a source video, the state of editing or a bug in the program. "
                           "Please check the application log file or console output for details.\n"
                           "Try anyway?");
            default:
                ask = false;
                break;
            }
            if (ask && !GUI_Question(alert))
            {
                video_body->undo();
                cutsNotOnIntraWarned = -1;
                break;
            }
            cutsNotOnIntraWarned = (int)chk;
        }
        video_body->getVideoInfo(avifileinfo);
        d = video_body->getVideoDuration() - d;
        if (markA > currentPts)
        {
            markA += d;
            markB += d;
        }
        if (markA <= currentPts && currentPts <= markB)
        {
            markB += d;
        }
        video_body->setMarkerAPts(markA);
        video_body->setMarkerBPts(markB);
        A_Resync();
        if (!lastFrame)
            currentPts += d;
        if (!video_body->goToTimeVideo(currentPts))
        {
            // If seek fails, we may crash in admPreview::samePicture()
            // due to _currentSegment and _currentPts going out of sync.
            // Rewind to get on firm ground again.
            A_Rewind();
        }
        else
        {
            admPreview::samePicture();
            GUI_setCurrentFrameAndTime();
        }
    }
    break;

    case ACT_Undo:
    case ACT_Redo:
        if (avifileinfo)
        {
            uint64_t currentPts = video_body->getCurrentFramePts();
            bool r = false;

            if (action == ACT_Undo)
            {
                r = video_body->undo();
            }
            else
            {
                r = video_body->redo();
            }
            if (r)
            {
                video_body->getVideoInfo(avifileinfo);
                A_Resync();
                A_Rewind();

                if (currentPts <= video_body->getVideoDuration())
                    r = GUI_GoToTime(currentPts);
                else
                    r = false;

                EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
                if (ed && ed->edTrack)
                    ed->edTrack->goToTime(r ? currentPts : 0); // update audio segment
            }
        }
        break;

    case ACT_ResetSegments:
        if (avifileinfo)
            if (GUI_Question(QT_TRANSLATE_NOOP("adm", "Are you sure?")))
            {
                video_body->clearUndoQueue();
                video_body->resetSeg();
                video_body->getVideoInfo(avifileinfo);

                A_Resync();
                A_Rewind();
                A_ResetMarkers();

                // forget last project file
                video_body->setProjectName("");
            }
        break;

    case ACT_Delete:
    case ACT_Cut: {
        uint64_t a = video_body->getMarkerAPts();
        uint64_t b = video_body->getMarkerBPts();
        uint64_t current = video_body->getCurrentFramePts();
        uint64_t before = video_body->getVideoDuration();
        if (a > b)
        {
            uint64_t p = a;
            a = b;
            b = p;
        }
        // Special case of B being beyond the last frame
        bool lastFrame = false;
        if (b == before)
        {
            lastFrame = true;
        }
        else
        {
            uint64_t pts = video_body->getLastKeyFramePts();
            if (pts != ADM_NO_PTS && b >= pts) // don't waste time if B is before the last keyframe
            {
                GUI_infiniteForward(pts);
                uint64_t lastFramePts = video_body->getCurrentFramePts();
                if (b > lastFramePts)
                    lastFrame = true; // B is beyond the last frame
            }
        }
        if (!a && lastFrame)
        {
            if (action == ACT_Cut)
            {
                GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Cutting"),
                              QT_TRANSLATE_NOOP("adm", "It is impossible to cut out the entire video. Please recheck "
                                                       "the position of markers A and B."));
            }
            else
            {
                GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Deleting"),
                              QT_TRANSLATE_NOOP("adm", "It is impossible to delete the entire video. Please recheck "
                                                       "the position of markers A and B."));
            }
            break;
        }
        if (action == ACT_Cut)
        {
            video_body->copyToClipBoard(a, b);
        }
        video_body->addToUndoQueue();
        bool result = false;
        if (lastFrame)
            result = video_body->truncate(a);
        else
            result = video_body->remove(a, b);
        if (!result)
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Cutting"), QT_TRANSLATE_NOOP("adm", "Error while cutting out."));
            break;
        }
        ADM_cutPointType chk = ADM_EDITOR_CUT_POINT_KEY;
        if (!lastFrame && !UI_getCurrentVCodec())
            chk = video_body->checkCutIsOnIntra(a);
        if (cutsNotOnIntraWarned != (int)chk && chk != ADM_EDITOR_CUT_POINT_KEY)
        {
            const char *alert;
            bool ask = true;
            switch (chk)
            {
            case ADM_EDITOR_CUT_POINT_NON_KEY:
                if (action == ACT_Cut)
                    alert = QT_TRANSLATE_NOOP("adm", "The end point of the cut is not on a keyframe.\n"
                                                     "Video saved in copy mode will be corrupted at this point.\n"
                                                     "Proceed anyway?");
                else
                    alert = QT_TRANSLATE_NOOP("adm", "The end point of the deletion is not on a keyframe.\n"
                                                     "Video saved in copy mode will be corrupted at this point.\n"
                                                     "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_BAD_POC:
                if (action == ACT_Cut)
                    alert = QT_TRANSLATE_NOOP(
                        "adm", "This video uses non-IDR recovery points instead of IDR as keyframes. "
                               "Picture reordering information in the video stream is not reset at non-IDR frames. "
                               "The chosen start and end points of the cut may result in playback interruption "
                               "due to reversed display order of frames if saved in copy mode.\n"
                               "Proceed anyway?");
                else
                    alert = QT_TRANSLATE_NOOP(
                        "adm", "This video uses non-IDR recovery points instead of IDR as keyframes. "
                               "Picture reordering information in the video stream is not reset at non-IDR frames. "
                               "The chosen start and end points of the deletion may result in playback interruption "
                               "due to reversed display order of frames if saved in copy mode.\n"
                               "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_MISMATCH:
                if (action == ACT_Cut)
                    alert =
                        QT_TRANSLATE_NOOP("adm", "Codec or codec settings across the cut do not match. "
                                                 "Playback of the video saved in copy mode may stop at this point.\n"
                                                 "Proceed anyway?");
                else
                    alert =
                        QT_TRANSLATE_NOOP("adm", "Codec or codec settings across the deletion do not match. "
                                                 "Playback of the video saved in copy mode may stop at this point.\n"
                                                 "Proceed anyway?");
                break;
            case ADM_EDITOR_CUT_POINT_UNCHECKED:
                alert = QT_TRANSLATE_NOOP(
                    "adm", "Cut points could not be checked.\n"
                           "This indicates an issue with a source video, the state of editing or a bug in the program. "
                           "Please check the application log file or console output for details.\n"
                           "Proceed anyway?");
                break;
            default:
                ask = false;
                break;
            }
            if (ask && !GUI_Question(alert))
            {
                video_body->undo();
                cutsNotOnIntraWarned = -1;
                break;
            }
            cutsNotOnIntraWarned = (int)chk;
        }
        A_ResetMarkers();
        A_Resync(); // total duration & stuff

        // If A was at the first frame of a segment with start time in ref = 0 and ref video
        // not starting at zero, the part of the segment before the first frame got deleted too,
        // even if we didn't ask for that explicitely. We must adjust timestamps accordingly
        // to avoid seek errors. This is not relevant for truncating.
        uint64_t after = video_body->getVideoDuration();
        uint64_t c = 0;
        if (!lastFrame)
            c = before - after + a - b;
        if (current >= a) // else current is before A, so nothing to do
        {
            if (current < b) // current is between A & B => A
            {
                current = a - c;
            }
            else // current is after the removed chunk, adjust
            {
                current -= b - a + c;
            }
        }
        if (current >= after) // the current frame is gone
        {
            // Can we go to the last keyframe before the cut?
            current = after;
            // current equal segment duration does not belong to this segment or
            // to any segment, when it matches video duration, triggering rewind.
            if (current && current != ADM_NO_PTS)
                current--;
            if (!video_body->getPKFramePTS(&current))
            { // nope
                A_Rewind();
                break;
            }
        }
        if (!video_body->goToTimeVideo(current))
        {
            // If seek fails, we may crash in admPreview::samePicture()
            // due to _currentSegment and _currentPts going out of sync.
            // Rewind to get on firm ground again.
            A_Rewind();
        }
        else
        {
            admPreview::samePicture();
            GUI_setCurrentFrameAndTime();
        }
    }
    break;
        // set decoder option (post processing ...)
    case ACT_DecoderOption:
        video_body->setDecodeParam(admPreview::getCurrentPts());

        break;
    case ACT_VIDEO_FILTERS:
        GUI_handleVFilter();
        break;
    case ACT_VIDEO_PARTIAL_FILTERS:
        GUI_handleVPartialFilter();
        break;

    case ACT_HEX_DUMP:
        GUI_showCurrentFrameHex();
        break;
    case ACT_SIZE_DUMP:
        GUI_showSize();
        break;
    case ACT_TimeShift:
        A_TimeShift();
        break;
    default:
        printf("\n unhandled action %d\n", action);
        ADM_assert(0);
        return;
    }
}

//_____________________________________________________________
//
// Open AVI File
//    mode 0: normal
//    mode 1: Suspicious
//_____________________________________________________________

/**
        \fn A_openVideo
        \brief Open (replace mode) a video
*/
int A_openVideo(const char *name)
{
    uint8_t res;
    char *longname;
    uint32_t magic[4];
    uint32_t id = 0;
    cutsNotOnIntraWarned = false;

    if (playing)
        return 0;
    /// check if name exists
    FILE *fd;
    fd = ADM_fopen(name, "rb");
    if (!fd)
    {
        if (errno == EACCES)
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Permission error"), QT_TRANSLATE_NOOP("adm", "Cannot open \"%s\"."),
                          name);
        }
        if (errno == ENOENT)
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "File error"), QT_TRANSLATE_NOOP("adm", "\"%s\" does not exist."),
                          name);
        }
        return 0;
    }
    if (4 == fread(magic, 4, 4, fd))
        id = R32(magic[0]);
    fclose(fd);

    GUI_close(); // Cleanup

    //  DIA_StartBusy ();
    /*
     ** we may get a relative path by cmdline
     */
    longname = ADM_PathCanonize(name);

    // check if avisynth input is given
    if (fourCC::check(id, (uint8_t *)"ADAP"))
        res = video_body->addFile(AVS_PROXY_DUMMY_FILE);
    else
        res = video_body->addFile(longname);

    // forget last project file
    video_body->setProjectName("");

    if (res != ADM_OK) // an error occured
    {
        delete[] longname;
        if (ADM_IGN == res)
        {
            return 0;
        }

        if (fourCC::check(id, (uint8_t *)"//AD"))
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Cannot open project using the video loader."),
                          QT_TRANSLATE_NOOP("adm", "Try 'File' -> 'Load/Run Project...'"));
        }
        else
        {
            GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Could not open the file"), NULL);
        }
        return 0;
    }

    {
        int i;
#if 0
        FILE *fd = NULL;
        char magic[4];

        /* check myself it is a project file (transparent detected and read
         ** by video_body->addFile (name);
         */
        //#warning FIXME
        if ((fd = ADM_fopen(longname, "rb")))
        {
            if (fread(magic, 4, 1, fd) == 4)
            {
                /* remember a workbench file */
                if (!strncmp(magic, "ADMW", 4))
                {
                    actual_workbench_file = ADM_strdup(longname);
                }
            }
            fclose(fd);
        }
#endif
        /* remember any video or workbench file to "recent" */
        prefs->set_lastfile(longname);
        updateLoaded();
        UI_updateRecentMenu();
        int ac = 0; // audio codec = copy
        bool loadDefault;
        if (!prefs->get(RESET_ENCODER_ON_VIDEO_LOAD, &loadDefault))
            loadDefault = false;
        // if true, discard changes in output config on video load
        if (loadDefault)
        {
            UI_setAudioCodec(ac); // revert to copy, we don't save default audio codec config yet
            A_loadDefaultSettings();
        }
        else
        {
            ac = UI_getCurrentACodec();
            if (video_body->getNumberOfActiveAudioTracks())
                audioCodecSetByIndex(0, ac); // try to preserve audio codec
        }

        if (currentaudiostream)
        {
            uint32_t nbAudio;
            audioInfo *infos = NULL;
            if (video_body->getAudioStreamsInfo(admPreview::getCurrentPts() + 1, &nbAudio, &infos))
            {
                if (nbAudio > 1)
                { // Multiple track warn user
                    GUI_Info_HIG(ADM_LOG_INFO, QT_TRANSLATE_NOOP("adm", "Multiple Audio Tracks"),
                                 QT_TRANSLATE_NOOP("adm", "The file you just loaded contains several audio tracks.\n"
                                                          "Go to Audio->MainTrack to select the active one."));
                }
            }
            if (infos)
                delete[] infos;
            // Revert mixer to copy
            // setCurrentMixerFromString("NONE");
            EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
            if (ed)
                ed->audioEncodingConfig.audioFilterSetMixer(CHANNEL_INVALID);
        }
        for (i = strlen(longname); i >= 0; i--)
        {
#ifdef _WIN32
            if (longname[i] == '\\' || longname[i] == '/')
#else
            if (longname[i] == '/')
#endif
            {

                i++;
                break;
            }
        }
        UI_setTitle(longname + i);
    }

    delete[] longname;
    return 1;
}
/**
    \fn updateLoaded
    \brief update the UI after loading a file

*/
void updateLoaded(bool resetMarker)
{
    avifileinfo = new aviInfo;
    if (!video_body->getVideoInfo(avifileinfo))
    {
        ADM_warning("\n get info failed...cancelling load...\n");
        delete avifileinfo;
        avifileinfo = NULL;
        return;
    }
    // now get audio information if exists
    WAVHeader *wavinfo = NULL;
    ADM_audioStream *stream = NULL;
    video_body->getDefaultAudioTrack(&stream);
    if (stream)
        wavinfo = stream->getInfo();

    if (!wavinfo)
    {
        ADM_info("\n *** NO AUDIO ***\n");
        wavinfo = (WAVHeader *)NULL;
    }

    // Init renderer
    admPreview::setMainDimension(avifileinfo->width, avifileinfo->height, ZOOM_AUTO);
    // Draw first frame
    GUI_setAllFrameAndTime();
    if (resetMarker)
        A_ResetMarkers();
    A_Rewind();
    UI_setAudioTrackCount(video_body->getNumberOfActiveAudioTracks());
    ADM_info(" conf updated \n");
    UI_setDecoderName(video_body->getVideoDecoderName());
    UI_displayZoomLevel();
}

//___________________________________________
//  Append an AVI to the existing one
//___________________________________________
int A_appendVideo(const char *name)
{
    if (playing)
        return 0;
    bool markerChanged = false;
    // Check if A or B was changed
    uint64_t beginPts = 0, beginDts;
    uint32_t flags;
    uint64_t markerA = video_body->getMarkerAPts();
    uint64_t markerB = video_body->getMarkerBPts();
    video_body->getVideoPtsDts(0, &flags, &beginPts, &beginDts);
    uint64_t theEnd = video_body->getVideoDuration();

    ADM_info("Start is %s, marker A is %s\n", ADM_us2plain(beginPts), ADM_us2plain(markerA));
    ADM_info("End is %s, marker B is %s\n", ADM_us2plain(theEnd), ADM_us2plain(markerB));

    if (theEnd != markerB)
    {
        markerChanged = true;
    }
    // If markerA is zero, it means beginning of the video, no need to check further
    if (markerA)
    {
        if (markerA != beginPts) // moved ?
            markerChanged = true;
    }

    video_body->addToUndoQueue();
    uint64_t currentPts = admPreview::getCurrentPts();
    if (!video_body->addFile(name))
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Something failed when appending"), NULL);
        return 0;
    }

    if (!markerChanged)
    {
        video_body->setMarkerBPts(video_body->getVideoDuration());
        ADM_info("Extending marker B to the end (%s)\n", ADM_us2plain(video_body->getMarkerBPts()));
    }
    admPreview::seekToTime(currentPts);
    ReSync();
    GUI_setCurrentFrameAndTime();
    UI_setMarkers(video_body->getMarkerAPts(), video_body->getMarkerBPts());
    return 1;
}

//
//      Whenever a changed happened in the the stream, resync
//  related infos including audio & video filters

void ReSync(void)
{
    // update audio stream
    // If we were on avi , mark it...
    GUI_setAllFrameAndTime();
}

//      Clean up
//      free all pending stuff, make leakchecker happy
//
void cleanUp(void)
{
    bool saveprefsonexit;

    prefs->get(FEATURES_SAVEPREFSONEXIT, &saveprefsonexit);

    if (saveprefsonexit)
    {
        prefs->save();
    }

    if (avifileinfo)
    {
        delete avifileinfo;
        avifileinfo = NULL;
    }
    admPreview::cleanUp();
    if (video_body)
    {
        delete video_body;
        video_body = NULL;
    }

    currentaudiostream = NULL;
    //    filterCleanUp();
}

// #warning fixme
/**
 * \fn parseScript
 * @param engine
 * @param name
 * @param mode
 * @return
 */
bool parseScript(IScriptEngine *engine, const char *name, IScriptEngine::RunMode mode)
{
    if (playing)
        return false;

    bool ret;
    char *longname = ADM_PathCanonize(name);

    ret = engine->runScriptFile(longname, IScriptEngine::Normal);

    if (ret)
    {
        video_body->setProjectName(longname);
    }

    if (strcmp(longname, getDefaultSettingsFilePath().c_str()) && strcmp(longname, getLastSessionFilePath().c_str()) &&
        strcmp(longname, getCrashRecoveryFilePath().c_str()) &&
        strstr(longname, ADM_getCustomDir().c_str()) != longname &&
        strstr(longname, ADM_getJobDir().c_str()) != longname)
    {
        prefs->set_lastprojectfile(longname);
        UI_updateRecentProjectMenu();
    }
    delete[] longname;
    longname = NULL;

    // update main menu shift
    EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
    if (ed)
    {
        UI_setAudioCodec(ed->encoderIndex);
        UI_setTimeShift(ed->audioEncodingConfig.shiftEnabled, ed->audioEncodingConfig.shiftInMs);
    }

    return ret;
}
/**
 *
 * @param name
 * @return
 */
bool A_runPythonScript(const std::string &name)
{
    if (!getPythonScriptEngine())
    {
        GUI_Info_HIG(ADM_LOG_IMPORTANT, "Qt",
                     QT_TRANSLATE_NOOP("adm", "The tinypy plugin is missing.\nExpect problems."));
        return false;
    }

    return parseScript(getPythonScriptEngine(), name.c_str(), IScriptEngine::Normal);
}
bool A_parseScript(IScriptEngine *engine, const char *name)
{
    return parseScript(engine, name, IScriptEngine::Normal);
}

void A_saveScript(IScriptEngine *engine, const char *name)
{
    IScriptWriter *writer = engine->createScriptWriter();
    ADM_ScriptGenerator generator(video_body, writer);
    std::stringstream stream(std::stringstream::in | std::stringstream::out);
    std::string fileName = name;

    generator.generateScript(stream);
    delete writer;

    if (fileName.rfind(".") == std::string::npos)
    {
        fileName += "." + engine->defaultFileExtension();
    }

    FILE *file = ADM_fopen(fileName.c_str(), "wt");
    string script = stream.str();

    ADM_fwrite(script.c_str(), script.length(), 1, file);
    ADM_fclose(file);
}
/**
 *
 * @param engine
 * @param name
 */

void A_saveDefaultSettings()
{
    IScriptEngine *engine = getPythonScriptEngine();
    if (!engine)
    {
        GUI_Info_HIG(ADM_LOG_IMPORTANT, "Qt",
                     QT_TRANSLATE_NOOP("adm", "The tinypy plugin is missing.\nExpect problems."));
        return;
    }

    IScriptWriter *writer = engine->createScriptWriter();
    ADM_ScriptGenerator generator(video_body, writer);
    std::stringstream stream(std::stringstream::in | std::stringstream::out);

    generator.generateScript(stream, ADM_ScriptGenerator::GENERATE_SETTINGS);
    delete writer;

    std::string script = stream.str();
    ADM_info("Generated settings:\n");
    printf("%s\n", script.c_str());

    FILE *file = ADM_fopen(getDefaultSettingsFilePath().c_str(), "wt");
    ADM_fwrite(script.c_str(), script.length(), 1, file);
    ADM_fclose(file);
}
/**
 * \fn A_loadDefaultSettings
 * @return
 */
bool A_loadDefaultSettings(void)
{
    if (ADM_fileExist(getDefaultSettingsFilePath().c_str()) && ADM_fileSize(getDefaultSettingsFilePath().c_str()) > 5)
    {
        return A_runPythonScript(getDefaultSettingsFilePath());
    }
    else
    { // default to MKV as output container instead of AVI if no user defined default settings exist
        return video_body->setContainer("MKV", NULL);
    }
    return false;
}
/**
 * \fn A_saveSession
 */
bool A_saveSession(void)
{
    static std::string tmp;
    IScriptEngine *engine = getPythonScriptEngine();
    if (!engine)
        return false;
    ADM_info("Saving the current state of editing\n");
    if (ADM_fileExist(getLastSessionFilePath().c_str()))
    {
        if (!tmp.size())
        {
            tmp = getLastSessionFilePath();
            size_t pos = tmp.find_last_of(".");
            if (pos != std::string::npos)
                tmp = tmp.substr(0, pos);
            tmp += ".tmp";
        }
        A_saveScript(engine, tmp.c_str());
        if (!ADM_eraseFile(getLastSessionFilePath().c_str()))
        {
            ADM_warning("Could not delete the old saved session (%s)\n", getLastSessionFilePath().c_str());
            if (!ADM_eraseFile(tmp.c_str()))
                ADM_warning("Could not delete temporary file (%s)\n", tmp.c_str());
            return false;
        }
        return !!rename(tmp.c_str(), getLastSessionFilePath().c_str());
    }
    A_saveScript(engine, getLastSessionFilePath().c_str());
    return true;
}
/**
 * \fn A_checkSavedSession
 */
bool A_checkSavedSession(bool load)
{
    if (false == ADM_fileExist(getLastSessionFilePath().c_str()))
        return false;
    if (!load)
        return true;
    IScriptEngine *engine = getPythonScriptEngine();
    if (!engine)
        return false;
    ADM_info("Restoring the last editing state from %s\n", getLastSessionFilePath().c_str());
    if (false == A_parseScript(engine, getLastSessionFilePath().c_str()))
        return false;
    video_body->rewind();
    admPreview::samePicture();
    A_Resync();
    if (false == ADM_eraseFile(getLastSessionFilePath().c_str()))
        ADM_warning("Could not delete %s\n", getLastSessionFilePath().c_str());
    return true;
}
/**
    Unpack all frames without displaying them to check for error

*/
void A_videoCheck(void)
{
#if 0
uint32_t nb=0;
//uint32_t buf[720*576*2];
uint32_t error=0;
ADMImage *aImage;
DIA_workingBase *work;

    nb = avifileinfo->nb_frames;
    work=createWorking(QT_TRANSLATE_NOOP("adm","Checking video"));
    aImage=new ADMImage(avifileinfo->width,avifileinfo->height);
  for(uint32_t i=0;i<nb;i++)
  {
    work->update(i, nb);
          if(!work->isAlive()) break;
    if(!GUI_getFrameContent (aImage,i))
    {
        error ++;
        printf("Frame %u has error\n",i);
    }

    };
  delete work;
  delete aImage;
  if(error==0)
    GUI_Info_HIG(ADM_LOG_IMPORTANT,QT_TRANSLATE_NOOP("adm","No error found"), NULL);
else
    {
        char str[400];
                sprintf(str,QT_TRANSLATE_NOOP("adm","Errors found in %u frames"),error);
        GUI_Info_HIG(ADM_LOG_IMPORTANT,str, NULL);

    }
    GUI_GoToFrame(0);
#endif
}
int A_delete(uint32_t start, uint32_t end)
{
    uint32_t count;

    aviInfo info;
    ADM_assert(video_body->getVideoInfo(&info));
    count = end - start;

    if (end < start)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Marker A > B"),
                      QT_TRANSLATE_NOOP("adm", "Cannot delete the selection."));
        return 0;
    }
    if (count >= info.nb_frames - 1)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "You can't remove all frames"), NULL);
        return 0;
    }

    //      video_body->dumpSeg ();
    //      if (!video_body->removeFrames (start, end))
    if (0)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Something bad happened"), NULL);
        return 0;
    }
    //      video_body->dumpSeg ();
    // resync GUI and video
    if (!video_body->updateVideoInfo(avifileinfo))
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Something bad happened (II)"), NULL);
    }

    A_ResetMarkers();
    GUI_setAllFrameAndTime();
    ReSync();
    return 1;
}
extern int DIA_getMPParams(uint32_t *pplevel, uint32_t *ppstrength, bool *swap);
//

//
void A_setPostproc(void)
{
    uint32_t type, strength;
    bool swap;
    stagedActionSuccess = 0;
    if (!avifileinfo)
        return;

    video_body->getPostProc(&type, &strength, &swap);

    if (DIA_getMPParams(&type, &strength, &swap))
    {
        video_body->setPostProc(type, strength, swap);
        stagedActionSuccess = 1;
        return;
    }
}

//
extern int DIA_getHDRParams(uint32_t *toneMappingMethod, float *saturationAdjust, float *boostAdjust, bool *adaptiveRGB,
                            uint32_t *gamutMethod);
//
void A_setHDRConfig(void)
{
    uint32_t method, gamutMethod;
    float saturation, boost;
    bool adaptiveRGB;
    stagedActionSuccess = 0;
    if (!avifileinfo)
        return;

    if (!video_body->getHDRConfig(&method, &saturation, &boost, &adaptiveRGB, &gamutMethod))
    {
        method = 1;
        saturation = boost = 1.;
        adaptiveRGB = true;
        gamutMethod = 0;
    }

    if (DIA_getHDRParams(&method, &saturation, &boost, &adaptiveRGB, &gamutMethod))
    {
        video_body->setHDRConfig(method, saturation, boost, adaptiveRGB, gamutMethod);
        stagedActionSuccess = 1;
        return;
    }
}

/**

*/
int A_setAudioTrack(int track)
{
    video_body->changeAudioStream(0, track);
    return true;
}
/**
      \fn A_audioTrack
      \brief Allow to select audio track
*/

void A_audioTrack(void)
{
    PoolOfAudioTracks *pool = video_body->getPoolOfAudioTrack();
    ActiveAudioTracks *active = video_body->getPoolOfActiveAudioTrack();
    DIA_audioTrackBase *base = createAudioTrack(pool, active);
    base->run();
    delete base;
    EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
    if (ed)
    {
        UI_setAudioCodec(ed->encoderIndex);
        UI_setTimeShift(ed->audioEncodingConfig.shiftEnabled, ed->audioEncodingConfig.shiftInMs);
    }
    UI_setAudioTrackCount(video_body->getNumberOfActiveAudioTracks());
}
#if 0
        audioInfo *infos=NULL;
        uint32_t nbAudioTracks,currentAudioTrack;
        uint32_t newTrack;

        if(!video_body->getAudioStreamsInfo(0,&nbAudioTracks,&infos)) return;
        currentAudioTrack=video_body->getCurrentAudioStreamNumber(0);
        newTrack=currentAudioTrack;
        // Now build the list of embedded track
#define MAX_AUDIO_TRACK 10
#define MAX_AUDIO_TRACK_NAME 100
        diaMenuEntryDynamic *sourceavitracks[MAX_AUDIO_TRACK];
        char string[MAX_AUDIO_TRACK_NAME];
        for(int i=0;i<nbAudioTracks;i++)
        {
          sprintf(string,"Audio track %d (%s, %d channels, %d kbit/s)",i,
                        getStrFromAudioCodec(infos[i].encoding),
                        infos[i].channels,infos[i].bitrate);
           sourceavitracks[i]=new diaMenuEntryDynamic(i,string,NULL);
        }
         if(infos) delete [] infos;

         diaElemMenuDynamic   sourceFromVideo(&newTrack,QT_TRANSLATE_NOOP("adm","_Track from video:"),nbAudioTracks,sourceavitracks);
         diaElem *allWidgets[]={&sourceFromVideo};

         if( diaFactoryRun(QT_TRANSLATE_NOOP("adm","Main Audio Track"),1,allWidgets))
         {
            if(newTrack!=currentAudioTrack)
            {
                    A_setAudioTrack(newTrack);
            }
        }

roger_and_out:
         /* Clean up */
         for(int i=0;i<nbAudioTracks;i++)
            delete sourceavitracks[i];
        return;

}
#endif
/**
 * \fn extractTrackIndex
 * @param trackIdxTxt
 * @return
 */
static inline int extractTrackIndex(const char *trackIdxTxt)
{
    static const int MAX_TRACK_IDX_LENGTH =
        4 + 1; // Max length expected = 0xFF. Test 1 char more than assumed to take the \0 into account
    size_t trackTxtLen = strlen(trackIdxTxt);
    if (trackTxtLen >= MAX_TRACK_IDX_LENGTH)
        trackTxtLen = MAX_TRACK_IDX_LENGTH;
    if (MAX_TRACK_IDX_LENGTH == trackTxtLen)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Invalid audio index given"), NULL);
        return -1;
    }
    char *endptr;
    int trackIdx = static_cast<int>(strtol(trackIdxTxt, &endptr, 0));

    if (trackIdx < 1 || trackIdx > ADM_MAXIMUM_AMOUT_AUDIO_STREAMS || endptr != trackIdxTxt + trackTxtLen)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Invalid audio index given"), NULL);
        return -1;
    }
    return trackIdx - 1;
}
/**
        \fn A_externalAudioTrack
        \brief Select external audio track (for 2nd track)
        @param trackIdx The track index to use, according to the active track list
        @param filename
*/
void A_externalAudioTrack(const char *trackIdxTxt, const char *filename)
{
    int trackIdx = extractTrackIndex(trackIdxTxt);
    if (trackIdx == -1)
        return;
    printf("\texternal audio index = %d\n", trackIdx);
    printf("\tgiven '%s'\n", filename);

    ADM_edAudioTrackExternal *ext = create_edAudioExternal(filename);
    if (!ext)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Error"),
                      QT_TRANSLATE_NOOP("adm", "Cannot use that file as audio track"));
        return;
    }
    // add to the list of the known input files
    ActiveAudioTracks *tracks = video_body->getPoolOfActiveAudioTrack();
    PoolOfAudioTracks *pool = video_body->getPoolOfAudioTrack();
    int assumedIdx = pool->size();
    pool->addInternalTrack(ext);
    pool->dump();
    // the shortcut insert which should work the most time
    if (pool->size() > assumedIdx && pool->at(assumedIdx) == ext)
    {
        if (trackIdx < tracks->size())
            tracks->insertTrack(trackIdx, assumedIdx, ext);
        else
            tracks->insertTrack(tracks->size(), assumedIdx, ext);
        if (tracks->size() > trackIdx + 1)
            tracks->removeTrack(trackIdx + 1);
    }
    else
    {
        assumedIdx = -1;
        for (int i = 0; i < pool->size(); i++)
        {
            if (pool->at(i) == ext)
            {
                assumedIdx = i;
                break;
            }
        }
        if (assumedIdx == -1)
        {
            // This should never happen, but who knows?
            GUI_Error_HIG(
                QT_TRANSLATE_NOOP("adm", "Error"),
                QT_TRANSLATE_NOOP(
                    "adm", "Audio file not found in list, even though it should be there. Create a bug report!"));
            return;
        }
        printf("assumed Idx = %d\n", assumedIdx);
        if (trackIdx >= tracks->size())
        {
            tracks->addTrack(assumedIdx, ext);
        }
        else
        {
            tracks->removeTrack(trackIdx);
            tracks->insertTrack(trackIdx, assumedIdx, ext);
        }
    }
    tracks->dump();
    printf("external file appended");
}

/**
    \fn A_setAudioLang
    \brief Setting the language name for the given track index
    @param trackIndex The track index to modify, according to the active track list
*/
void A_setAudioLang(const char *trackIdxTxt, const char *langueName)
{
    if (!video_body->isFileOpen())
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Error"),
                      QT_TRANSLATE_NOOP("adm", "Unable to set the audio language: No video loaded yet!"));
        return;
    }
    int trackIdx = extractTrackIndex(trackIdxTxt);
    if (trackIdx == -1)
        return;
    ActiveAudioTracks *tracks = video_body->getPoolOfActiveAudioTrack();
    if (tracks->size() == 0)
    {
        GUI_Error_HIG(
            QT_TRANSLATE_NOOP("adm", "Error"),
            QT_TRANSLATE_NOOP(
                "adm", "Setting the language for the given track index is not possible: Video has no audio file!"));
        return;
    }
    else if (tracks->size() <= trackIdx)
    {
        GUI_Error_HIG(
            QT_TRANSLATE_NOOP("adm", "Error"),
            QT_TRANSLATE_NOOP("adm",
                              "Setting the language for the given track index is not possible: Invalid track index!"));
        return;
    }
    tracks->atEdAudio(trackIdx)->setLanguage(langueName);
}

/**
    \fn A_Resync
    \brief
*/
void A_Resync(void)
{
    if (!avifileinfo)
        return;
    GUI_setAllFrameAndTime();
    UI_setMarkers(video_body->getMarkerAPts(), video_body->getMarkerBPts());
    UI_setAudioTrackCount(video_body->getNumberOfActiveAudioTracks());
}
bool DIA_job_select(char **jobname, char **filename);
void A_addJob(void)
{
    char *name = NULL;
    char *fullname;
    const char *base;
    char *final = NULL;

    if (!DIA_job_select(&name, &final))
        return;
    if (!name || !final)
        return;
    if (!*name || !*final)
        return;

    base = ADM_strdup(ADM_getJobDir().c_str());
    fullname = new char[strlen(name) + strlen(base) + 2 + 4];

    strcpy(fullname, base);
    strcat(fullname, "/");
    strcat(fullname, name);
    strcat(fullname, ".py");

    A_saveScript(getScriptEngines()[0], final);

    delete[] fullname;
    ADM_dealloc(base);
    ADM_dealloc(name);
    ADM_dealloc(final);
}
/**
    \fn GUI_GetScale
    \brief Return the % of the scale, between 0 and ADM_SCALE_SIZE

*/
uint32_t GUI_GetScale(void)
{

    double percent;
    float tg;

    percent = UI_readScale();
    tg = ADM_SCALE_SIZE * percent / 100.;

    return (uint32_t)floor(tg);
    ;
}
/**
    \fn GUI_SetScale
    \brief Set the scale, input is between 0 and ADM_SCALE_SIZE (max)
*/
void GUI_SetScale(uint32_t scale)
{
    double percent;
    percent = scale;
    percent /= ADM_SCALE_SIZE;
    percent *= 100;
    UI_setScale(percent);
}

/**
      \fn GUI_getFrameContent
      \brief fill image with content of frame frame
*/
uint8_t GUI_getFrameContent(ADMImage *image, uint32_t frame)
{
    //  uint32_t flags;
    //  if(!video_body->getUncompressedFrame(frame,image,&flags)) return 0;
    return 1;
}
/**
    \fn GUI_close
    \brief Close opened file and cleanup filters etc..
*/
uint8_t GUI_close(void)
{
    if (avifileinfo) // already opened ?
    {                // delete everything
        // if preview is on
        if (ADM_PREVIEW_NONE != admPreview::getPreviewMode())
        {
            admPreview::stop();
            admPreview::setPreviewMode(ADM_PREVIEW_NONE);
        }
        admPreview::setMainDimension(0, 0, ZOOM_1_1); // destroy preview
        UI_setNeedsResizingFlag(false);
        int32_t inactiveVolume[8] = {255, 255, 255, 255, 255, 255, 255, 255};
        UI_setVUMeter(inactiveVolume);
        A_saveSession();
        delete avifileinfo;
        // delete wavinfo;
        avifileinfo = NULL;
        ADM_vf_clearFilters();
        video_body->clearUndoQueue();
        video_body->cleanup();
        UI_setAudioTrackCount(0);
        UI_setTimeShift(false, 0);
        //      filterCleanUp ();
        UI_setTitle(NULL);
        UI_setDecoderName("XXXX");
        UI_displayZoomLevel();

        A_ResetMarkers();
        ReSync();

        return 1;
    }
    return 0;
}
/**
      \fn GUI_avsProxy
      \brief Shortcut to connect to avsProxy
*/

void GUI_avsProxy(void)
{
    if (playing)
        return;
    uint8_t res;

    GUI_close();
    res = video_body->addFile(AVS_PROXY_DUMMY_FILE);
    // forget last project file
    video_body->setProjectName("avsproxy");
    if (res != ADM_OK) // an error occured
    {
        currentaudiostream = NULL;
        avifileinfo = NULL;
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "AvsProxy"),
                      QT_TRANSLATE_NOOP("adm", "Failed to connect to avsproxy.\nIs it running ?"));
        return;
    }

    updateLoaded();
    UI_setTitle(QT_TRANSLATE_NOOP("adm", "avsproxy"));
    return;
}
/**
      \fn GUI_showCurrentFrameHex
      \brief Display the first 32 bytes of the current frame in hex
*/

void GUI_showCurrentFrameHex(void)
{
#if 0
 uint8_t *buffer;
 uint32_t fullLen,flags;
 char sType[5];
 char sSize[15];
 ADMCompressedImage image;
 uint8_t seq;

 if (!avifileinfo) return;

 buffer=new uint8_t [avifileinfo->width*avifileinfo->height*3];
 image.data=buffer;


 video_body->getFrame (video_body->getCurrentFrame(),&image,&seq);
 fullLen=image.dataLength;
 video_body->getFlags (video_body->getCurrentFrame(), &flags);

 diaElemHex binhex("*****",fullLen,buffer);

 if(flags&AVI_KEY_FRAME) sprintf(sType,"I");
  else if(flags&AVI_B_FRAME) sprintf(sType,"B");
    else sprintf(sType,"P");
 sprintf(sSize,"%d bytes",fullLen);

 diaElemReadOnlyText Type(sType,QT_TRANSLATE_NOOP("adm","Frame type:"));
 diaElemReadOnlyText Size(sSize,QT_TRANSLATE_NOOP("adm","Frame size:"));
 diaElem *elems[]={&Type,&Size,&binhex   };
 if(diaFactoryRun(QT_TRANSLATE_NOOP("adm","Frame Hex Dump"),3,elems))

 delete [] buffer;
#endif
}
/**
    \fn GUI_showSize
    \brief Show frame size

*/
#define DUMP_SIZE 30
void GUI_showSize(void)
{
#if 0
uint8_t *buffer;
 uint32_t fullLen,flags;
 ADMCompressedImage image;
 uint8_t seq;
 char                text[DUMP_SIZE][100];

 if (!avifileinfo) return;

 buffer=new uint8_t [avifileinfo->width*avifileinfo->height*3];
 image.data=buffer;
    for(int i=0;i<DUMP_SIZE;i++)
    {
        int target=video_body->getCurrentFrame()+i;
        video_body->getFlags ( target,&flags);
        video_body->getFrame ( target,&image,&seq);
        fullLen=image.dataLength;
        sprintf(text[i],"Frame %d:%d",target,fullLen);
        printf("%s\n",text[i]);
    }




 delete [] buffer;
#endif
}

/**
 *      \fn UI_getPreferredRender
 *      \brief Returns to render lib the user preferred rendering method
 *
 */
ADM_RENDER_TYPE UI_getPreferredRender(void)
{
    char *displ;
    unsigned int renderI;
    ADM_RENDER_TYPE render;

#if !defined _WIN32 && !defined(__APPLE__)
    // First check if local
    // We do it in a very wrong way : If DISPLAY!=:0.0 we assume remote display
    // in that case we do not even try to use accel

    // Win32 and Mac/Qt4 don't have DISPLAY
    displ = getenv("DISPLAY");
    if (!displ)
    {
        return RENDER_GTK;
    }
#if 0
        if(strcmp(displ,":0") && strcmp(displ,":0.0"))
        {
                printf("Looks like remote display, no Xv :%s\n",displ);
                return RENDER_GTK;
        }
#endif
#endif

    if (prefs->get(VIDEODEVICE, &renderI) != RC_OK)
    {
        render = RENDER_GTK;
    }
    else
    {
        render = (ADM_RENDER_TYPE)renderI;
    }

    return render;
}

/**
    \fn A_ResetMarkers
*/
void A_ResetMarkers(void)
{
    uint64_t duration = video_body->getVideoDuration();
    ADM_info("Video Total duration : %s ms\n", ADM_us2plain(duration));
    video_body->setMarkerAPts(0);
    video_body->setMarkerBPts(duration);
    UI_setMarkers(0, duration);
}
/**
    \fn A_Rewind
    \brief Go back to the first frame
*/
void A_Rewind(void)
{
    admPreview::stop();
    video_body->rewind();
#if 0 /* Should not be needed anymore after ADM_Composer::DecodePictureUpToIntra has been fixed. */
               video_body->rewind(); // do it twice, for interlaced content it may fail the 1st time
#endif
    admPreview::start();
    admPreview::samePicture();
    // admPreview::samePicture();
    GUI_setCurrentFrameAndTime();
}
void brokenAct(void)
{
    GUI_Error_HIG(QT_TRANSLATE_NOOP("adm", "Oops"),
                  QT_TRANSLATE_NOOP("adm", "This function is disabled or no longer valid"));
}
/**
 * \fn A_TimeShift
 * @return
 */
bool A_TimeShift(void)
{
    static int update = 0;
    int onoff;
    int value;
    if (update)
        return 1; // prevent looping when updating the UI
    update = 1;
    // Read and update
    update = 0;
    UI_getTimeShift(&onoff, &value);
    printf("Shift enabled=%d value=%d\n", onoff, value);
    EditableAudioTrack *ed = video_body->getDefaultEditableAudioTrack();
    if (!ed)
    {
        update = 0;
        return 0;
    }
    ed->audioEncodingConfig.shiftEnabled = onoff;
    ed->audioEncodingConfig.shiftInMs = value;
    update = 0;
    return true;
}

bool avisynthPortAsCommandLineArgument = false;
bool A_getCommandLinePort(uint32_t &port)
{
    if (avisynthPortAsCommandLineArgument)
        prefs->get(AVISYNTH_AVISYNTH_LOCALPORT, &port);
    return avisynthPortAsCommandLineArgument;
}
void A_set_avisynth_port(char *port_number_as_text)
{
    // somehow strtol seems to die with EAGAIN
    int input_length = strlen(port_number_as_text);
    uint32_t portNumber = 0;
    int idx = 0;

    for (; idx < input_length; idx++)
        if (port_number_as_text[idx] <= '9' && port_number_as_text[idx] >= '0')
            portNumber = portNumber * 10 + port_number_as_text[idx] - '0';
        else
        {
            fprintf(stderr, "Invalid character in port number\n");
            fflush(stderr);
            exit(-1);
        }
    if (portNumber < 1024 || portNumber > 65535)
    {
        fprintf(stderr, "Invalid port number! Valid range is [1024, 65535]\n");
        fflush(stderr);
        exit(-1);
    }
    avisynthPortAsCommandLineArgument = true;
    prefs->set(AVISYNTH_AVISYNTH_LOCALPORT, portNumber);
}
/**
 * \fn A_RunScript
 * @param a
 */
void A_RunScript(const char *a)
{
    call_scriptEngine(a);
    A_Resync();
}
/**
 * \fn getDefaultSettingsFilePath
 */
std::string getDefaultSettingsFilePath(void)
{
    static std::string s;
    if (s.size())
        return s;
    s = ADM_getConfigBaseDir();
    s += "defaultSettings.py";
    return s;
}
/**
 * \fn getLastSessionFilePath
 */
std::string getLastSessionFilePath(void)
{
    static std::string s;
    if (s.size())
        return s;
    s = ADM_getBaseDir();
    s += "lastEdit.py";
    return s;
}
/**
 * \fn getCrashRecoveryFilePath
 */
std::string getCrashRecoveryFilePath(void)
{
    static std::string s;
    if (s.size())
        return s;
    s = ADM_getBaseDir();
    s += "crash.py";
    return s;
}

// EOF
