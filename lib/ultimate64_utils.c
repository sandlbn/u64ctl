/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * Utility functions implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Debug printf function */
#ifdef DEBUG_BUILD
void
U64_DebugPrintf (CONST_STRPTR format, ...)
{
  va_list args;
  char buffer[256];

  va_start (args, format);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  /* Output to serial port if available */
  kprintf ("[U64] %s\n", buffer);

  /* Also output to console */
  printf ("[U64 DEBUG] %s\n", buffer);
}
#endif

/* Async support functions */
#ifdef U64_ASYNC_SUPPORT

/* Reset asynchronously */
LONG
U64_ResetAsync (U64Connection *conn, U64AsyncCallback callback, APTR userdata)
{
  if (!conn || !callback)
    {
      return U64_ERR_INVALID;
    }

  /* Check if async operation is already pending */
  if (conn->async_pending)
    {
      return U64_ERR_GENERAL;
    }

  /* Store callback and userdata */
  conn->async_callback = callback;
  conn->async_userdata = userdata;
  conn->async_pending = TRUE;

  /* TODO: Implement async operation using Amiga message ports */

  return U64_OK;
}

/* Load PRG asynchronously */
LONG
U64_LoadPRGAsync (U64Connection *conn, CONST UBYTE *data, ULONG size,
                  U64AsyncCallback callback, APTR userdata)
{
  if (!conn || !data || !callback)
    {
      return U64_ERR_INVALID;
    }

  /* Check if async operation is already pending */
  if (conn->async_pending)
    {
      return U64_ERR_GENERAL;
    }

  /* Store callback and userdata */
  conn->async_callback = callback;
  conn->async_userdata = userdata;
  conn->async_pending = TRUE;

  /* TODO: Implement async operation */

  return U64_OK;
}

/* Check async operation status */
BOOL
U64_CheckAsync (U64Connection *conn)
{
  struct Message *msg;

  if (!conn || !conn->async_pending)
    {
      return FALSE;
    }

  /* Check for reply message */
  if (conn->reply_port)
    {
      msg = GetMsg (conn->reply_port);
      if (msg)
        {
          /* Process completion */
          if (conn->async_callback)
            {
              conn->async_callback (conn, U64_OK, conn->async_userdata);
            }

          /* Clear async state */
          conn->async_pending = FALSE;
          conn->async_callback = NULL;
          conn->async_userdata = NULL;

          return TRUE;
        }
    }

  return FALSE;
}

/* Cancel async operation */
void
U64_CancelAsync (U64Connection *conn)
{
  if (!conn || !conn->async_pending)
    {
      return;
    }

  /* TODO: Cancel pending operation */

  /* Clear async state */
  conn->async_pending = FALSE;
  conn->async_callback = NULL;
  conn->async_userdata = NULL;
}

#endif /* U64_ASYNC_SUPPORT */

/* VIC Stream support */
#ifdef U64_VICSTREAM_SUPPORT

/* Capture VIC frame */
LONG
U64_CaptureFrame (U64Connection *conn, U64Frame *frame)
{
  /* TODO: Implement VIC stream capture */
  return U64_ERR_NOTIMPL;
}

/* Free captured frame */
void
U64_FreeFrame (U64Frame *frame)
{
  if (!frame)
    {
      return;
    }

  if (frame->data)
    {
      FreeMem (frame->data, frame->width * frame->height * 3);
      frame->data = NULL;
    }

  frame->width = 0;
  frame->height = 0;
}

/* Save frame as IFF file */
LONG
U64_SaveFrameIFF (U64Frame *frame, CONST_STRPTR filename)
{
  /* TODO: Implement IFF/ILBM saving */
  return U64_ERR_NOTIMPL;
}

#endif /* U64_VICSTREAM_SUPPORT */