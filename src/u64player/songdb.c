/* Ultimate64 SID Player - song-length database
 * For Amiga OS 3.x by Marcin Spoczynski
 */

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

#include "player.h"
#include "file_utils.h"

/* Song length database functions */
BOOL CheckSongLengthsFile(char *filepath, ULONG filepath_size)
{
    BPTR lock;
    char progdir[256];
    BOOL found = FALSE;

    /* Get program directory */
    lock = GetProgramDir();
    if (lock) {
        if (NameFromLock(lock, progdir, sizeof(progdir))) {
            /* Try common filename variations */
            const char *filenames[] = {
                "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5",
                "Songlengths.txt", "songlengths.txt", NULL
            };

            int i;
            for (i = 0; filenames[i]; i++) {
                strncpy(filepath, progdir, filepath_size - 1);
                filepath[filepath_size - 1] = '\0';
                AddPart(filepath, filenames[i], filepath_size);

                BPTR file = Open(filepath, MODE_OLDFILE);
                if (file) {
                    Close(file);
                    U64_DEBUG("Found songlengths file: %s", filepath);
                    found = TRUE;
                    break;
                }
            }
        }
    }

    if (!found) {
        /* Also try current directory */
        const char *filenames[] = {
            "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5", NULL
        };

        int i;
        for (i = 0; filenames[i]; i++) {
            BPTR file = Open(filenames[i], MODE_OLDFILE);
            if (file) {
                Close(file);
                strcpy(filepath, filenames[i]);
                U64_DEBUG("Found songlengths file in current dir: %s", filepath);
                found = TRUE;
                break;
            }
        }
    }

    return found;
}

/* Load song lengths database with progress feedback */
BOOL
LoadSongLengthsWithProgress(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    char line[1024];
    SongLengthEntry *entry;
    ULONG count = 0;
    ULONG line_count = 0;
    ULONG processed_lines = 0;

    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        APP_UpdateStatus("Cannot open songlengths file");
        return FALSE;
    }

    /* First pass: count total lines for progress */
    APP_UpdateStatus("Analyzing songlengths database...");
    while (FGets(file, line, sizeof(line))) {
        line_count++;
        if ((line_count % 1000) == 0) {
            /* Build progress message manually */
            static char progress_msg[256];
            strcpy(progress_msg, "Analyzing... ");

            /* Convert line_count to string manually */
            char count_str[32];
            ULONG temp_count = line_count;
            int pos = 0;

            if (temp_count == 0) {
                count_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;

                while (temp_count > 0) {
                    digits[digit_pos++] = '0' + (temp_count % 10);
                    temp_count /= 10;
                }

                /* Reverse digits */
                for (int i = digit_pos - 1; i >= 0; i--) {
                    count_str[pos++] = digits[i];
                }
            }
            count_str[pos] = '\0';

            strcat(progress_msg, count_str);
            strcat(progress_msg, " lines");
            APP_UpdateStatus(progress_msg);

            /* Process MUI events to keep interface responsive */
            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }
    }

    /* Reset to beginning */
    Seek(file, 0, OFFSET_BEGINNING);

    /* Free existing database */
    FreeSongLengthDB(obj);

    APP_UpdateStatus("Loading song lengths database...");

    while (FGets(file, line, sizeof(line))) {
        char *md5_str, *length_str, *pos;
        UBYTE md5[MD5_HASH_SIZE];
        char *newline;

        processed_lines++;

        /* Update progress every 100 lines */
        if ((processed_lines % 100) == 0) {
            /* Build progress message manually */
            static char progress_msg[256];
            strcpy(progress_msg, "Loading database... ");

            /* Calculate percentage manually */
            ULONG percent = (processed_lines * 100) / line_count;

            /* Convert percent to string */
            char percent_str[8];
            ULONG temp_percent = percent;
            int pos = 0;

            if (temp_percent == 0) {
                percent_str[0] = '0';
                pos = 1;
            } else {
                char digits[8];
                int digit_pos = 0;

                while (temp_percent > 0) {
                    digits[digit_pos++] = '0' + (temp_percent % 10);
                    temp_percent /= 10;
                }

                for (int i = digit_pos - 1; i >= 0; i--) {
                    percent_str[pos++] = digits[i];
                }
            }
            percent_str[pos] = '\0';

            strcat(progress_msg, percent_str);
            strcat(progress_msg, "% (");

            /* Add processed_lines count */
            char processed_str[32];
            ULONG temp_processed = processed_lines;
            pos = 0;

            if (temp_processed == 0) {
                processed_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;

                while (temp_processed > 0) {
                    digits[digit_pos++] = '0' + (temp_processed % 10);
                    temp_processed /= 10;
                }

                for (int i = digit_pos - 1; i >= 0; i--) {
                    processed_str[pos++] = digits[i];
                }
            }
            processed_str[pos] = '\0';

            strcat(progress_msg, processed_str);
            strcat(progress_msg, "/");

            /* Add total line count */
            char total_str[32];
            ULONG temp_total = line_count;
            pos = 0;

            if (temp_total == 0) {
                total_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;

                while (temp_total > 0) {
                    digits[digit_pos++] = '0' + (temp_total % 10);
                    temp_total /= 10;
                }

                for (int i = digit_pos - 1; i >= 0; i--) {
                    total_str[pos++] = digits[i];
                }
            }
            total_str[pos] = '\0';

            strcat(progress_msg, total_str);
            strcat(progress_msg, " entries)");

            APP_UpdateStatus(progress_msg);

            /* Process MUI events to keep interface responsive */
            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        /* Remove trailing newline */
        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        /* Find the '=' separator */
        pos = strchr(line, '=');
        if (!pos) {
            continue;
        }

        *pos = '\0';
        md5_str = line;
        length_str = pos + 1;

        /* Validate MD5 string length */
        if (strlen(md5_str) != 32) {
            continue;
        }

        /* Parse MD5 */
        if (!HexStringToMD5(md5_str, md5)) {
            continue;
        }

        /* Parse lengths */
        ULONG lengths[256];
        UWORD num_lengths = 0;

        char length_copy[512];
        strncpy(length_copy, length_str, sizeof(length_copy) - 1);
        length_copy[sizeof(length_copy) - 1] = '\0';

        char *token = strtok(length_copy, " \t");

        while (token && num_lengths < 256) {
            ULONG duration = ParseTimeString(token);
            if (duration > 0) {
                lengths[num_lengths] = duration + 1; /* +1 second buffer */
                num_lengths++;
            }
            token = strtok(NULL, " \t");
        }

        if (num_lengths == 0) {
            continue;
        }

        /* Create database entry */
        entry = AllocVec(sizeof(SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!entry) {
            continue;
        }

        CopyMem(md5, entry->md5, MD5_HASH_SIZE);
        entry->num_subsongs = num_lengths;
        entry->lengths = AllocVec(sizeof(ULONG) * num_lengths, MEMF_PUBLIC);

        if (!entry->lengths) {
            FreeVec(entry);
            continue;
        }

        CopyMem(lengths, entry->lengths, sizeof(ULONG) * num_lengths);

        /* Add to database */
        entry->next = obj->songlength_db;
        obj->songlength_db = entry;
        count++;
    }

    Close(file);

    /* Build final status message manually */
    static char status_msg[256];
    strcpy(status_msg, "Loaded ");

    /* Convert count to string manually */
    char count_str[32];
    ULONG temp_count = count;
    int pos = 0;

    if (temp_count == 0) {
        count_str[0] = '0';
        pos = 1;
    } else {
        char digits[16];
        int digit_pos = 0;

        while (temp_count > 0) {
            digits[digit_pos++] = '0' + (temp_count % 10);
            temp_count /= 10;
        }

        for (int i = digit_pos - 1; i >= 0; i--) {
            count_str[pos++] = digits[i];
        }
    }
    count_str[pos] = '\0';

    strcat(status_msg, count_str);
    strcat(status_msg, " song length entries (+1 sec buffer)");
    APP_UpdateStatus(status_msg);

    U64_DEBUG("Total entries loaded: %u (all durations include +1 second buffer)",
              (unsigned int)count);
    return TRUE;
}

/* Auto-load songlengths on startup */
void AutoLoadSongLengths(struct ObjApp *obj)
{
    char filepath[512];

    U64_DEBUG("=== AutoLoadSongLengths START ===");

    if (CheckSongLengthsFile(filepath, sizeof(filepath))) {
        U64_DEBUG("Auto-loading songlengths from: %s", filepath);

        APP_UpdateStatus("Auto-loading songlengths database...");

        /* Disable the Load Songlengths button temporarily */
        set(obj->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
        set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Loading...");

        if (LoadSongLengthsWithProgress(obj, filepath)) {
            /* Update any existing playlist entries */
            if (obj->playlist_count > 0) {
                APP_UpdateStatus("Updating playlist with song lengths...");

                PlaylistEntry *entry = obj->playlist_head;
                ULONG updated = 0;
                ULONG entry_num = 0;

                while (entry) {
                    entry_num++;

                    /* Update progress every 10 entries */
                    if ((entry_num % 10) == 0) {
                        char progress_msg[256];
                        sprintf(progress_msg, "Updating playlist... %u/%u",
                                (unsigned int)entry_num,
                                (unsigned int)obj->playlist_count);
                        APP_UpdateStatus(progress_msg);

                        /* Process MUI events */
                        ULONG signals;
                        DoMethod(obj->App, MUIM_Application_Input, &signals);
                    }

                    /* Check if we have this SID in database */
                    SongLengthEntry *db_entry = obj->songlength_db;

                    while (db_entry) {
                        if (memcmp(db_entry->md5, entry->md5, MD5_HASH_SIZE) == 0) {
                            UWORD old_subsongs = entry->subsongs;

                            if (db_entry->num_subsongs > old_subsongs
                                && db_entry->num_subsongs <= 256) {
                                entry->subsongs = db_entry->num_subsongs;
                                updated++;
                                U64_DEBUG("Updated %s: %d -> %d subsongs",
                                          FilePart(entry->filename),
                                          old_subsongs, entry->subsongs);
                            }

                            /* Update duration for current subsong */
                            entry->duration = FindSongLength(obj, entry->md5, entry->current_subsong);
                            break;
                        }
                        db_entry = db_entry->next;
                    }

                    entry = entry->next;
                }

                /* Update display */
                APP_UpdatePlaylistDisplay();
                if (obj->current_entry) {
                    APP_UpdateCurrentSongCache();
                    APP_UpdateCurrentSongDisplay();
                }

                char final_msg[256];
                sprintf(final_msg,
                        "Database loaded. Updated %u of %u playlist entries",
                        (unsigned int)updated,
                        (unsigned int)obj->playlist_count);
                APP_UpdateStatus(final_msg);
            } else {
                APP_UpdateStatus("Songlengths database loaded successfully");
            }
        }

        /* Re-enable the button */
        set(obj->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
    } else {
        U64_DEBUG("No songlengths file found in program directory");
        APP_UpdateStatus("Ready - No songlengths database found");
    }

    U64_DEBUG("=== AutoLoadSongLengths END ===");
}

/* Progress callback for MUI updates */
static void DownloadProgressCallback(ULONG bytes_downloaded, APTR userdata)
{
    (void)userdata;
    static char progress_msg[256];

    /* Add debug output */
    U64_DEBUG("Progress callback: %lu bytes", (unsigned long)bytes_downloaded);

    /* Convert bytes to KB for display */
    ULONG kb_downloaded = bytes_downloaded / 1024;

    sprintf(progress_msg, "Downloaded %lu KB from HVSC server",
        (unsigned long)kb_downloaded);  /* Use %lu instead of %u */

    APP_UpdateStatus(progress_msg);

    /* Process MUI events to keep interface responsive */
    if (objApp && objApp->App) {
        ULONG signals;
        DoMethod(objApp->App, MUIM_Application_Input, &signals);
    }
}

BOOL APP_DownloadSongLengths(void)
{
    char local_filename[256];
    char progdir[256];
    BPTR lock;
    LONG result;

    if (!objApp) return FALSE;

    APP_UpdateStatus("Connecting to HVSC server...");

    /* Disable button during download */
    set(objApp->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
    set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Downloading...");

    /* Determine save location */
    lock = GetProgramDir();
    if (lock && NameFromLock(lock, progdir, sizeof(progdir))) {
        strcpy(local_filename, progdir);
        AddPart(local_filename, "Songlengths.md5", sizeof(local_filename));
    } else {
        strcpy(local_filename, "Songlengths.md5");
    }

    U64_DEBUG("Download target: %s", local_filename);

    const char *urls[] = {
        "http://hvsc.brona.dk/HVSC/C64Music/DOCUMENTS/Songlengths.md5",
        NULL
    };

    result = U64_ERR_GENERAL;
    for (int i = 0; urls[i] && result != U64_OK; i++) {
        char status_msg[256];
        sprintf(status_msg, "Trying download from URL %d...", i + 1);
        APP_UpdateStatus(status_msg);

        U64_DEBUG("Attempting download from: %s", urls[i]);
        result = U64_DownloadToFile(urls[i], local_filename, DownloadProgressCallback, NULL);

        if (result != U64_OK) {
            U64_DEBUG("Download attempt %d failed: %s", i + 1, U64_GetErrorString(result));
        }
    }

    if (result != U64_OK) {
        char error_msg[256];

        /* Provide specific error messages */
        switch (result) {
            case U64_ERR_NETWORK:
                sprintf(error_msg, "Network error - check internet connection");
                break;
            case U64_ERR_ACCESS:
                sprintf(error_msg, "File access error - check disk space and permissions");
                break;
            case U64_ERR_NOTFOUND:
                sprintf(error_msg, "File not found on HVSC server");
                break;
            case U64_ERR_TIMEOUT:
                sprintf(error_msg, "Download timeout - server may be busy");
                break;
            default:
                sprintf(error_msg, "Download failed: %s", U64_GetErrorString(result));
                break;
        }

        APP_UpdateStatus(error_msg);
        U64_DEBUG("All download attempts failed");

        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    APP_UpdateStatus("Download complete, verifying file...");

    /* Verify downloaded file */
    BPTR test_file = Open(local_filename, MODE_OLDFILE);
    if (!test_file) {
        APP_UpdateStatus("Error: Downloaded file not accessible");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    /* Check file size */
    Seek(test_file, 0, OFFSET_END);
    LONG file_size = Seek(test_file, 0, OFFSET_BEGINNING);
    Close(test_file);

    if (file_size < 10000) { /* Expect at least 10KB */
        char error_msg[256];
        sprintf(error_msg, "Downloaded file too small (%ld bytes) - may be error page", file_size);
        APP_UpdateStatus(error_msg);
        DeleteFile(local_filename);

        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    char verify_msg[256];
    sprintf(verify_msg, "File verified OK (%ld bytes), loading database...", file_size);
    APP_UpdateStatus(verify_msg);

    /* Load the downloaded file */
    if (LoadSongLengthsWithProgress(objApp, local_filename)) {
        /* Update existing playlist entries */
        if (objApp->playlist_count > 0) {
            PlaylistEntry *entry = objApp->playlist_head;
            ULONG updated = 0;
            ULONG entry_num = 0;

            APP_UpdateStatus("Updating playlist with HVSC song lengths...");

            while (entry) {
                entry_num++;
                if ((entry_num % 10) == 0) {
                    char progress_msg[256];
                    sprintf(progress_msg, "Updating playlist... %u/%u",
                            (unsigned int)entry_num, (unsigned int)objApp->playlist_count);
                    APP_UpdateStatus(progress_msg);

                    ULONG signals;
                    DoMethod(objApp->App, MUIM_Application_Input, &signals);
                }

                SongLengthEntry *db_entry = objApp->songlength_db;
                while (db_entry) {
                    if (MD5Compare(db_entry->md5, entry->md5)) {
                        if (db_entry->num_subsongs > entry->subsongs && db_entry->num_subsongs <= 256) {
                            entry->subsongs = db_entry->num_subsongs;
                            updated++;
                        }
                        entry->duration = FindSongLength(objApp, entry->md5, entry->current_subsong);
                        break;
                    }
                    db_entry = db_entry->next;
                }
                entry = entry->next;
            }

            APP_UpdatePlaylistDisplay();
            if (objApp->current_entry) {
                APP_UpdateCurrentSongCache();
                APP_UpdateCurrentSongDisplay();
            }

            char final_msg[256];
            sprintf(final_msg, "HVSC database loaded! Updated %u of %u playlist entries",
                    (unsigned int)updated, (unsigned int)objApp->playlist_count);
            APP_UpdateStatus(final_msg);
        } else {
            APP_UpdateStatus("HVSC Songlengths database downloaded and loaded successfully!");
        }
    } else {
        APP_UpdateStatus("File downloaded but database loading failed");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
    set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");

    return TRUE;
}

ULONG FindSongLength(struct ObjApp *obj, const UBYTE md5[MD5_HASH_SIZE], UWORD subsong)
{
    SongLengthEntry *entry = obj->songlength_db;

    while (entry) {
        if (MD5Compare(entry->md5, md5)) {
            if (subsong < entry->num_subsongs) {
                return entry->lengths[subsong];
            }
            break;
        }
        entry = entry->next;
    }

    return DEFAULT_SONG_LENGTH;
}

void FreeSongLengthDB(struct ObjApp *obj)
{
    SongLengthEntry *entry = obj->songlength_db;
    SongLengthEntry *next;

    while (entry) {
        next = entry->next;

        if (entry->lengths) {
            FreeVec(entry->lengths);
        }
        FreeVec(entry);

        entry = next;
    }

    obj->songlength_db = NULL;
}
