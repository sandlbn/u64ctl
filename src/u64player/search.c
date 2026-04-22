/* Ultimate64 SID Player - search/filter
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/mui.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "player.h"
#include "string_utils.h"

/* Helper function to safely convert string to lowercase */
static STRPTR SafeToLower(CONST_STRPTR input)
{
    ULONG len;
    STRPTR result;
    ULONG i;

    if (!input) return NULL;

    len = strlen(input);
    if (len > 512) len = 512; /* Limit to prevent excessive memory use */

    result = AllocVec(len + 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!result) return NULL;

    for (i = 0; i < len; i++) {
        result[i] = tolower((unsigned char)input[i]);
    }
    result[len] = '\0';

    return result;
}

/* Helper function to check if entry matches search term */
BOOL MatchesSearchTerm(PlaylistEntry *entry, const char *search_term)
{
    STRPTR search_lower = NULL;
    STRPTR title_lower = NULL;
    STRPTR filename_lower = NULL;
    STRPTR basename = NULL;
    BOOL result = FALSE;

    if (!entry || !search_term || strlen(search_term) == 0) {
        return TRUE; /* No search term means everything matches */
    }

    /* Convert search term to lowercase */
    search_lower = SafeToLower(search_term);
    if (!search_lower) {
        return FALSE;
    }

    /* Check title */
    if (entry->title && strlen(entry->title) > 0) {
        title_lower = SafeToLower(entry->title);
        if (title_lower && strstr(title_lower, search_lower)) {
            result = TRUE;
            goto cleanup;
        }
    }

    /* Check filename */
    if (entry->filename && strlen(entry->filename) > 0) {
        basename = FilePart(entry->filename);
        if (basename) {
            filename_lower = SafeToLower(basename);
            if (filename_lower && strstr(filename_lower, search_lower)) {
                result = TRUE;
                goto cleanup;
            }
        }
    }

cleanup:
    if (search_lower) FreeVec(search_lower);
    if (title_lower) FreeVec(title_lower);
    if (filename_lower) FreeVec(filename_lower);

    return result;
}

/* Update search matches and visibility array */
void UpdateSearchMatches(void)
{
    PlaylistEntry *entry;
    ULONG i;

    if (!objApp) return;

    /* Allocate/reallocate visibility array */
    if (objApp->playlist_visible) {
        FreeVec(objApp->playlist_visible);
    }

    if (objApp->playlist_count > 0) {
        objApp->playlist_visible = AllocVec(objApp->playlist_count * sizeof(BOOL), MEMF_PUBLIC);
        if (!objApp->playlist_visible) {
            return;
        }
    }

    /* Update visibility and count matches */
    objApp->search_total_matches = 0;
    objApp->search_current_match = 0;

    entry = objApp->playlist_head;
    for (i = 0; i < objApp->playlist_count && entry; i++) {
        BOOL matches = MatchesSearchTerm(entry, objApp->search_text);

        if (objApp->playlist_visible) {
            objApp->playlist_visible[i] = matches;
        }

        if (matches) {
            objApp->search_total_matches++;
        }

        entry = entry->next;
    }

    /* Update button states */
    BOOL has_matches = (objApp->search_total_matches > 0);
    BOOL has_search = (strlen(objApp->search_text) > 0);

    set(objApp->BTN_SearchNext, MUIA_Disabled, !has_matches || objApp->search_mode_filter);
    set(objApp->BTN_SearchPrev, MUIA_Disabled, !has_matches || objApp->search_mode_filter);
    set(objApp->BTN_SearchClear, MUIA_Disabled, !has_search);
}

/* Modified playlist display function for filtering */
void UpdateFilteredPlaylistDisplay(void)
{
    PlaylistEntry *entry;
    ULONG i, display_index;

    if (!objApp || !objApp->LSV_PlaylistList) return;

    /* Clear list first */
    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, TRUE);
    DoMethod(objApp->LSV_PlaylistList, MUIM_List_Clear);

    if (objApp->playlist_count == 0) {
        set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);
        return;
    }

    /* Add entries based on filter mode */
    entry = objApp->playlist_head;
    display_index = 0;

    for (i = 0; i < objApp->playlist_count && entry; i++) {
        BOOL should_display = TRUE;

        /* In filter mode, only show visible entries */
        if (objApp->search_mode_filter && objApp->playlist_visible) {
            should_display = objApp->playlist_visible[i];
        }

        if (should_display) {
            STRPTR basename = FilePart(entry->filename);
            STRPTR list_string = AllocVec(512, MEMF_PUBLIC | MEMF_CLEAR);

            if (list_string && basename) {
                /* Build clean filename */
                char clean_name[128];
                ULONG clean_len = strlen(basename);
                if (clean_len >= sizeof(clean_name)) {
                    clean_len = sizeof(clean_name) - 1;
                }

                CopyMem(basename, clean_name, clean_len);
                clean_name[clean_len] = '\0';

                char *dot = strrchr(clean_name, '.');
                if (dot && stricmp(dot, ".sid") == 0) {
                    *dot = '\0';
                }

                /* Choose display name safely */
                STRPTR display_name = clean_name;
                if (entry->title && strlen(entry->title) > 0) {
                    display_name = entry->title;
                }

                /* Build display string using safe operations */
                strncpy(list_string, display_name, 200);
                list_string[200] = '\0';

                /* Add subsong and timing info */
                if (entry->subsongs > 1) {
                    char temp[64];
                    sprintf(temp, " [%u/%u]",
                           (unsigned int)(entry->current_subsong + 1),
                           (unsigned int)entry->subsongs);
                    strncat(list_string, temp, 50);

                    ULONG current_duration = FindSongLength(objApp, entry->md5, entry->current_subsong);
                    if (current_duration == 0) current_duration = entry->duration;
                    if (current_duration == 0) current_duration = DEFAULT_SONG_LENGTH;

                    sprintf(temp, " %u:%02u",
                           (unsigned int)(current_duration / 60),
                           (unsigned int)(current_duration % 60));
                    strncat(list_string, temp, 30);
                } else {
                    ULONG duration = entry->duration;
                    if (duration == 0) duration = FindSongLength(objApp, entry->md5, 0);
                    if (duration == 0) duration = DEFAULT_SONG_LENGTH;

                    char temp[32];
                    sprintf(temp, " - %u:%02u",
                           (unsigned int)(duration / 60),
                           (unsigned int)(duration % 60));
                    strncat(list_string, temp, 30);
                }

                DoMethod(objApp->LSV_PlaylistList, MUIM_List_InsertSingle, list_string,
                         MUIV_List_Insert_Bottom);
                display_index++;
            }

            if (list_string) {
                /* Don't free here - MUI will handle it */
            }
        }

        entry = entry->next;
    }

    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);

    /* Update status */
    if (objApp->search_mode_filter && strlen(objApp->search_text) > 0) {
        char status[128];
        sprintf(status, "Filter: %u of %u entries shown",
                (unsigned int)objApp->search_total_matches,
                (unsigned int)objApp->playlist_count);
        APP_UpdateStatus(status);
    }
}

/* Search event handlers */
BOOL APP_SearchTextChanged(void)
{
    STRPTR search_str;
    ULONG len;

    if (!objApp) return FALSE;

    get(objApp->STR_SearchText, MUIA_String_Contents, &search_str);

    if (search_str) {
        len = strlen(search_str);
        if (len >= sizeof(objApp->search_text)) {
            len = sizeof(objApp->search_text) - 1;
        }
        CopyMem(search_str, objApp->search_text, len);
        objApp->search_text[len] = '\0';
    } else {
        objApp->search_text[0] = '\0';
    }

    /* Only update if we have a reasonable search term */
    if (strlen(objApp->search_text) > 0 && strlen(objApp->search_text) < 100) {
        UpdateSearchMatches();

        if (objApp->search_mode_filter) {
            UpdateFilteredPlaylistDisplay();
        } else if (objApp->search_total_matches > 0) {
            /* Search mode - jump to first match */
            APP_SearchNext();
        }
    } else if (strlen(objApp->search_text) == 0) {
        /* Clear search */
        UpdateSearchMatches();
        APP_UpdatePlaylistDisplay();
    }

    return TRUE;
}

BOOL APP_SearchModeChanged(void)
{
    LONG mode;

    if (!objApp) return FALSE;

    get(objApp->CYC_SearchMode, MUIA_Cycle_Active, &mode);
    objApp->search_mode_filter = (mode == 1); /* 1 = Filter */

    UpdateSearchMatches();

    if (objApp->search_mode_filter) {
        UpdateFilteredPlaylistDisplay();
    } else {
        /* Switch back to normal display */
        APP_UpdatePlaylistDisplay();
        if (strlen(objApp->search_text) > 0 && objApp->search_total_matches > 0) {
            APP_SearchNext(); /* Jump to first match */
        }
    }

    return TRUE;
}

BOOL APP_SearchClear(void)
{
    if (!objApp) return FALSE;

    set(objApp->STR_SearchText, MUIA_String_Contents, "");
    objApp->search_text[0] = '\0';

    UpdateSearchMatches();
    APP_UpdatePlaylistDisplay();
    APP_UpdateStatus("Search cleared");

    return TRUE;
}

BOOL APP_SearchNext(void)
{
    PlaylistEntry *entry;
    ULONG i, match_count;

    if (!objApp || objApp->search_total_matches == 0 || objApp->search_mode_filter) {
        return FALSE;
    }

    /* Find next match after current position */
    entry = objApp->playlist_head;
    match_count = 0;

    for (i = 0; i < objApp->playlist_count && entry; i++) {
        if (MatchesSearchTerm(entry, objApp->search_text)) {
            match_count++;

            if (i > objApp->current_index ||
                (objApp->search_current_match >= objApp->search_total_matches)) {

                /* Found next match */
                objApp->current_entry = entry;
                objApp->current_index = i;
                objApp->search_current_match = match_count;

                set(objApp->LSV_PlaylistList, MUIA_List_Active, i);
                APP_UpdateCurrentSongCache();
                APP_UpdateCurrentSongDisplay();

                char status[128];
                STRPTR basename = FilePart(entry->filename);
                sprintf(status, "Match %u of %u: %.60s",
                        (unsigned int)objApp->search_current_match,
                        (unsigned int)objApp->search_total_matches,
                        basename ? basename : "Unknown");
                APP_UpdateStatus(status);

                return TRUE;
            }
        }
        entry = entry->next;
    }

    /* Wrap around to first match */
    objApp->search_current_match = 0;
    return APP_SearchNext();
}

BOOL APP_SearchPrev(void)
{
    PlaylistEntry *entry;
    ULONG i, match_count;
    PlaylistEntry *prev_match = NULL;
    ULONG prev_match_index = 0;
    ULONG prev_match_count = 0;

    if (!objApp || objApp->search_total_matches == 0 || objApp->search_mode_filter) {
        return FALSE;
    }

    /* Find previous match before current position */
    entry = objApp->playlist_head;
    match_count = 0;

    for (i = 0; i < objApp->playlist_count && entry; i++) {
        if (MatchesSearchTerm(entry, objApp->search_text)) {
            match_count++;

            if (i < objApp->current_index) {
                prev_match = entry;
                prev_match_index = i;
                prev_match_count = match_count;
            } else if (i >= objApp->current_index) {
                break; /* Found current or later match */
            }
        }
        entry = entry->next;
    }

    if (prev_match) {
        /* Found previous match */
        objApp->current_entry = prev_match;
        objApp->current_index = prev_match_index;
        objApp->search_current_match = prev_match_count;

        set(objApp->LSV_PlaylistList, MUIA_List_Active, prev_match_index);
        APP_UpdateCurrentSongCache();
        APP_UpdateCurrentSongDisplay();

        char status[128];
        STRPTR basename = FilePart(prev_match->filename);
        sprintf(status, "Match %u of %u: %.60s",
                (unsigned int)objApp->search_current_match,
                (unsigned int)objApp->search_total_matches,
                basename ? basename : "Unknown");
        APP_UpdateStatus(status);

        return TRUE;
    } else {
        /* Wrap around to last match */
        objApp->search_current_match = objApp->search_total_matches + 1;
        return APP_SearchPrev();
    }
}
