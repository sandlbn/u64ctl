/* Ultimate64 SID Player - entry point, library init, app lifecycle
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/mui.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "player.h"

/* Version string */
static const char version[] = "$VER: u64sidplayer 0.3.2 (2025)";

/* Global library bases */
struct Library *MUIMasterBase = NULL;
struct Library *AslBase = NULL;

/* Global application instance */
struct ObjApp *objApp = NULL;

/* Library initialization */
BOOL InitLibs(void)
{
    if (!(MUIMasterBase = OpenLibrary(MUIMASTER_NAME, 19))) {
        return FALSE;
    }

    if (!(AslBase = OpenLibrary("asl.library", 37))) {
        CloseLibrary(MUIMasterBase);
        return FALSE;
    }

    return TRUE;
}

void CleanupLibs(void)
{
    if (AslBase) CloseLibrary(AslBase);
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
}

/* Create application */
struct ObjApp *CreateApp(void)
{
    struct ObjApp *obj;

    U64_DEBUG("Creating App...");

    if ((obj = AllocVec(sizeof(struct ObjApp), MEMF_PUBLIC | MEMF_CLEAR))) {
        U64_DEBUG("Memory allocated");

        /* Create windows */
        CreateMenu(obj);
        CreateWindowMain(obj);
        CreateWindowConfig(obj);

        if (obj->WIN_Main && obj->WIN_Config) {
            /* Create Application object */
            obj->App = MUI_NewObject(MUIC_Application,
                MUIA_Application_Title, "Ultimate64 SID Player",
                MUIA_Application_Version, version,
                MUIA_Application_Copyright, "2025",
                MUIA_Application_Author, "Marcin Spoczynski",
                MUIA_Application_Description, "SID Player with Playlist for Ultimate64",
                MUIA_Application_Base, "U64SIDPLAYER",
                MUIA_Application_Menustrip, obj->MN_Main,
                SubWindow, obj->WIN_Main,
                SubWindow, obj->WIN_Config,
                TAG_DONE);

            if (obj->App) {
                U64_DEBUG("Application object created");

                /* Create events */
                CreateMenuEvents(obj);
                CreateWindowMainEvents(obj);
                CreateWindowConfigEvents(obj);
                /* Initialize search data */
                obj->search_text[0] = '\0';
                obj->search_mode_filter = FALSE; /* Default to search mode */
                obj->playlist_visible = NULL;
                obj->search_current_match = 0;
                obj->search_total_matches = 0;
                U64_DEBUG("App creation complete");
                return obj;
            }
        }

        /* Cleanup on failure */
        FreeVec(obj);
    }

    return NULL;
}

void DisposeApp(struct ObjApp *obj)
{
    if (obj) {
        /* NEW: Stop timer device */
        StopTimerDevice();

        FreePlaylists(obj);
        FreeSongLengthDB(obj);

        if (obj->connection) {
            U64_Disconnect(obj->connection);
        }

        if (obj->App) {
            MUI_DisposeObject(obj->App);
        }

        FreeVec(obj);
    }
}

/* Main function */
int main(void)
{
    int result = RETURN_FAIL;
    BOOL running = TRUE;
    ULONG signals = 0;

    if (!InitLibs()) {
        return 20;
    }

    if ((objApp = CreateApp())) {
        if (!APP_Init()) {
            U64_DEBUG("Failed to initialize application");
            DisposeApp(objApp);
            CleanupLibs();
            return RETURN_FAIL;
        }

        /* Initialize current_playlist_file */
        objApp->current_playlist_file[0] = '\0';

        LoadConfig(objApp);

        set(objApp->BTN_Play, MUIA_Disabled, TRUE);
        set(objApp->BTN_Stop, MUIA_Disabled, TRUE);
        set(objApp->BTN_Next, MUIA_Disabled, TRUE);
        set(objApp->BTN_Prev, MUIA_Disabled, TRUE);

        DoMethod(objApp->WIN_Main, MUIM_Set, MUIA_Window_Open, TRUE);

        ULONG isOpen = FALSE;
        get(objApp->WIN_Main, MUIA_Window_Open, &isOpen);
        if (!isOpen) {
            running = FALSE;
        } else {
            APP_UpdateStatus("Ready - Configure connection to get started");
        }

        AutoLoadSongLengths(objApp);

        while (running) {
           signals = 0;
            ULONG id = DoMethod(objApp->App, MUIM_Application_NewInput, &signals);


            if (id > 0) {
                switch (id) {
                    case EVENT_CONNECT: APP_Connect(); break;
                    case EVENT_ADD_FILE: APP_AddFiles(); break;
                    case EVENT_REMOVE_FILE: APP_RemoveFile(); break;
                    case EVENT_CLEAR_PLAYLIST: APP_ClearPlaylist(); break;
                    case EVENT_PLAY: APP_Play(); break;
                    case EVENT_STOP: APP_Stop(); break;
                    case EVENT_NEXT: APP_Next(); break;
                    case EVENT_PREV: APP_Prev(); break;
                    case EVENT_SHUFFLE:
                        get(objApp->CHK_Shuffle, MUIA_Selected, &objApp->shuffle_mode);
                        APP_UpdateStatus(objApp->shuffle_mode ? "Shuffle enabled" : "Shuffle disabled");
                        break;
                    case EVENT_REPEAT:
                        get(objApp->CHK_Repeat, MUIA_Selected, &objApp->repeat_mode);
                        APP_UpdateStatus(objApp->repeat_mode ? "Repeat enabled" : "Repeat disabled");
                        break;
                    case EVENT_DOWNLOAD_SONGLENGTHS: APP_DownloadSongLengths(); break;
                    case EVENT_ABOUT: APP_About(); break;
                    case EVENT_CONFIG_OPEN: APP_ConfigOpen(); break;
                    case EVENT_CONFIG_OK: APP_ConfigOK(); break;
                    case EVENT_CONFIG_CANCEL: APP_ConfigCancel(); break;
                    case EVENT_PLAYLIST_ACTIVE: APP_PlaylistActive(); break;
                    case EVENT_PLAYLIST_DCLICK: APP_PlaylistDoubleClick(); break;
                    case EVENT_PLAYLIST_LOAD: APP_PlaylistLoad(); break;
                    case EVENT_PLAYLIST_SAVE: APP_PlaylistSave(); break;
                    case EVENT_PLAYLIST_SAVE_AS: APP_PlaylistSaveAs(); break;
                    case EVENT_SEARCH_TEXT: APP_SearchTextChanged(); break;
                    case EVENT_SEARCH_MODE_CHANGED: APP_SearchModeChanged(); break;
                    case EVENT_SEARCH_CLEAR: APP_SearchClear(); break;
                    case EVENT_SEARCH_NEXT: APP_SearchNext(); break;
                    case EVENT_SEARCH_PREV: APP_SearchPrev(); break;
                    case MUIV_Application_ReturnID_Quit: running = FALSE; break;
                }
            }

            if (id == MUIV_Application_ReturnID_Quit) {
                running = FALSE;
                break;
            }

            // Build wait signal mask - include timer if running
            ULONG waitSignals = signals | SIGBREAKF_CTRL_C;
            if (TimerRunning && TimerSig) {
                waitSignals |= TimerSig;
            }

            // Only wait if we have signals to wait for
            if (waitSignals & ~SIGBREAKF_CTRL_C) {  // If more than just CTRL_C
                ULONG receivedSignals = Wait(waitSignals);

                if (receivedSignals & SIGBREAKF_CTRL_C) {
                    running = FALSE;
                    break;
                }
                CheckTimerSignal(receivedSignals);
            }
        }

        set(objApp->WIN_Main, MUIA_Window_Open, FALSE);
        if (objApp->WIN_Config) {
            set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
        }

        SaveConfig(objApp);
        DisposeApp(objApp);
        result = RETURN_OK;
    }

    U64_CleanupLibrary();
    CleanupLibs();

    return result;
}
