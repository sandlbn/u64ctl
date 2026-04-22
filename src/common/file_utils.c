#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "file_utils.h"

UBYTE *
U64_ReadFile(CONST_STRPTR filename, ULONG *size)
{
  BPTR file;
  LONG file_size;
  UBYTE *buffer;

  file = Open((STRPTR)filename, MODE_OLDFILE);
  if (!file)
    return NULL;

  Seek(file, 0, OFFSET_END);
  file_size = Seek(file, 0, OFFSET_BEGINNING);

  if (file_size <= 0)
    {
      Close(file);
      return NULL;
    }

  buffer = AllocVec(file_size, MEMF_PUBLIC);
  if (!buffer)
    {
      Close(file);
      return NULL;
    }

  if (Read(file, buffer, file_size) != file_size)
    {
      FreeVec(buffer);
      Close(file);
      return NULL;
    }

  Close(file);

  if (size)
    *size = file_size;

  return buffer;
}
