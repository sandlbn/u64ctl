/* Ultimate64 Control - small parsing helpers
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <stdlib.h>
#include <string.h>

#include "mui_app.h"

/* Parse address string (hex or decimal) */
UWORD
ParseAddress (CONST_STRPTR addr_str)
{
  if (!addr_str)
    return 0;

  if (strncmp (addr_str, "0x", 2) == 0 || strncmp (addr_str, "0X", 2) == 0)
    {
      return (UWORD)strtol (addr_str, NULL, 16);
    }
  else if (addr_str[0] == '$')
    {
      return (UWORD)strtol (addr_str + 1, NULL, 16);
    }
  else
    {
      return (UWORD)strtol (addr_str, NULL, 10);
    }
}

/* Parse value string (hex or decimal) */
UBYTE
ParseValue (CONST_STRPTR val_str)
{
  if (!val_str)
    return 0;

  if (strncmp (val_str, "0x", 2) == 0 || strncmp (val_str, "0X", 2) == 0)
    {
      return (UBYTE)strtol (val_str, NULL, 16);
    }
  else if (val_str[0] == '$')
    {
      return (UBYTE)strtol (val_str + 1, NULL, 16);
    }
  else
    {
      return (UBYTE)strtol (val_str, NULL, 10);
    }
}
