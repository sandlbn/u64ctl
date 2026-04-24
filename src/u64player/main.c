/* Ultimate64 SID Player - entry point, library init, app lifecycle
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/tasks.h>
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
#include "md5set.h"

/* File names for persistent user state — all live in the program directory. */
#define HEARD_FILENAME      "heard.txt"
#define FAVOURITES_FILENAME "favourites.txt"
#define SESSION_FILENAME    "session.u64pl"

/* Build a path "PROGDIR/<name>" into out. Returns TRUE on success. */
static BOOL
build_progdir_path(char *out, ULONG out_size, CONST_STRPTR name)
{
    BPTR lock = GetProgramDir();
    char progdir[256];
    if (!lock || !NameFromLock(lock, progdir, sizeof(progdir))) return FALSE;
    strncpy(out, progdir, out_size - 1);
    out[out_size - 1] = '\0';
    AddPart(out, (STRPTR)name, out_size);
    return TRUE;
}

/* Guaranteed stack size (bytes). MUI + SID-playback + song-length-DB load
 * can burn through ~10-12KB; we request 48 KB headroom. StackSwap onto a
 * private buffer if the caller's stack is smaller. */
#define REQUIRED_STACK 49152

static struct StackSwapStruct g_sss;
static UBYTE *g_new_stack = NULL;

static int AppMain(void);

/* Version string */
static const char version[] = "$VER: u64player 0.4.0 (2025)";

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
        StopTimerDevice();

        /* Persist user state before freeing playlist data. */
        {
            char path[512];
            if (obj->heard_db
                && build_progdir_path(path, sizeof(path), HEARD_FILENAME)) {
                MD5Set_Save(obj->heard_db, path);
            }
            if (obj->favourites
                && build_progdir_path(path, sizeof(path), FAVOURITES_FILENAME)) {
                MD5Set_Save(obj->favourites, path);
            }
            /* Session restore: snapshot the current playlist. If it's empty,
             * delete any stale saved session so we don't restore a ghost. */
            if (build_progdir_path(path, sizeof(path), SESSION_FILENAME)) {
                if (obj->playlist_count > 0) {
                    SavePlaylistToFile(obj, path);
                } else {
                    DeleteFile(path);
                }
            }
        }

        MD5Set_Free(obj->heard_db);     obj->heard_db = NULL;
        MD5Set_Free(obj->favourites);   obj->favourites = NULL;

        FreePlaylists(obj);
        FreeSongLengthDB(obj);

        if (obj->connection) {
            U64_Disconnect(obj->connection);
            obj->connection = NULL;
        }

        if (obj->App) {
            MUI_DisposeObject(obj->App);
        }

        FreeVec(obj);
    }
}

/* Main function — swaps to a private stack if caller's stack is too small,
 * then runs AppMain() and swaps back. */
int main(void)
{
    struct Process *proc = (struct Process *)FindTask(NULL);
    ULONG current_stack =
        (ULONG)proc->pr_Task.tc_SPUpper - (ULONG)proc->pr_Task.tc_SPLower;
    int retval;

    if (current_stack >= REQUIRED_STACK)
        return AppMain();

    g_new_stack = AllocMem(REQUIRED_STACK, MEMF_PUBLIC);
    if (!g_new_stack) {
        printf("Out of memory for stack\n");
        return 20;
    }

    g_sss.stk_Lower = g_new_stack;
    g_sss.stk_Upper = (ULONG)(g_new_stack + REQUIRED_STACK);
    g_sss.stk_Pointer = (APTR)(g_new_stack + REQUIRED_STACK);

    StackSwap(&g_sss);
    retval = AppMain();
    StackSwap(&g_sss);

    FreeMem(g_new_stack, REQUIRED_STACK);
    g_new_stack = NULL;
    return retval;
}

static int AppMain(void)
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

        /* Load persistent user sets (heard + favourites). Absence of the
         * files just yields empty sets, which is fine. */
        objApp->heard_db = MD5Set_Create();
        objApp->favourites = MD5Set_Create();
        {
            char path[512];
            if (build_progdir_path(path, sizeof(path), HEARD_FILENAME))
                MD5Set_Load(objApp->heard_db, path);
            if (build_progdir_path(path, sizeof(path), FAVOURITES_FILENAME))
                MD5Set_Load(objApp->favourites, path);

            /* Session restore: if a prior session was saved, reload it now
             * (before AutoLoadSongLengths so its playlist-refresh loop can
             * populate durations). */
            if (build_progdir_path(path, sizeof(path), SESSION_FILENAME)) {
                BPTR test = Open(path, MODE_OLDFILE);
                if (test) {
                    Close(test);
                    U64_DEBUG("restoring session playlist from %s", path);
                    LoadPlaylistFromFile(objApp, path);
                }
            }
        }

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

        /* After the DB is ready, report "X of Y SIDs heard" as a one-shot
         * status so the user sees their HVSC completion progress. */
        if (objApp->heard_db && objApp->heard_db->count > 0) {
            char msg[160];
            ULONG total = objApp->songlength_db
                ? objApp->songlength_db->entry_count : 0;
            if (total > 0) {
                /* avoid float — percent * 100, scaled integer maths */
                ULONG scaled = (objApp->heard_db->count * 10000UL) / total;
                sprintf(msg, "%lu of %lu HVSC SIDs heard (%lu.%02lu%%)",
                        (unsigned long)objApp->heard_db->count,
                        (unsigned long)total,
                        (unsigned long)(scaled / 100),
                        (unsigned long)(scaled % 100));
            } else {
                sprintf(msg, "%lu unique SIDs heard",
                        (unsigned long)objApp->heard_db->count);
            }
            APP_UpdateStatus(msg);
        }

        /* If the user clicked Quit while the songlengths parser was running,
         * the parser flags it on objApp and bails; honour it here so the
         * event loop doesn't wait forever for a signal that already fired. */
        if (objApp->quit_requested) {
            running = FALSE;
        }

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
                    case EVENT_TOGGLE_FAVOURITE: APP_ToggleFavourite(); break;
                    case MUIV_Application_ReturnID_Quit: running = FALSE; break;
                }
            }

            if (id == MUIV_Application_ReturnID_Quit) {
                running = FALSE;
                break;
            }

            // Build wait signal mask - include timer if running
            ULONG waitSignals = signals | SIGBREAKF_CTRL_C | TimerWaitMask();

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
        Delay(5);

        result = RETURN_OK;
    }

    U64_CleanupLibrary();
    CleanupLibs();

    return result;
}
