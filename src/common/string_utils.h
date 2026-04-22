#ifndef U64_COMMON_STRING_UTILS_H
#define U64_COMMON_STRING_UTILS_H

#include <exec/types.h>

/* Convert C-style escape sequences (\n, \r, \t, \\, \", \0) in input to
 * their raw byte values. Returns an AllocVec'd buffer (caller FreeVec) or
 * NULL if input is NULL or allocation fails. Unknown escapes are preserved
 * verbatim.
 */
STRPTR U64_ConvertEscapeSequences(CONST_STRPTR input);

/* Duplicate a NUL-terminated string into an AllocVec'd buffer. Returns
 * NULL on NULL input or allocation failure. Caller frees with
 * U64_SafeStrFree (or FreeVec).
 */
STRPTR U64_SafeStrDup(CONST_STRPTR str);

/* Free a string allocated by U64_SafeStrDup. NULL is safe. */
void U64_SafeStrFree(STRPTR str);

/* Escape ", \\, \n and \r in str for embedding in a quoted line (playlist
 * format). Returns an AllocVec'd buffer (caller FreeVec).
 */
STRPTR U64_EscapeString(CONST_STRPTR str);

/* Reverse of U64_EscapeString. Returns an AllocVec'd buffer (caller
 * FreeVec).
 */
STRPTR U64_UnescapeString(CONST_STRPTR str);

#endif
