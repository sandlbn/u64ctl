#ifndef U64_COMMON_FILE_UTILS_H
#define U64_COMMON_FILE_UTILS_H

#include <exec/types.h>

/* Load a whole file into memory. Returns an AllocVec'd buffer (caller FreeVec)
 * or NULL on error. When size is non-NULL the byte count is written there on
 * success.
 */
UBYTE *U64_ReadFile(CONST_STRPTR filename, ULONG *size);

#endif
