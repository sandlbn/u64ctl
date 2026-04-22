/* Ultimate64 SID Player - SID file parsing helpers
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <exec/memory.h>
#include <exec/types.h>

#include <proto/exec.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"

/* SID file parsing functions */
UWORD ParseSIDSubsongs(const UBYTE *data, ULONG size)
{
    if (size < 0x7E || memcmp(data, "PSID", 4) != 0) {
        return 1; // Default to 1 subsong
    }

    UWORD songs = (data[14] << 8) | data[15];
    return (songs > 0) ? songs : 1;
}

STRPTR ExtractSIDTitle(const UBYTE *data, ULONG size)
{
    if (size < 0x7E || memcmp(data, "PSID", 4) != 0) {
        return NULL;
    }

    // Extract title (offset 0x16, 32 bytes)
    char title[33];
    CopyMem((APTR)(data + 0x16), title, 32);
    title[32] = '\0';

    // Extract author (offset 0x36, 32 bytes)
    char author[33];
    CopyMem((APTR)(data + 0x36), author, 32);
    author[32] = '\0';

    // Clean up both strings
    int i;
    for (i = 31; i >= 0 && (title[i] == ' ' || title[i] == '\0'); i--) {
        title[i] = '\0';
    }
    for (i = 31; i >= 0 && (author[i] == ' ' || author[i] == '\0'); i--) {
        author[i] = '\0';
    }

    // Extract SID model information
    char sid_info[32] = "";
    if (size >= 0x78) {
        UBYTE flags = data[0x77];
        if (flags & 0x10) {  // SID model specified
            if (flags & 0x40) {  // Dual SID
                strcpy(sid_info, (flags & 0x20) ? " (8580+)" : " (6581+)");
            } else {
                strcpy(sid_info, (flags & 0x20) ? " (8580)" : " (6581)");
            }
        }
    }

    // Check if author is valid (not empty, not placeholder)
    BOOL valid_author = (strlen(author) > 0 &&
                        strcmp(author, "<?> ") != 0 &&
                        strcmp(author, "<?>") != 0 &&
                        strcmp(author, "Unknown") != 0 &&
                        strcmp(author, "N/A") != 0);

    // Build result string
    if (valid_author && strlen(title) > 0) {
        // "Author - Title (SID Model)" format
        ULONG combined_len = strlen(author) + strlen(title) + strlen(sid_info) + 4;
        STRPTR combined = AllocVec(combined_len, MEMF_PUBLIC | MEMF_CLEAR);
        if (combined) {
            strcpy(combined, author);
            strcat(combined, " - ");
            strcat(combined, title);
            strcat(combined, sid_info);
            return combined;
        }
    } else if (strlen(title) > 0) {
        // "Title (SID Model)" format - skip author
        ULONG combined_len = strlen(title) + strlen(sid_info) + 1;
        STRPTR combined = AllocVec(combined_len, MEMF_PUBLIC | MEMF_CLEAR);
        if (combined) {
            strcpy(combined, title);
            strcat(combined, sid_info);
            return combined;
        }
    } else if (valid_author) {
        // Fallback to just author if no title
        ULONG combined_len = strlen(author) + strlen(sid_info) + 1;
        STRPTR combined = AllocVec(combined_len, MEMF_PUBLIC | MEMF_CLEAR);
        if (combined) {
            strcpy(combined, author);
            strcat(combined, sid_info);
            return combined;
        }
    }

    return NULL;
}

ULONG ParseTimeString(const char *time_str)
{
    char *colon_pos;
    ULONG minutes = 0;
    ULONG seconds = 0;
    char temp_str[32];

    if (!time_str || strlen(time_str) == 0) {
        return 0;
    }

    /* Make a copy to work with */
    strncpy(temp_str, time_str, sizeof(temp_str) - 1);
    temp_str[sizeof(temp_str) - 1] = '\0';

    /* Find colon separator */
    colon_pos = strchr(temp_str, ':');
    if (!colon_pos) {
        /* No colon, treat as seconds only */
        return (ULONG)atoi(temp_str);
    }

    /* Split at colon */
    *colon_pos = '\0';
    minutes = (ULONG)atoi(temp_str);

    /* Parse seconds part */
    char *seconds_str = colon_pos + 1;
    char *dot_pos = strchr(seconds_str, '.');
    if (dot_pos) {
        *dot_pos = '\0'; /* Ignore milliseconds */
    }

    seconds = (ULONG)atoi(seconds_str);

    /* Convert to total seconds */
    return (minutes * 60) + seconds;
}
