#ifndef U64_COMMON_ENV_UTILS_H
#define U64_COMMON_ENV_UTILS_H

#include <exec/types.h>

/* Read an Amiga environment variable. Returns an AllocVec'd buffer the caller
 * must free via FreeVec, or NULL if the variable is unset or on error.
 */
STRPTR U64_ReadEnvVar(CONST_STRPTR var_name);

/* Write an Amiga environment variable. When persistent is TRUE the value is
 * also written to ENVARC: so it survives a reboot.
 */
BOOL U64_WriteEnvVar(CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent);

#endif
