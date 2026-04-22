/* Ultimate64 SID Player - playback control + current-song display
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

/* Simple cache for current song info to prevent corruption */
static struct {
  UWORD cached_subsongs;
  UWORD cached_current;
  ULONG cached_duration;
  char cached_title[256];
  char cached_filename[256];
  BOOL cache_valid;
} current_song_cache = { 0, 0, 0, "", "", FALSE };

void APP_UpdateCurrentSongCache(void)
{
    if (!objApp->current_entry) {
        current_song_cache.cache_valid = FALSE;
        return;
    }

    /* Disable interrupts while updating cache to prevent corruption */
    Forbid();

    current_song_cache.cached_subsongs = objApp->current_entry->subsongs;
    current_song_cache.cached_current = objApp->current_entry->current_subsong;
    current_song_cache.cached_duration = objApp->current_entry->duration;

    if (objApp->current_entry->title) {
        strncpy(current_song_cache.cached_title, objApp->current_entry->title,
                sizeof(current_song_cache.cached_title) - 1);
        current_song_cache.cached_title[sizeof(current_song_cache.cached_title) - 1] = '\0';
    } else {
        current_song_cache.cached_title[0] = '\0';
    }

    if (objApp->current_entry->filename) {
        strncpy(current_song_cache.cached_filename, objApp->current_entry->filename,
                sizeof(current_song_cache.cached_filename) - 1);
        current_song_cache.cached_filename[sizeof(current_song_cache.cached_filename) - 1] = '\0';
    } else {
        current_song_cache.cached_filename[0] = '\0';
    }

    current_song_cache.cache_valid = TRUE;

    Permit();
}

void
APP_UpdateCurrentSongDisplay(void)
{
    char song_str[256];

    if (!objApp) return;

    U64_DEBUG("=== APP_UpdateCurrentSongDisplay FIXED START ===");

    if (!objApp->current_entry || !current_song_cache.cache_valid) {
        set(objApp->TXT_CurrentSong, MUIA_Text_Contents, "No song loaded");
        set(objApp->TXT_CurrentTime, MUIA_Text_Contents, "0:00");
        set(objApp->TXT_TotalTime, MUIA_Text_Contents, "0:00");
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, "");
        set(objApp->GAU_Progress, MUIA_Gauge_Current, 0);
        return;
    }

    /* Use cached values */
    int subsongs = (int)current_song_cache.cached_subsongs;
    int current = (int)current_song_cache.cached_current;

    U64_DEBUG("Values: subsongs=%d, current=%d", subsongs, current);
    U64_DEBUG("Times: current_time=%lu, total_time=%lu",
              (unsigned long)objApp->current_time, (unsigned long)objApp->total_time);

    /* Song title */
    if (current_song_cache.cached_title[0] != '\0') {
        strcpy(song_str, current_song_cache.cached_title);
    } else {
        char *basename = FilePart(current_song_cache.cached_filename);
        strcpy(song_str, basename);
        char *dot = strrchr(song_str, '.');
        if (dot && stricmp(dot, ".sid") == 0) {
            *dot = '\0';
        }
    }
    set(objApp->TXT_CurrentSong, MUIA_Text_Contents, song_str);

    /* Current time string */
    static char current_time_str[16];
    current_time_str[0] = '\0';

    int current_minutes = (int)(objApp->current_time / 60);
    int current_seconds = (int)(objApp->current_time % 60);

    /* Build current time manually */
    if (current_minutes < 10) {
        char c = '0' + current_minutes;
        strncat(current_time_str, &c, 1);
    } else {
        char temp[3];
        temp[0] = '0' + (current_minutes / 10);
        temp[1] = '0' + (current_minutes % 10);
        temp[2] = '\0';
        strcat(current_time_str, temp);
    }

    strcat(current_time_str, ":");

    /* Add seconds (always 2 digits) */
    char temp[3];
    temp[0] = '0' + (current_seconds / 10);
    temp[1] = '0' + (current_seconds % 10);
    temp[2] = '\0';
    strcat(current_time_str, temp);

    U64_DEBUG("Built current time string: '%s'", current_time_str);
    set(objApp->TXT_CurrentTime, MUIA_Text_Contents, current_time_str);

    /* Total time string */
    static char total_time_str[16];
    total_time_str[0] = '\0';

    int total_minutes = (int)(objApp->total_time / 60);
    int total_seconds = (int)(objApp->total_time % 60);

    /* Build total time manually */
    if (total_minutes < 10) {
        char c = '0' + total_minutes;
        strncat(total_time_str, &c, 1);
    } else {
        char temp2[3];
        temp2[0] = '0' + (total_minutes / 10);
        temp2[1] = '0' + (total_minutes % 10);
        temp2[2] = '\0';
        strcat(total_time_str, temp2);
    }

    strcat(total_time_str, ":");

    /* Add seconds (always 2 digits) */
    char temp3[3];
    temp3[0] = '0' + (total_seconds / 10);
    temp3[1] = '0' + (total_seconds % 10);
    temp3[2] = '\0';
    strcat(total_time_str, temp3);

    U64_DEBUG("Built total time string: '%s'", total_time_str);
    set(objApp->TXT_TotalTime, MUIA_Text_Contents, total_time_str);

    /* Subsong info - build manually */
    if (subsongs > 1) {
        /* Use static string to avoid stack issues */
        static char subsong_str[64];
        subsong_str[0] = '\0';

        strcat(subsong_str, "Subsong ");

        /* Add current subsong number (1-based) */
        int display_current = current + 1;
        if (display_current < 10) {
            char c = '0' + display_current;
            strncat(subsong_str, &c, 1);
        } else if (display_current < 100) {
            char temp4[3];
            temp4[0] = '0' + (display_current / 10);
            temp4[1] = '0' + (display_current % 10);
            temp4[2] = '\0';
            strcat(subsong_str, temp4);
        }

        strcat(subsong_str, " of ");

        /* Add total subsongs */
        if (subsongs < 10) {
            char c = '0' + subsongs;
            strncat(subsong_str, &c, 1);
        } else if (subsongs < 100) {
            char temp5[3];
            temp5[0] = '0' + (subsongs / 10);
            temp5[1] = '0' + (subsongs % 10);
            temp5[2] = '\0';
            strcat(subsong_str, temp5);
        }

        U64_DEBUG("Built subsong string: '%s'", subsong_str);
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, subsong_str);
    } else {
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, "");
    }

    /* Progress gauge */
    if (objApp->total_time > 0) {
        ULONG progress = (objApp->current_time * 100) / objApp->total_time;
        if (progress > 100) progress = 100;
        set(objApp->GAU_Progress, MUIA_Gauge_Current, progress);
        U64_DEBUG("Progress: %lu%%", (unsigned long)progress);
    } else {
        set(objApp->GAU_Progress, MUIA_Gauge_Current, 0);
        U64_DEBUG("No total time, progress = 0");
    }

    U64_DEBUG("=== APP_UpdateCurrentSongDisplay FIXED END ===");
}

BOOL
PlayCurrentSong(struct ObjApp *obj)
{
    UBYTE *file_data;
    ULONG file_size;
    STRPTR error_details = NULL;
    LONG result;

    if (!obj->connection || !obj->current_entry) {
        return FALSE;
    }

    U64_DEBUG("=== PlayCurrentSong FIXED START ===");

    /* Update cache BEFORE doing anything else */
    APP_UpdateCurrentSongCache();

    /* Use cached values for safety */
    UWORD current_subsong = current_song_cache.cached_current;
    UWORD total_subsongs = current_song_cache.cached_subsongs;

    /* Ultimate64 expects subsong numbers starting from 1, not 0 */
    UBYTE ultimate64_subsong = current_subsong + 1;

    U64_DEBUG("PlayCurrentSong: Playing subsong %d (0-based) = %d (Ultimate64 1-based)",
              current_subsong, ultimate64_subsong);

    /* Load file */
    file_data = U64_ReadFile(obj->current_entry->filename, &file_size);
    if (!file_data) {
        APP_UpdateStatus("Failed to load SID file");
        return FALSE;
    }

    /* Play SID with specific subsong (1-based for Ultimate64) */
    result = U64_PlaySID(obj->connection, file_data, file_size, ultimate64_subsong, &error_details);

    FreeVec(file_data);

    if (result != U64_OK) {
        char error_msg[512];
        /* Build error message manually */
        strcpy(error_msg, "SID playback failed: ");
        if (error_details) {
            strcat(error_msg, error_details);
        } else {
            strcat(error_msg, U64_GetErrorString(result));
        }
        APP_UpdateStatus(error_msg);

        if (error_details) {
            FreeVec(error_details);
        }
        return FALSE;
    }

    if (error_details) {
        FreeVec(error_details);
    }

    /* Set proper total time for current subsong */
    obj->state = PLAYER_PLAYING;
    obj->current_time = 0;

    /* Get duration for the specific subsong being played */
    obj->total_time = FindSongLength(obj, obj->current_entry->md5, current_subsong);

    U64_DEBUG("Set total_time to %lu seconds for subsong %d",
              (unsigned long)obj->total_time, current_subsong);

    obj->timer_counter = 0;

    /* Update display using cached values */
    APP_UpdateCurrentSongDisplay();

    char *basename = FilePart(current_song_cache.cached_filename);

    /* Use static string to avoid stack corruption */
    static char status_msg[256];
    strcpy(status_msg, "Playing ");
    strcat(status_msg, basename);
    strcat(status_msg, " [");

    /* Add current subsong (1-based) manually */
    int display_current = (int)current_subsong + 1;
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

    /* Add total subsongs manually */
    int display_total = (int)total_subsongs;
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

    U64_DEBUG("Status message built: '%s'", status_msg);
    APP_UpdateStatus(status_msg);

    U64_DEBUG("=== PlayCurrentSong FIXED END ===");
    return TRUE;
}

BOOL APP_Play(void)
{
    LONG selected_index;

    if (!objApp) return FALSE;

    if (objApp->playlist_count == 0) {
        APP_UpdateStatus("No songs in playlist");
        return FALSE;
    }

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &selected_index);

    if (objApp->current_entry) {
        set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);

        APP_UpdateCurrentSongCache();
        if (PlayCurrentSong(objApp)) {
            /* NEW: Start periodic timer */
            StartPeriodicTimer();

            APP_UpdatePlaylistDisplay();
            return TRUE;
        } else {
            return FALSE;
        }
    }
    else if (objApp->playlist_head) {
        objApp->current_entry = objApp->playlist_head;
        objApp->current_index = 0;

        set(objApp->LSV_PlaylistList, MUIA_List_Active, 0);

        APP_UpdateCurrentSongCache();
        if (PlayCurrentSong(objApp)) {
            /* NEW: Start periodic timer */
            StartPeriodicTimer();

            APP_UpdatePlaylistDisplay();
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        APP_UpdateStatus("No songs in playlist");
        return FALSE;
    }
}

BOOL APP_Stop(void)
{
    LONG result;

    if (!objApp) return FALSE;

    if (objApp->state == PLAYER_PLAYING) {
        objApp->state = PLAYER_STOPPED;
        objApp->current_time = 0;

        /* NEW: Stop periodic timer */
        StopPeriodicTimer();

        /* NEW: Reset the Ultimate64 when stopping playback */
        if (objApp->connection) {
            result = U64_Reset(objApp->connection);
            if (result != U64_OK) {
                static char error_msg[256];
                strcpy(error_msg, "Reset failed: ");
                strcat(error_msg, U64_GetErrorString(result));
                APP_UpdateStatus(error_msg);
                U64_DEBUG("Reset failed: %s", U64_GetErrorString(result));
            } else {
                U64_DEBUG("Reset successful after stop");
            }
        }

        APP_UpdateCurrentSongDisplay();
        APP_UpdateStatus("Stopped");
    }

    return TRUE;
}

BOOL APP_Next(void)
{
    if (!objApp || !objApp->current_entry) {
        return FALSE;
    }

    /* Check if we have more subsongs */
    if (objApp->current_entry->current_subsong + 1 < objApp->current_entry->subsongs) {
        objApp->current_entry->current_subsong++;
        objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5,
                                                         objApp->current_entry->current_subsong);
        if (objApp->current_entry->duration == 0) {
            objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

        APP_UpdateCurrentSongCache();
        APP_UpdatePlaylistDisplay();

        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        } else {
            APP_UpdateCurrentSongDisplay();
        }
        return TRUE;
    }

    /* Reset current subsong and move to next entry */
    objApp->current_entry->current_subsong = 0;

    /* Move to next entry */
    if (objApp->shuffle_mode) {
        /* Random selection */
        if (objApp->playlist_count > 1) {
            ULONG random_index;
            PlaylistEntry *entry;

            do {
                random_index = rand() % objApp->playlist_count;
            } while (random_index == objApp->current_index && objApp->playlist_count > 1);

            entry = objApp->playlist_head;
            for (ULONG i = 0; i < random_index && entry; i++) {
                entry = entry->next;
            }

            objApp->current_entry = entry;
            objApp->current_index = random_index;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, random_index);
        }
    } else {
        /* Sequential */
        if (objApp->current_entry->next) {
            objApp->current_entry = objApp->current_entry->next;
            objApp->current_index++;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
        } else if (objApp->repeat_mode) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, 0);
        } else {
            LONG result;

            objApp->state = PLAYER_STOPPED;

            /* NEW: Reset the Ultimate64 when playlist ends */
            if (objApp->connection) {
                result = U64_Reset(objApp->connection);
                if (result != U64_OK) {
                    static char error_msg[256];
                    strcpy(error_msg, "Reset failed at playlist end: ");
                    strcat(error_msg, U64_GetErrorString(result));
                    APP_UpdateStatus(error_msg);
                    U64_DEBUG("Reset failed at playlist end: %s", U64_GetErrorString(result));
                } else {
                    U64_DEBUG("Reset successful at playlist end");
                    APP_UpdateStatus("Playlist finished - Reset complete");
                }
            } else {
                APP_UpdateStatus("Playlist finished");
            }

            APP_UpdateCurrentSongDisplay();
            return TRUE;

        }
    }

    /* Update duration for new song */
    objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5,
                                                     objApp->current_entry->current_subsong);
    if (objApp->current_entry->duration == 0) {
        objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
    }

    APP_UpdateCurrentSongCache();
    APP_UpdatePlaylistDisplay();

    if (objApp->state == PLAYER_PLAYING) {
        PlayCurrentSong(objApp);
    } else {
        APP_UpdateCurrentSongDisplay();
    }

    return TRUE;
}

BOOL APP_Prev(void)
{
    if (!objApp || !objApp->current_entry) {
        return FALSE;
    }

    /* If we're more than 3 seconds into the song, restart current song */
    if (objApp->current_time > 3) {
        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        }
        return TRUE;
    }

    /* Check if we can go to previous subsong */
    if (objApp->current_entry->current_subsong > 0) {
        objApp->current_entry->current_subsong--;
        objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5,
                                                         objApp->current_entry->current_subsong);
        if (objApp->current_entry->duration == 0) {
            objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

        APP_UpdateCurrentSongCache();
        APP_UpdatePlaylistDisplay();

        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        } else {
            APP_UpdateCurrentSongDisplay();
        }
        return TRUE;
    }

    /* Move to previous entry */
    if (objApp->current_index > 0) {
        PlaylistEntry *entry = objApp->playlist_head;

        for (ULONG i = 0; i < objApp->current_index - 1 && entry; i++) {
            entry = entry->next;
        }

        if (entry) {
            objApp->current_entry = entry;
            objApp->current_index--;

            /* Set to last subsong */
            objApp->current_entry->current_subsong = objApp->current_entry->subsongs - 1;
            objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5,
                                                             objApp->current_entry->current_subsong);
            if (objApp->current_entry->duration == 0) {
                objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
            }

            set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
            APP_UpdateCurrentSongCache();
            APP_UpdatePlaylistDisplay();

            if (objApp->state == PLAYER_PLAYING) {
                PlayCurrentSong(objApp);
            } else {
                APP_UpdateCurrentSongDisplay();
            }
        }
    }

    return TRUE;
}

void APP_TimerUpdate(void)
{
    if (!objApp) return;

    if (objApp->state == PLAYER_PLAYING) {
        objApp->timer_counter++;

        /* Update every 10 cycles (approximately 1 second) */
        if (objApp->timer_counter >= 10) {
            objApp->timer_counter = 0;
            objApp->current_time++;
            APP_UpdateCurrentSongDisplay();

            /* Check if current subsong is finished */
            if (objApp->current_time >= objApp->total_time) {
                APP_Next(); /* Auto-advance to next subsong/song */
            }
        }
    }
}
