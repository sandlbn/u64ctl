/* Ultimate64 SID Player - playlist management
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

#include "player.h"
#include "file_utils.h"
#include "string_utils.h"
#include "md5set.h"

/* Simple ULONG -> string helper (local to this module) */
static void
ULONGToString(ULONG value, char *buffer)
{
    int pos = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    char digits[16];
    int digit_pos = 0;

    while (value > 0) {
        digits[digit_pos++] = '0' + (value % 10);
        value /= 10;
    }

    /* Reverse digits */
    for (int i = digit_pos - 1; i >= 0; i--) {
        buffer[pos++] = digits[i];
    }
    buffer[pos] = '\0';
}

BOOL SavePlaylistToFile(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    PlaylistEntry *entry;
    char line[1024];
    char md5_str[MD5_STRING_SIZE];
    STRPTR escaped_filename, escaped_title;
    ULONG count = 0;

    file = Open(filename, MODE_NEWFILE);
    if (!file) {
        APP_UpdateStatus("Failed to create playlist file");
        return FALSE;
    }

    Write(file, "# Ultimate64 SID Player Playlist\n", 33);
    Write(file, "# Format: \"filename\" \"title\" md5hash subsongs current_subsong\n", 61);
    Write(file, "\n", 1);

    entry = obj->playlist_head;
    while (entry) {
        count++;

        if ((count % 10) == 0) {
            static char progress_msg[256];
            strcpy(progress_msg, "Saving playlist... ");

            char count_str[32];
            ULONGToString(count, count_str);
            strcat(progress_msg, count_str);

            APP_UpdateStatus(progress_msg);

            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        escaped_filename = U64_EscapeString(entry->filename);
        escaped_title = U64_EscapeString(entry->title);

        MD5ToHexString(entry->md5, md5_str);

        strcpy(line, "\"");
        if (escaped_filename) {
            strcat(line, escaped_filename);
        }
        strcat(line, "\" \"");
        if (escaped_title) {
            strcat(line, escaped_title);
        }
        strcat(line, "\" ");
        strcat(line, md5_str);
        strcat(line, " ");

        char subsongs_str[16];
        ULONGToString(entry->subsongs, subsongs_str);
        strcat(line, subsongs_str);
        strcat(line, " ");

        char current_str[16];
        ULONGToString(entry->current_subsong, current_str);
        strcat(line, current_str);
        strcat(line, "\n");

        Write(file, line, strlen(line));

        if (escaped_filename) FreeVec(escaped_filename);
        if (escaped_title) FreeVec(escaped_title);

        entry = entry->next;
    }

    Close(file);

    static char status_msg[256];
    strcpy(status_msg, "Saved ");

    char count_str[32];
    ULONGToString(count, count_str);
    strcat(status_msg, count_str);
    strcat(status_msg, " entries to playlist");

    APP_UpdateStatus(status_msg);

    return TRUE;
}

BOOL LoadPlaylistFromFile(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    char line[1024];
    ULONG count = 0;
    ULONG line_number = 0;

    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        APP_UpdateStatus("Failed to open playlist file");
        return FALSE;
    }

    FreePlaylists(obj);
    APP_UpdateStatus("Loading playlist...");

    while (FGets(file, line, sizeof(line))) {
        char *filename_start, *filename_end;
        char *title_start, *title_end;
        char *md5_start, *md5_end;
        char *subsongs_start, *current_start;
        PlaylistEntry *entry;
        char *newline;
        STRPTR unescaped_filename, unescaped_title;

        line_number++;

        if ((line_number % 25) == 0) {
            static char progress_msg[256];
            strcpy(progress_msg, "Loading... line ");

            char line_str[32];
            ULONGToString(line_number, line_str);
            strcat(progress_msg, line_str);

            APP_UpdateStatus(progress_msg);

            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        filename_start = strchr(line, '"');
        if (!filename_start) continue;
        filename_start++;

        filename_end = filename_start;
        while (*filename_end && *filename_end != '"') {
            if (*filename_end == '\\' && *(filename_end + 1)) {
                filename_end += 2;
            } else {
                filename_end++;
            }
        }
        if (*filename_end != '"') continue;
        *filename_end = '\0';

        title_start = strchr(filename_end + 1, '"');
        if (!title_start) {
            *filename_end = '"';
            continue;
        }
        title_start++;

        title_end = title_start;
        while (*title_end && *title_end != '"') {
            if (*title_end == '\\' && *(title_end + 1)) {
                title_end += 2;
            } else {
                title_end++;
            }
        }
        if (*title_end != '"') {
            *filename_end = '"';
            continue;
        }
        *title_end = '\0';

        md5_start = title_end + 1;
        while (*md5_start && (*md5_start == ' ' || *md5_start == '\t')) {
            md5_start++;
        }

        md5_end = md5_start;
        while (*md5_end && *md5_end != ' ' && *md5_end != '\t') {
            md5_end++;
        }
        if (md5_end - md5_start != 32) {
            *filename_end = '"';
            *title_end = '"';
            continue;
        }

        subsongs_start = md5_end;
        while (*subsongs_start && (*subsongs_start == ' ' || *subsongs_start == '\t')) {
            subsongs_start++;
        }

        current_start = subsongs_start;
        while (*current_start && *current_start != ' ' && *current_start != '\t') {
            current_start++;
        }
        while (*current_start && (*current_start == ' ' || *current_start == '\t')) {
            current_start++;
        }

        entry = AllocVec(sizeof(PlaylistEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!entry) {
            *filename_end = '"';
            *title_end = '"';
            continue;
        }

        unescaped_filename = U64_UnescapeString(filename_start);
        unescaped_title = U64_UnescapeString(title_start);

        *filename_end = '"';
        *title_end = '"';

        if (!unescaped_filename) {
            if (unescaped_title) FreeVec(unescaped_title);
            FreeVec(entry);
            continue;
        }

        entry->filename = unescaped_filename;
        entry->title = unescaped_title;

        char md5_temp[33];
        CopyMem(md5_start, md5_temp, 32);
        md5_temp[32] = '\0';
        if (!HexStringToMD5(md5_temp, entry->md5)) {
            U64_SafeStrFree(entry->filename);
            U64_SafeStrFree(entry->title);
            FreeVec(entry);
            continue;
        }

        entry->subsongs = (UWORD)atoi(subsongs_start);
        entry->current_subsong = (UWORD)atoi(current_start);

        if (entry->subsongs == 0) entry->subsongs = 1;
        if (entry->current_subsong >= entry->subsongs) {
            entry->current_subsong = 0;
        }

        entry->duration = FindSongLength(obj, entry->md5, entry->current_subsong);
        if (entry->duration == 0) {
            entry->duration = DEFAULT_SONG_LENGTH;
        }

        /* Sync favourite flag from persistent set. */
        entry->is_favourite = MD5Set_Contains(obj->favourites, entry->md5);

        if (!obj->playlist_head) {
            obj->playlist_head = entry;
        } else {
            PlaylistEntry *last = obj->playlist_head;
            while (last->next) {
                last = last->next;
            }
            last->next = entry;
        }

        obj->playlist_count++;
        count++;
    }

    Close(file);

    if (obj->playlist_head && !obj->current_entry) {
        obj->current_entry = obj->playlist_head;
        obj->current_index = 0;
        APP_UpdateCurrentSongCache();
    }

    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();

    static char status_msg[256];
    strcpy(status_msg, "Loaded ");

    char count_str[32];
    ULONGToString(count, count_str);
    strcat(status_msg, count_str);
    strcat(status_msg, " entries from playlist");

    APP_UpdateStatus(status_msg);

    return TRUE;
}

BOOL APP_PlaylistSave(void)
{
    if (!objApp) return FALSE;

    if (strlen(objApp->current_playlist_file) > 0) {
        return SavePlaylistToFile(objApp, objApp->current_playlist_file);
    } else {
        return APP_PlaylistSaveAs();
    }
}

BOOL APP_PlaylistSaveAs(void)
{
    struct FileRequester *req;
    BOOL success = FALSE;

    if (!objApp || !AslBase) return FALSE;

    if (objApp->playlist_count == 0) {
        APP_UpdateStatus("No playlist to save");
        return FALSE;
    }

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, "Save Playlist As",
        ASLFR_DoSaveMode, TRUE,
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, "#?.m3u",
        ASLFR_InitialFile, "playlist.m3u",
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);

    if (req && AslRequest(req, NULL)) {
        char filename[512];
        strcpy(filename, req->rf_Dir);
        AddPart(filename, req->rf_File, sizeof(filename));

        if (!strstr(filename, ".m3u") && !strstr(filename, ".M3U")) {
            strcat(filename, ".m3u");
        }

        if (SavePlaylistToFile(objApp, filename)) {
            strncpy(objApp->current_playlist_file, filename,
                    sizeof(objApp->current_playlist_file) - 1);
            objApp->current_playlist_file[sizeof(objApp->current_playlist_file) - 1] = '\0';
            success = TRUE;
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    return success;
}

BOOL APP_PlaylistLoad(void)
{
    struct FileRequester *req;
    BOOL success = FALSE;

    if (!objApp || !AslBase) return FALSE;

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, "Load Playlist",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, "#?.m3u",
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);

    if (req && AslRequest(req, NULL)) {
        char filename[512];
        strcpy(filename, req->rf_Dir);
        AddPart(filename, req->rf_File, sizeof(filename));

        if (LoadPlaylistFromFile(objApp, filename)) {
            strncpy(objApp->current_playlist_file, filename,
                    sizeof(objApp->current_playlist_file) - 1);
            objApp->current_playlist_file[sizeof(objApp->current_playlist_file) - 1] = '\0';
            success = TRUE;
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    return success;
}

BOOL APP_ClearPlaylist(void)
{
    if (!objApp) return FALSE;

    /* Stop playback */
    if (objApp->state == PLAYER_PLAYING) {
        objApp->state = PLAYER_STOPPED;
    }

    FreePlaylists(objApp);
    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();
    APP_UpdateStatus("Playlist cleared");

    return TRUE;
}

BOOL AddPlaylistEntry(struct ObjApp *obj, CONST_STRPTR filename)
{
    UBYTE *file_data;
    ULONG file_size;
    PlaylistEntry *entry;
    PlaylistEntry *last;

    /* Load file to calculate MD5 and parse header */
    file_data = U64_ReadFile(filename, &file_size);
    if (!file_data) {
        return FALSE;
    }

    /* Create new entry */
    entry = AllocVec(sizeof(PlaylistEntry), MEMF_PUBLIC | MEMF_CLEAR);
    if (!entry) {
        FreeVec(file_data);
        return FALSE;
    }

    /* Copy filename */
    entry->filename = U64_SafeStrDup(filename);
    if (!entry->filename) {
        FreeVec(entry);
        FreeVec(file_data);
        return FALSE;
    }

    /* Calculate MD5 */
    CalculateMD5(file_data, file_size, entry->md5);

    /* Restore favourite flag from persistent set. */
    entry->is_favourite = MD5Set_Contains(obj->favourites, entry->md5);

    /* Parse SID header for basic subsong count */
    UWORD header_subsongs = ParseSIDSubsongs(file_data, file_size);
    entry->subsongs = header_subsongs;
    entry->current_subsong = 0; /* Always start with first subsong */

    /* Check if we have this SID in our songlength database */
    if (obj->songlength_db) {
        SongLengthEntry *db_entry = SongDB_Find(obj->songlength_db, entry->md5);
        if (db_entry) {
            U64_DEBUG("Found %s in database: %d subsongs (header said %d)",
                      FilePart(filename), db_entry->num_subsongs, header_subsongs);

            if (db_entry->num_subsongs > header_subsongs
                && db_entry->num_subsongs <= 256) {
                entry->subsongs = db_entry->num_subsongs;
            }
        }
    }

    /* Extract title */
    entry->title = ExtractSIDTitle(file_data, file_size);
    if (!entry->title) {
        /* Use filename as title */
        STRPTR basename = FilePart(filename);
        entry->title = U64_SafeStrDup(basename);
    }

    /* Find song length for the first subsong (subsong 0) */
    entry->duration = FindSongLength(obj, entry->md5, 0);
    if (entry->duration == 0) {
        entry->duration = DEFAULT_SONG_LENGTH;
    }

    FreeVec(file_data);

    /* Add to playlist */
    if (!obj->playlist_head) {
        obj->playlist_head = entry;
    } else {
        last = obj->playlist_head;
        while (last->next) {
            last = last->next;
        }
        last->next = entry;
    }

    obj->playlist_count++;
    return TRUE;
}

BOOL
APP_PlaylistDoubleClick(void)
{
    LONG active;

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &active);

    U64_DEBUG("=== APP_PlaylistDoubleClick (DOUBLE CLICK) ===");
    U64_DEBUG("Double-clicked index: %ld", active);

    if (active == MUIV_List_Active_Off || active < 0) {
        U64_DEBUG("No valid selection for double-click");
        return FALSE;
    }

    /* Find the entry */
    PlaylistEntry *entry = objApp->playlist_head;
    for (ULONG i = 0; i < (ULONG)active && entry; i++) {
        entry = entry->next;
    }

    if (entry) {
        U64_DEBUG("Double-clicked on: %s", FilePart(entry->filename));

        objApp->current_entry = entry;
        objApp->current_index = (ULONG)active;

        /* Update cache immediately */
        APP_UpdateCurrentSongCache();

        /* ALWAYS start playing on double-click, regardless of current state */
        U64_DEBUG("Starting playback due to double-click");
        if (PlayCurrentSong(objApp)) {
            /* Update playlist display to show current selection */
            APP_UpdatePlaylistDisplay();
            U64_DEBUG("Successfully started playing double-clicked song");
        } else {
            U64_DEBUG("Failed to start playing double-clicked song");
        }
    } else {
        U64_DEBUG("ERROR: Could not find double-clicked entry");
    }

    return TRUE;
}

BOOL
APP_PlaylistActive(void)
{
    LONG index;

    if (!objApp) return FALSE;

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &index);

    U64_DEBUG("=== APP_PlaylistActive (SINGLE CLICK) ===");
    U64_DEBUG("Selected index: %ld", index);

    if (index == MUIV_List_Active_Off || index < 0) {
        U64_DEBUG("No valid selection");
        return FALSE;
    }

    /* Find the selected entry */
    PlaylistEntry *entry = objApp->playlist_head;
    for (ULONG i = 0; i < (ULONG)index && entry; i++) {
        entry = entry->next;
    }

    if (entry) {
        U64_DEBUG("Single-clicked on: %s", FilePart(entry->filename));

        /* ONLY update current entry - do NOT start playing */
        objApp->current_entry = entry;
        objApp->current_index = (ULONG)index;

        /* Update cache and display */
        APP_UpdateCurrentSongCache();
        APP_UpdateCurrentSongDisplay();

        /* Build status message manually - show selection, not playing */
        static char status_msg[256];
        strcpy(status_msg, "Selected: ");

        char *basename = FilePart(entry->filename);
        strcat(status_msg, basename);

        if (entry->subsongs > 1) {
            strcat(status_msg, " [");

            /* Add current subsong (1-based) */
            int display_current = (int)entry->current_subsong + 1;
            if (display_current < 10) {
                char c = '0' + display_current;
                strncat(status_msg, &c, 1);
            } else if (display_current < 100) {
                char temp[3];
                temp[0] = '0' + (display_current / 10);
                temp[1] = '0' + (display_current % 10);
                temp[2] = '\0';
                strcat(status_msg, temp);
            }

            strcat(status_msg, "/");

            /* Add total subsongs */
            int display_total = (int)entry->subsongs;
            if (display_total < 10) {
                char c = '0' + display_total;
                strncat(status_msg, &c, 1);
            } else if (display_total < 100) {
                char temp[3];
                temp[0] = '0' + (display_total / 10);
                temp[1] = '0' + (display_total % 10);
                temp[2] = '\0';
                strcat(status_msg, temp);
            }

            strcat(status_msg, "]");
        }

        APP_UpdateStatus(status_msg);
        U64_DEBUG("Updated selection without playing");
    } else {
        U64_DEBUG("ERROR: Could not find selected entry");
    }

    return TRUE;
}

/* Free Playlists */
void FreePlaylists(struct ObjApp *obj)
{
    PlaylistEntry *entry = obj->playlist_head;
    PlaylistEntry *next;

    /* Clear the MUI list first to prevent access to freed memory */
    if (obj->LSV_PlaylistList) {
        set(obj->LSV_PlaylistList, MUIA_List_Quiet, TRUE);
        DoMethod(obj->LSV_PlaylistList, MUIM_List_Clear);
        set(obj->LSV_PlaylistList, MUIA_List_Quiet, FALSE);
    }

    /* Free playlist entries */
    while (entry) {
        next = entry->next;
        U64_SafeStrFree(entry->filename);
        U64_SafeStrFree(entry->title);
        FreeVec(entry);
        entry = next;
    }

    /* Clean up search data */
    if (obj->playlist_visible) {
        FreeVec(obj->playlist_visible);
        obj->playlist_visible = NULL;
    }

    obj->playlist_head = NULL;
    obj->current_entry = NULL;
    obj->playlist_count = 0;
    obj->current_index = 0;
    obj->search_current_match = 0;
    obj->search_total_matches = 0;
}

void
APP_UpdatePlaylistDisplay(void)
{
    PlaylistEntry *entry;
    ULONG i;

    if (!objApp || !objApp->LSV_PlaylistList) return;

    U64_DEBUG("=== APP_UpdatePlaylistDisplay FIXED START ===");

    /* Clear list first */
    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, TRUE);
    DoMethod(objApp->LSV_PlaylistList, MUIM_List_Clear);

    if (objApp->playlist_count == 0) {
        set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);
        U64_DEBUG("Playlist is empty");
        return;
    }

    /* Add entries one by one to the list */
    entry = objApp->playlist_head;
    for (i = 0; i < objApp->playlist_count && entry; i++) {
        char *basename = FilePart(entry->filename);

        U64_DEBUG("=== PROCESSING ENTRY %d ===", (int)i);
        U64_DEBUG("Entry address: %p", entry);
        U64_DEBUG("Filename: %s", basename);

        /* Read values safely into local int variables */
        int entry_subsongs = (int)entry->subsongs;
        int entry_current = (int)entry->current_subsong;
        int entry_duration = (int)entry->duration;

        U64_DEBUG("Raw entry values: subsongs=%d, current=%d, duration=%d",
                  entry_subsongs, entry_current, entry_duration);

        /* Validate values */
        if (entry_subsongs <= 0) {
            entry_subsongs = 1;
            U64_DEBUG("WARNING: Fixed subsongs to 1");
        }
        if (entry_current < 0 || entry_current >= entry_subsongs) {
            entry_current = 0;
            U64_DEBUG("WARNING: Fixed current subsong to 0");
        }

        /* Remove .sid extension for cleaner display */
        char clean_name[256];
        strncpy(clean_name, basename, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = '\0';
        char *dot = strrchr(clean_name, '.');
        if (dot && stricmp(dot, ".sid") == 0) {
            *dot = '\0';
        }

        /* Use title if available, otherwise use cleaned filename */
        char *display_name;
        if (entry->title && strlen(entry->title) > 0) {
            display_name = entry->title;
        } else {
            display_name = clean_name;
        }

        /* Allocate display string on heap */
        STRPTR list_string = AllocVec(512, MEMF_PUBLIC | MEMF_CLEAR);
        if (!list_string) {
            U64_DEBUG("ERROR: Failed to allocate display string");
            entry = entry->next;
            continue;
        }

        /* Build display string manually without sprintf */
        list_string[0] = '\0';
        if (entry->is_favourite) {
            strcpy(list_string, "* ");
        }
        strcat(list_string, display_name);

        if (entry_subsongs > 1) {
            /* Add subsong info: " [1/12]" */
            strcat(list_string, " [");

            /* Add current subsong number (1-based) */
            int display_current = entry_current + 1;
            if (display_current < 10) {
                char c = '0' + display_current;
                strncat(list_string, &c, 1);
            } else if (display_current < 100) {
                char temp[3];
                temp[0] = '0' + (display_current / 10);
                temp[1] = '0' + (display_current % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            } else {
                strcat(list_string, "99+"); /* Fallback for very high numbers */
            }

            strcat(list_string, "/");

            /* Add total subsongs */
            if (entry_subsongs < 10) {
                char c = '0' + entry_subsongs;
                strncat(list_string, &c, 1);
            } else if (entry_subsongs < 100) {
                char temp[3];
                temp[0] = '0' + (entry_subsongs / 10);
                temp[1] = '0' + (entry_subsongs % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            } else {
                strcat(list_string, "99+"); /* Fallback */
            }

            strcat(list_string, "] ");

            /* Get duration for current subsong */
            ULONG current_duration = FindSongLength(objApp, entry->md5, entry_current);
            if (current_duration == 0) {
                current_duration = (ULONG)entry_duration;
                if (current_duration == 0) {
                    current_duration = DEFAULT_SONG_LENGTH;
                }
            }

            /* Add time manually */
            int minutes = (int)(current_duration / 60);
            int seconds = (int)(current_duration % 60);

            /* Add minutes */
            if (minutes < 10) {
                char c = '0' + minutes;
                strncat(list_string, &c, 1);
            } else {
                char temp[3];
                temp[0] = '0' + (minutes / 10);
                temp[1] = '0' + (minutes % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            }

            strcat(list_string, ":");

            /* Add seconds (always 2 digits) */
            char temp[3];
            temp[0] = '0' + (seconds / 10);
            temp[1] = '0' + (seconds % 10);
            temp[2] = '\0';
            strcat(list_string, temp);
        } else {
            /* Single subsong: show just time */
            ULONG duration = (ULONG)entry_duration;
            if (duration == 0) {
                duration = FindSongLength(objApp, entry->md5, 0);
                if (duration == 0) {
                    duration = DEFAULT_SONG_LENGTH;
                }
            }

            strcat(list_string, " - ");

            /* Add time manually */
            int minutes = (int)(duration / 60);
            int seconds = (int)(duration % 60);

            /* Add minutes */
            if (minutes < 10) {
                char c = '0' + minutes;
                strncat(list_string, &c, 1);
            } else {
                char temp[3];
                temp[0] = '0' + (minutes / 10);
                temp[1] = '0' + (minutes % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            }

            strcat(list_string, ":");

            /* Add seconds (always 2 digits) */
            char temp[3];
            temp[0] = '0' + (seconds / 10);
            temp[1] = '0' + (seconds % 10);
            temp[2] = '\0';
            strcat(list_string, temp);
        }

        U64_DEBUG("Final display string: '%s'", list_string);

        /* Add to MUI list */
        DoMethod(objApp->LSV_PlaylistList, MUIM_List_InsertSingle, list_string,
                 MUIV_List_Insert_Bottom);

        U64_DEBUG("=== END ENTRY %d ===", (int)i);
        entry = entry->next;
    }

    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);

    /* Highlight current entry */
    if (objApp->current_entry) {
        set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
    }

    U64_DEBUG("=== APP_UpdatePlaylistDisplay FIXED END ===");
}
