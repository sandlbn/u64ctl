#include <dos/dos.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>

#include "env_utils.h"

STRPTR
U64_ReadEnvVar(CONST_STRPTR var_name)
{
  LONG result;
  STRPTR buffer;
  ULONG buffer_size = 256;

  buffer = AllocVec(buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!buffer)
    return NULL;

  result = GetVar((STRPTR)var_name, buffer, buffer_size, GVF_GLOBAL_ONLY);

  if (result > 0)
    {
      STRPTR final_buffer = AllocVec(result + 1, MEMF_PUBLIC);
      if (final_buffer)
        {
          strcpy(final_buffer, buffer);
          FreeVec(buffer);
          return final_buffer;
        }
    }

  FreeVec(buffer);
  return NULL;
}

BOOL
U64_WriteEnvVar(CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent)
{
  LONG result;
  ULONG flags = GVF_GLOBAL_ONLY;

  if (!var_name || !value)
    return FALSE;

  result = SetVar((STRPTR)var_name, (STRPTR)value, strlen(value), flags);

  if (result && persistent)
    {
      BPTR file;
      STRPTR envarc_path;
      ULONG path_len = strlen("ENVARC:") + strlen(var_name) + 1;

      envarc_path = AllocVec(path_len, MEMF_PUBLIC);
      if (envarc_path)
        {
          STRPTR dir_end;

          strcpy(envarc_path, "ENVARC:");
          strcat(envarc_path, var_name);

          dir_end = strrchr(envarc_path, '/');
          if (dir_end)
            {
              *dir_end = '\0';
              CreateDir(envarc_path);
              *dir_end = '/';
            }

          file = Open(envarc_path, MODE_NEWFILE);
          if (file)
            {
              Write(file, (STRPTR)value, strlen(value));
              Close(file);
            }

          FreeVec(envarc_path);
        }
    }

  return result ? TRUE : FALSE;
}
