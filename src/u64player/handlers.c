/* Ultimate64 SID Player - remaining APP_* event handlers
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/asl.h>
#include <libraries/mui.h>
#include <workbench/workbench.h>

#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "player.h"
#include "file_utils.h"
#include "string_utils.h"
#include "md5set.h"

void APP_UpdateStatus(CONST_STRPTR text)
{
    if (objApp && objApp->TXT_Status) {
        set(objApp->TXT_Status, MUIA_Text_Contents, text);
    }
}

/* Toggle the "favourite" flag on the most relevant playlist entry:
 *   1. the currently-playing track if there is one,
 *   2. otherwise the currently-selected row in the playlist,
 *   3. otherwise no-op (empty playlist).
 * Keeps the persistent favourites set in sync so the flag survives restart. */
BOOL APP_ToggleFavourite(void)
{
    if (!objApp || !objApp->favourites) return FALSE;

    PlaylistEntry *target = objApp->current_entry;
    if (!target) {
        /* Fall back to the MUI-selected row. */
        LONG active = -1;
        get(objApp->LSV_PlaylistList, MUIA_List_Active, &active);
        if (active < 0) return FALSE;

        PlaylistEntry *e = objApp->playlist_head;
        LONG i = 0;
        while (e && i < active) { e = e->next; i++; }
        target = e;
    }
    if (!target) return FALSE;

    target->is_favourite = !target->is_favourite;
    if (target->is_favourite) {
        MD5Set_Insert(objApp->favourites, target->md5);
        APP_UpdateStatus("Added to favourites");
    } else {
        MD5Set_Remove(objApp->favourites, target->md5);
        APP_UpdateStatus("Removed from favourites");
    }

    APP_UpdatePlaylistDisplay();
    return TRUE;
}

BOOL APP_GetSIDConfig(char *sid1_info, char *sid2_info, size_t buffer_size)
{
    // Clear output buffers
    sid1_info[0] = '\0';
    sid2_info[0] = '\0';

    // Local variables to avoid corruption
    char sid1_type[128] = "";
    char sid2_type[128] = "";
    char sid1_addr[128] = "";
    char sid2_addr[128] = "";

    if (!objApp || !objApp->connection) {
        U64_DEBUG("APP_GetSIDConfig: No connection");
        snprintf(sid1_info, buffer_size, "SID #1: Not connected");
        snprintf(sid2_info, buffer_size, "SID #2: Not connected");
        return FALSE;
    }

    U64_DEBUG("=== Getting SID Configuration ===");

    // STEP 1: Get SID chip types from "SID Sockets Configuration"
    {
        U64ConfigItem *items = NULL;
        ULONG item_count = 0;
        LONG result;

        U64_DEBUG("STEP 1: Requesting SID Sockets Configuration...");
        result = U64_GetConfigCategory(objApp->connection, "SID Sockets Configuration", &items, &item_count);
        U64_DEBUG("SID Sockets Configuration result: %ld, items: %p, count: %lu",
                  result, items, (unsigned long)item_count);

        if (result == U64_OK && items && item_count > 0) {
            for (ULONG i = 0; i < item_count; i++) {
                if (items[i].name && items[i].value.current_str) {
                    U64_DEBUG("SID Sockets item %lu: name='%s' value='%s'",
                              (unsigned long)i, items[i].name, items[i].value.current_str);

                    // Use exact string comparison for the detected socket keys
                    if (strcmp(items[i].name, "SID Detected Socket 1") == 0) {
                        strncpy(sid1_type, items[i].value.current_str, sizeof(sid1_type) - 1);
                        sid1_type[sizeof(sid1_type) - 1] = '\0';
                        U64_DEBUG("*** FOUND SID1 TYPE: '%s' ***", sid1_type);
                    }
                    else if (strcmp(items[i].name, "SID Detected Socket 2") == 0) {
                        strncpy(sid2_type, items[i].value.current_str, sizeof(sid2_type) - 1);
                        sid2_type[sizeof(sid2_type) - 1] = '\0';
                        U64_DEBUG("*** FOUND SID2 TYPE: '%s' ***", sid2_type);
                    }
                    // Ignore the "SID Socket X" keys (without "Detected") as they just show "Enabled"
                }
            }
            U64_FreeConfigItems(items, item_count);
        } else {
            U64_DEBUG("STEP 1 FAILED: result=%ld", result);
        }

        U64_DEBUG("After STEP 1: sid1_type='%s', sid2_type='%s'", sid1_type, sid2_type);
    }

    // STEP 2: Get SID addresses from "SID Addressing"
    {
        U64ConfigItem *items = NULL;
        ULONG item_count = 0;
        LONG result;

        U64_DEBUG("STEP 2: Requesting SID Addressing configuration...");
        result = U64_GetConfigCategory(objApp->connection, "SID Addressing", &items, &item_count);
        U64_DEBUG("SID Addressing result: %ld, items: %p, count: %lu",
                  result, items, (unsigned long)item_count);

        if (result == U64_OK && items && item_count > 0) {
            for (ULONG i = 0; i < item_count; i++) {
                if (items[i].name && items[i].value.current_str) {
                    U64_DEBUG("SID Addressing item %lu: name='%s' value='%s'",
                              (unsigned long)i, items[i].name, items[i].value.current_str);

                    // Use exact string matching for address keys
                    if (strcmp(items[i].name, "SID Socket 1 Address") == 0) {
                        strncpy(sid1_addr, items[i].value.current_str, sizeof(sid1_addr) - 1);
                        sid1_addr[sizeof(sid1_addr) - 1] = '\0';
                        U64_DEBUG("*** FOUND SID1 ADDR: '%s' ***", sid1_addr);
                    }
                    else if (strcmp(items[i].name, "SID Socket 2 Address") == 0) {
                        strncpy(sid2_addr, items[i].value.current_str, sizeof(sid2_addr) - 1);
                        sid2_addr[sizeof(sid2_addr) - 1] = '\0';
                        U64_DEBUG("*** FOUND SID2 ADDR: '%s' ***", sid2_addr);
                    }
                }
            }
            U64_FreeConfigItems(items, item_count);
        } else {
            U64_DEBUG("STEP 2 FAILED: result=%ld, using defaults", result);
            strcpy(sid1_addr, "$D400");
            strcpy(sid2_addr, "$D420");
        }

        U64_DEBUG("After STEP 2: sid1_addr='%s', sid2_addr='%s'", sid1_addr, sid2_addr);
    }

    U64_DEBUG("=== FINAL RESULTS ===");
    U64_DEBUG("SID1: type='%s', addr='%s'", sid1_type, sid1_addr);
    U64_DEBUG("SID2: type='%s', addr='%s'", sid2_type, sid2_addr);

    // Build display strings
    if (strlen(sid1_type) > 0 && strcmp(sid1_type, "None") != 0 && strcmp(sid1_type, "none") != 0) {
        if (strlen(sid1_addr) > 0) {
            snprintf(sid1_info, buffer_size, "SID #1: %s @ %s", sid1_type, sid1_addr);
        } else {
            snprintf(sid1_info, buffer_size, "SID #1: %s @ $D400", sid1_type); // default address
        }
    } else {
        if (strlen(sid1_addr) > 0) {
            snprintf(sid1_info, buffer_size, "SID #1: Unknown @ %s", sid1_addr);
        } else {
            snprintf(sid1_info, buffer_size, "SID #1: Ultimate II");
        }
    }

    if (strlen(sid2_type) > 0 && strcmp(sid2_type, "None") != 0 && strcmp(sid2_type, "none") != 0) {
        if (strlen(sid2_addr) > 0) {
            snprintf(sid2_info, buffer_size, "SID #2: %s @ %s", sid2_type, sid2_addr);
        } else {
            snprintf(sid2_info, buffer_size, "SID #2: %s @ $D420", sid2_type); // default address
        }
    } else {
        if (strlen(sid2_addr) > 0) {
            snprintf(sid2_info, buffer_size, "SID #2: Unknown @ %s", sid2_addr);
        } else {
            snprintf(sid2_info, buffer_size, "SID #2: Ultimate II");
        }
    }

    U64_DEBUG("=== DISPLAY STRINGS ===");
    U64_DEBUG("sid1_info: '%s'", sid1_info);
    U64_DEBUG("sid2_info: '%s'", sid2_info);

    return TRUE;
}

/* Function to update SID configuration display */
void APP_UpdateSIDConfigDisplay(void)
{
    char sid1_info[64];
    char sid2_info[64];

    if (!objApp || !objApp->TXT_SID1_Info || !objApp->TXT_SID2_Info) {
        return;
    }

    if (objApp->connection && APP_GetSIDConfig(sid1_info, sid2_info, sizeof(sid1_info))) {
        set(objApp->TXT_SID1_Info, MUIA_Text_Contents, sid1_info);
        set(objApp->TXT_SID2_Info, MUIA_Text_Contents, sid2_info);
    } else {
        set(objApp->TXT_SID1_Info, MUIA_Text_Contents, "SID #1: Not connected");
        set(objApp->TXT_SID2_Info, MUIA_Text_Contents, "SID #2: Not connected");
    }
}

/* Application functions implementation */
BOOL APP_Init(void)
{
    if (!U64_InitLibrary()) {
        U64_DEBUG("Failed to initialize Ultimate64 library");
        return FALSE;
    }

    /* NEW: Initialize timer device */
    if (!StartTimerDevice()) {
        U64_DEBUG("Failed to initialize timer device - continuing without timer");
    }
    #ifdef DEBUG_BUILD
    U64_SetVerboseMode(TRUE);
    #endif
    LoadConfig(objApp);
    srand(time(NULL));

    return TRUE;
}

BOOL APP_Connect(void)
{
    if (!objApp) return FALSE;

    if (objApp->connection) {
        /* Disconnect */
        if (objApp->state == PLAYER_PLAYING) {
            objApp->state = PLAYER_STOPPED;
        }

        U64_Disconnect(objApp->connection);
        objApp->connection = NULL;

        set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Disconnected");
        set(objApp->BTN_Connect, MUIA_Text_Contents, "Connect");
        APP_UpdateStatus("Disconnected");

        APP_UpdateSIDConfigDisplay();

        /* Disable controls */
        set(objApp->BTN_Play, MUIA_Disabled, TRUE);
        set(objApp->BTN_Stop, MUIA_Disabled, TRUE);
        set(objApp->BTN_Next, MUIA_Disabled, TRUE);
        set(objApp->BTN_Prev, MUIA_Disabled, TRUE);
    } else {
        /* Check if configuration is complete */
        if (strlen(objApp->host) == 0) {
            APP_UpdateStatus("Please configure connection settings first");
            APP_ConfigOpen();
            return FALSE;
        }

        /* Connect */
        objApp->connection = U64_Connect(objApp->host,
            strlen(objApp->password) > 0 ? objApp->password : NULL);

        if (objApp->connection) {
            char status[256];
            sprintf(status, "Connected to %s", objApp->host);
            set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Connected");
            set(objApp->BTN_Connect, MUIA_Text_Contents, "Disconnect");
            APP_UpdateStatus(status);

            APP_UpdateSIDConfigDisplay();

            /* Enable controls */
            set(objApp->BTN_Play, MUIA_Disabled, FALSE);
            set(objApp->BTN_Stop, MUIA_Disabled, FALSE);
            set(objApp->BTN_Next, MUIA_Disabled, FALSE);
            set(objApp->BTN_Prev, MUIA_Disabled, FALSE);
        } else {
            set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Disconnected");
            APP_UpdateStatus("Connection failed");

            APP_UpdateSIDConfigDisplay();
        }
    }

    return TRUE;
}

BOOL APP_AddFiles(void)
{
    struct FileRequester *req = NULL;
    BOOL success = FALSE;
    ULONG added = 0;
    BOOL use_remembered_dir = FALSE;

    if (!AslBase) {
        APP_UpdateStatus("ASL library not available");
        return FALSE;
    }

    /* Check remembered directory */
    if (strlen(objApp->last_sid_dir) > 0) {
        BPTR lock = Lock(objApp->last_sid_dir, ACCESS_READ);
        if (lock) {
            UnLock(lock);
            use_remembered_dir = TRUE;
        } else {
            objApp->last_sid_dir[0] = '\0';
        }
    }

    /* Create ASL requester */
    if (use_remembered_dir) {
        req = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_TitleText, "Select SID files to add",
            ASLFR_DoPatterns, TRUE,
            ASLFR_InitialPattern, "#?.sid",
            ASLFR_DoMultiSelect, TRUE,
            ASLFR_InitialDrawer, objApp->last_sid_dir,
            ASLFR_RejectIcons, TRUE,
            TAG_DONE);
    } else {
        req = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_TitleText, "Select SID files to add",
            ASLFR_DoPatterns, TRUE,
            ASLFR_InitialPattern, "#?.sid",
            ASLFR_DoMultiSelect, TRUE,
            ASLFR_RejectIcons, TRUE,
            TAG_DONE);
    }

    if (!req) {
        APP_UpdateStatus("Failed to create file requester");
        return FALSE;
    }

    /* Show ASL requester */
    success = AslRequest(req, NULL);

    /* Process results */
    if (success) {
        struct WBArg *args = req->rf_ArgList;
        LONG num_args = req->rf_NumArgs;

        if (args && num_args > 0 && num_args <= 1000) {
            /* Remember directory */
            if (req->rf_Dir && strlen(req->rf_Dir) > 0) {
                strncpy(objApp->last_sid_dir, req->rf_Dir, sizeof(objApp->last_sid_dir) - 1);
                objApp->last_sid_dir[sizeof(objApp->last_sid_dir) - 1] = '\0';
            }

            for (LONG i = 0; i < num_args; i++) {
                char filename[512];

                if (args[i].wa_Lock && args[i].wa_Name) {
                    if (NameFromLock(args[i].wa_Lock, filename, sizeof(filename) - 32)) {
                        if (AddPart(filename, args[i].wa_Name, sizeof(filename))) {
                            if (AddPlaylistEntry(objApp, filename)) {
                                added++;
                            }
                        }
                    }
                }

                if ((i % 10) == 0) {
                    Delay(1);
                }
            }
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    /* Update displays */
    if (success && added > 0) {
        APP_UpdatePlaylistDisplay();

        if (!objApp->current_entry && objApp->playlist_head) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
            objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5,
                                                             objApp->current_entry->current_subsong);
            if (objApp->current_entry->duration == 0) {
                objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
            }
            APP_UpdateCurrentSongCache();
            APP_UpdateCurrentSongDisplay();
        }

        char status_msg[256];
        sprintf(status_msg, "Added %lu files to playlist", (unsigned int)added);
        APP_UpdateStatus(status_msg);
    } else if (success) {
        APP_UpdateStatus("No valid SID files found");
    } else {
        APP_UpdateStatus("File selection cancelled");
    }

    return success;
}

BOOL APP_RemoveFile(void)
{
    LONG active;

    if (!objApp) return FALSE;

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &active);

    if (active == MUIV_List_Active_Off || active < 0) {
        APP_UpdateStatus("No file selected");
        return FALSE;
    }

    /* Find entry to remove */
    PlaylistEntry *entry = objApp->playlist_head;
    PlaylistEntry *prev = NULL;

    for (ULONG i = 0; i < (ULONG)active && entry; i++) {
        prev = entry;
        entry = entry->next;
    }

    if (!entry) {
        return FALSE;
    }

    /* Stop playback if removing current song */
    if (entry == objApp->current_entry) {
        if (objApp->state == PLAYER_PLAYING) {
            objApp->state = PLAYER_STOPPED;
        }
        objApp->current_entry = entry->next;
        if (!objApp->current_entry && objApp->playlist_head != entry) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
        }
    }

    /* Remove from list */
    if (prev) {
        prev->next = entry->next;
    } else {
        objApp->playlist_head = entry->next;
    }

    /* Adjust current index */
    if (objApp->current_index > (ULONG)active) {
        objApp->current_index--;
    }

    /* Free entry */
    U64_SafeStrFree(entry->filename);
    U64_SafeStrFree(entry->title);
    FreeVec(entry);

    objApp->playlist_count--;

    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();
    APP_UpdateStatus("File removed from playlist");

    return TRUE;
}

BOOL APP_About(void)
{
    if (!objApp) return FALSE;

    MUI_RequestA(objApp->App,
                objApp->WIN_Main,
                0,
                NULL,
                "Continue",
                "\33c\0338Ultimate64 SID Player v1.0\n\n"
                "\0332Playlist SID Player for Ultimate64\n\n"
                "2025 Marcin Spoczynski",
                NULL);
    return TRUE;
}

BOOL APP_ConfigOpen(void)
{
    if (!objApp || !objApp->WIN_Config) return FALSE;

    /* Update string gadgets with current values */
    set(objApp->STR_ConfigHost, MUIA_String_Contents, objApp->host);
    set(objApp->STR_ConfigPassword, MUIA_String_Contents, objApp->password);

    /* Open the window */
    set(objApp->WIN_Config, MUIA_Window_Open, TRUE);
    return TRUE;
}

BOOL APP_ConfigOK(void)
{
    STRPTR host_str, password_str;

    if (!objApp || !objApp->WIN_Config) return FALSE;

    /* Get values from string gadgets */
    get(objApp->STR_ConfigHost, MUIA_String_Contents, &host_str);
    get(objApp->STR_ConfigPassword, MUIA_String_Contents, &password_str);

    /* Copy values */
    if (host_str) {
        strncpy(objApp->host, host_str, sizeof(objApp->host) - 1);
        objApp->host[sizeof(objApp->host) - 1] = '\0';
    }

    if (password_str) {
        strncpy(objApp->password, password_str, sizeof(objApp->password) - 1);
        objApp->password[sizeof(objApp->password) - 1] = '\0';
    }

    /* Save configuration */
    SaveConfig(objApp);

    /* Close window */
    set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
    APP_UpdateStatus("Configuration saved");

    return TRUE;
}

BOOL APP_ConfigCancel(void)
{
    if (!objApp || !objApp->WIN_Config) return FALSE;

    set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
    return TRUE;
}
