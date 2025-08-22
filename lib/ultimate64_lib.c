/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * Main library implementation
 */

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Global library base pointers */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Global library base pointers */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Global verbose flag */
BOOL g_u64_verbose_mode = FALSE;

/* Set verbose mode for debugging */
void
U64_SetVerboseMode (BOOL verbose)
{
  g_u64_verbose_mode = verbose;
}

/* Debug printf function  */
void
U64_DebugPrintf (CONST_STRPTR format, ...)
{
  va_list args;
  char buffer[256];

  if (!g_u64_verbose_mode)
    {
      return; /* Don't print anything if verbose mode is off wtf */
    }

  va_start (args, format);
  vsnprintf (buffer, sizeof (buffer), format, args);
  va_end (args);

  /* Output to console with [DEBUG] prefix */
  printf ("[DEBUG] %s\n", buffer);
  fflush (stdout); /* Ensure immediate output */
}

/* Error string table */
static const char *error_strings[] = {
  "Success",                  /* U64_OK = 0 */
  "General error",            /* U64_ERR_GENERAL = -1 */
  "Memory allocation failed", /* U64_ERR_MEMORY = -2 */
  "Network error",            /* U64_ERR_NETWORK = -3 */
  "Not found",                /* U64_ERR_NOTFOUND = -4 */
  "Invalid parameter",        /* U64_ERR_INVALID = -5 */
  "Address overflow",         /* U64_ERR_OVERFLOW = -6 */
  "Access denied",            /* U64_ERR_ACCESS = -7 */
  "Not implemented",          /* U64_ERR_NOTIMPL = -8 */
  "Timeout"                   /* U64_ERR_TIMEOUT = -9 */
};

/* Initialize library */
BOOL
U64_InitLibrary (void)
{
  SysBase = *((struct ExecBase **)4);

  if (!(DOSBase = (struct DosLibrary *)OpenLibrary ("dos.library", 36L)))
    {
      return FALSE;
    }

  /* Initialize network subsystem */
  LONG net_result = U64_NetInit ();

  if (net_result != U64_OK)
    {
      U64_DEBUG ("Network initialization failed, continuing without network");
    }

  return TRUE;
}

/* Cleanup library  */
void
U64_CleanupLibrary (void)
{
  U64_DEBUG ("Starting minimal cleanup");

  /* Add delay to let operations complete */
  Delay (10); /* 200ms */

  /* Only cleanup network, leave DOS library open */
  U64_DEBUG ("Cleaning up network only");
  U64_NetCleanup ();

  /* DON'T close DOS library - let system handle it on exit */
  if (DOSBase)
    {
      U64_DEBUG ("Leaving DOS library open (system will close)");
      /* DOSBase = NULL; - don't even clear it */
    }

  U64_DEBUG ("Minimal cleanup complete");
}

/* Connect to Ultimate device */
U64Connection *
U64_Connect (CONST_STRPTR host, CONST_STRPTR password)
{
  U64Connection *conn;
  ULONG host_len, pw_len = 0;

  if (!host)
    {
      return NULL;
    }

  host_len = strlen ((char *)host);
  if (password)
    {
      pw_len = strlen ((char *)password);
    }

  /* Allocate connection structure */
  conn = AllocMem (sizeof (U64Connection), MEMF_PUBLIC | MEMF_CLEAR);
  if (!conn)
    {
      return NULL;
    }

  /* Allocate and copy host string */
  conn->host = AllocMem (host_len + 1, MEMF_PUBLIC);
  if (!conn->host)
    {
      FreeMem (conn, sizeof (U64Connection));
      return NULL;
    }
  strcpy ((char *)conn->host, (char *)host);

  /* Allocate and copy password if provided */
  if (password)
    {
      conn->password = AllocMem (pw_len + 1, MEMF_PUBLIC);
      if (!conn->password)
        {
          FreeMem (conn->host, host_len + 1);
          FreeMem (conn, sizeof (U64Connection));
          return NULL;
        }
      strcpy ((char *)conn->password, (char *)password);
    }

  /* Set default port */
  conn->port = 80;

  /* Build URL prefix */
  conn->url_prefix = AllocMem (256, MEMF_PUBLIC);
  if (conn->url_prefix)
    {
      sprintf ((char *)conn->url_prefix, "http://%s", (char *)conn->host);
    }

#ifdef U64_ASYNC_SUPPORT
  /* Create message port for async operations */
  conn->reply_port = CreateMsgPort ();
#endif

  conn->last_error = U64_OK;

  U64_DEBUG ("Created connection to %s", (char *)host);

  return conn;
}

/* Disconnect from Ultimate device */
void
U64_Disconnect (U64Connection *conn)
{
  if (!conn)
    {
      U64_DEBUG ("NULL connection");
      return;
    }

  U64_DEBUG ("Starting disconnect from %s",
             conn->host ? (char *)conn->host : "unknown");

  /* Disconnect network first */
  U64_NetDisconnect (conn);

#ifdef U64_ASYNC_SUPPORT
  if (conn->async_pending)
    {
      U64_DEBUG ("Cancelling async operation");
      U64_CancelAsync (conn);
    }
  if (conn->reply_port)
    {
      U64_DEBUG ("Deleting message port");
      DeleteMsgPort (conn->reply_port);
      conn->reply_port = NULL;
    }
#endif

  /* Free host string safely */
  if (conn->host)
    {
      ULONG host_len = strlen ((char *)conn->host);
      U64_DEBUG ("Freeing host string (%lu bytes)",
                 (unsigned long)host_len + 1);
      FreeMem (conn->host, host_len + 1);
      conn->host = NULL; /* Prevent double-free */
    }

  /* Free password string safely */
  if (conn->password)
    {
      ULONG pw_len = strlen ((char *)conn->password);
      U64_DEBUG ("Freeing password string (%lu bytes)",
                 (unsigned long)pw_len + 1);
      FreeMem (conn->password, pw_len + 1);
      conn->password = NULL; /* Prevent double-free */
    }

  /* Free URL prefix safely */
  if (conn->url_prefix)
    {
      U64_DEBUG ("Freeing URL prefix (256 bytes)");
      FreeMem (conn->url_prefix, 256);
      conn->url_prefix = NULL; /* Prevent double-free */
    }

  /* Clear the connection structure before freeing */
  U64_DEBUG ("Clearing connection structure");
  memset (conn, 0, sizeof (U64Connection));

  /* Free connection structure */
  U64_DEBUG ("Freeing connection structure");
  FreeMem (conn, sizeof (U64Connection));

  U64_DEBUG ("Disconnect complete");
}

/* Get last error code */
LONG
U64_GetLastError (U64Connection *conn)
{
  if (!conn)
    {
      return U64_ERR_INVALID;
    }
  return conn->last_error;
}

/* Get error string */
CONST_STRPTR
U64_GetErrorString (LONG error)
{
  LONG index = -error;

#ifdef DEBUG_BUILD
  printf ("[U64] GetErrorString: error=%ld, index=%ld\n", error, index);
#endif

  if (index < 0
      || index >= (LONG)(sizeof (error_strings) / sizeof (error_strings[0])))
    {
      return (CONST_STRPTR) "Unknown error";
    }

  return (CONST_STRPTR)error_strings[index];
}

/* Get device information */
LONG
U64_GetDeviceInfo (U64Connection *conn, U64DeviceInfo *info)
{
  HttpRequest req;
  LONG result;

  if (!conn || !info)
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Getting device info...");

  /* Clear info structure */
  memset (info, 0, sizeof (U64DeviceInfo));

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_GET;
  req.path = "/v1/info";

  /* Execute request */
  result = U64_HttpRequest (conn, &req);
  if (result != U64_OK)
    {
      conn->last_error = result;
      return result;
    }

  if (!req.response)
    {
      conn->last_error = U64_ERR_GENERAL;
      return U64_ERR_GENERAL;
    }

  U64_DEBUG ("Device info response: %s", req.response);

  /* Parse JSON response */
  result = U64_ParseDeviceInfo (req.response, info);

  /* Free response */
  if (req.response)
    {
      FreeMem (req.response, req.response_size + 1);
    }

  conn->last_error = result;
  return result;
}

/* Get version string */
CONST_STRPTR
U64_GetVersion (U64Connection *conn)
{
  HttpRequest req;
  LONG result;
  static char version[64];
  JsonParser parser;

  if (!conn)
    {
      return NULL;
    }

  U64_DEBUG ("Getting version...");

  /* Clear static buffer */
  memset (version, 0, sizeof (version));

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_GET;
  req.path = "/v1/version"; /* Correct API endpoint */

  /* Execute request */
  result = U64_HttpRequest (conn, &req);
  if (result != U64_OK)
    {
      conn->last_error = result;
      return NULL;
    }

  if (!req.response)
    {
      conn->last_error = U64_ERR_GENERAL;
      return NULL;
    }

  U64_DEBUG ("Version response: %.100s",
             req.response); /* Limit debug output */

  /* Parse JSON response to extract version field */
  if (U64_JsonInit (&parser, req.response))
    {
      if (U64_JsonFindKey (&parser, "version"))
        {
          if (U64_JsonGetString (&parser, version, sizeof (version)))
            {
              /* Success - Free response and return version */
              if (req.response)
                {
                  FreeMem (req.response, req.response_size + 1);
                }
              return (CONST_STRPTR)version;
            }
        }
    }

  /* copy entire response if JSON parsing fails */
  ULONG copy_size = req.response_size;
  if (copy_size >= sizeof (version))
    {
      copy_size = sizeof (version) - 1;
    }

  if (copy_size > 0)
    {
      CopyMem (req.response, version, copy_size);
      version[copy_size] = '\0';
    }
  else
    {
      strcpy (version, "unknown");
    }

  /* Free response */
  if (req.response)
    {
      FreeMem (req.response, req.response_size + 1);
    }

  return (CONST_STRPTR)version;
}

/* Helper function for simple machine commands */
static LONG
U64_SimpleMachineCommand (U64Connection *conn, CONST_STRPTR path)
{
  HttpRequest req;
  LONG result;
  STRPTR response_copy = NULL;

  if (!conn || !path)
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Executing machine command: %s", path);

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_PUT;
  req.path = (STRPTR)path;

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request result: %ld", result);
  U64_DEBUG ("HTTP status code: %d", req.status_code);

  /* SAFELY handle response - copy it before freeing */
  if (req.response && req.response_size > 0)
    {
      /* Make a safe copy for debugging */
      response_copy
          = AllocMem (req.response_size + 1, MEMF_PUBLIC | MEMF_CLEAR);
      if (response_copy)
        {
          CopyMem (req.response, response_copy, req.response_size);
          response_copy[req.response_size] = '\0';
          U64_DEBUG ("Response body: '%.100s'",
                     response_copy); /* Limit output length */
          FreeMem (response_copy, req.response_size + 1);
        }

      /* Free the original response */
      FreeMem (req.response, req.response_size + 1);
      req.response = NULL; /* Prevent double-free */
    }
  else
    {
      U64_DEBUG ("No response body");
    }

  /* Machine commands often return 204 No Content on success */
  if (result == U64_OK)
    {
      U64_DEBUG ("HTTP request succeeded, checking status code");
      if (req.status_code == 200 || req.status_code == 204)
        {
          U64_DEBUG ("Status code indicates success");
          conn->last_error = U64_OK;
          return U64_OK;
        }
      else
        {
          U64_DEBUG ("Unexpected status code: %d", req.status_code);
          /* For machine commands, many non-standard codes might still be OK,
           * for future */
          if (req.status_code >= 200 && req.status_code < 300)
            {
              U64_DEBUG ("2xx status code - treating as success");
              conn->last_error = U64_OK;
              return U64_OK;
            }
          else
            {
              U64_DEBUG ("Non-2xx status code - treating as error");
              conn->last_error = U64_ERR_GENERAL;
              return U64_ERR_GENERAL;
            }
        }
    }
  else
    {
      U64_DEBUG ("HTTP request failed with result: %ld", result);
      conn->last_error = result;
      return result;
    }
}

/* Reset machine */
LONG
U64_Reset (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:reset");
}

/* Reboot machine */
LONG
U64_Reboot (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:reboot");
}

/* Power off machine (U64 only) */
LONG
U64_PowerOff (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:poweroff");
}

/* Pause machine */
LONG
U64_Pause (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:pause");
}

/* Resume machine */
LONG
U64_Resume (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:resume");
}

/* Press menu button */
LONG
U64_MenuButton (U64Connection *conn)
{
  return U64_SimpleMachineCommand (conn, "/v1/machine:menu_button");
}

/* Write memory */
LONG
U64_WriteMem (U64Connection *conn, UWORD address, CONST UBYTE *data,
              UWORD length)
{
  HttpRequest req;
  LONG result;
  char *path_buffer;
  char *hex_buffer;
  char address_str[8];
  UWORD i;
  ULONG path_len;

  if (!conn || !data || length == 0)
    {
      U64_DEBUG ("Invalid parameters: conn=%p, data=%p, length=%d", conn, data,
                 length);
      return U64_ERR_INVALID;
    }

  /* Check for address overflow */
  if (U64_CheckAddressOverflow (address, length) != U64_OK)
    {
      conn->last_error = U64_ERR_OVERFLOW;
      return conn->last_error;
    }

  /* Maximum 128 bytes as per API documentation */
  if (length > 128)
    {
      conn->last_error = U64_ERR_INVALID;
      return conn->last_error;
    }

  U64_DEBUG ("=== SAFE WriteMem Implementation ===");
  U64_DEBUG ("Writing %d bytes to $%04X", length, address);
  U64_DEBUG ("Input data[0] = 0x%02X", data[0]);

  /* Allocate separate buffers to avoid corruption */
  hex_buffer = AllocMem (512, MEMF_PUBLIC | MEMF_CLEAR);
  if (!hex_buffer)
    {
      U64_DEBUG ("Failed to allocate hex buffer");
      return U64_ERR_MEMORY;
    }

  path_buffer = AllocMem (1024, MEMF_PUBLIC | MEMF_CLEAR);
  if (!path_buffer)
    {
      U64_DEBUG ("Failed to allocate path buffer");
      FreeMem (hex_buffer, 512);
      return U64_ERR_MEMORY;
    }

  /* Build hex string character by character */
  hex_buffer[0] = '\0';
  for (i = 0; i < length; i++)
    {
      char byte_str[4];
      UBYTE byte_val = data[i];

      U64_DEBUG ("Processing byte %d: 0x%02X", i, byte_val);

      /* Convert byte to hex manually to avoid sprintf issues */
      byte_str[0] = "0123456789ABCDEF"[(byte_val >> 4) & 0x0F];
      byte_str[1] = "0123456789ABCDEF"[byte_val & 0x0F];
      byte_str[2] = '\0';

      strcat (hex_buffer, byte_str);

      U64_DEBUG ("After byte %d, hex_buffer = '%s'", i, hex_buffer);
    }

  U64_DEBUG ("Complete hex string: '%s' (length: %lu)", hex_buffer,
             (unsigned long)strlen (hex_buffer));

  /* Build address string manually */
  address_str[0] = "0123456789ABCDEF"[(address >> 12) & 0x0F];
  address_str[1] = "0123456789ABCDEF"[(address >> 8) & 0x0F];
  address_str[2] = "0123456789ABCDEF"[(address >> 4) & 0x0F];
  address_str[3] = "0123456789ABCDEF"[address & 0x0F];
  address_str[4] = '\0';

  U64_DEBUG ("Address string: '%s'", address_str);

  /* Build path string piece by piece to avoid sprintf corruption */
  strcpy (path_buffer, "/v1/machine:writemem?address=");
  strcat (path_buffer, address_str);
  strcat (path_buffer, "&data=");
  strcat (path_buffer, hex_buffer);

  U64_DEBUG ("Final path: '%s'", path_buffer);
  U64_DEBUG ("Path length: %lu", (unsigned long)strlen (path_buffer));

  /* Verify the path contains what we expect */
  if (!strstr (path_buffer, "address="))
    {
      U64_DEBUG ("ERROR: No address parameter in path!");
      FreeMem (hex_buffer, 512);
      FreeMem (path_buffer, 1024);
      return U64_ERR_GENERAL;
    }

  if (!strstr (path_buffer, "data="))
    {
      U64_DEBUG ("ERROR: No data parameter in path!");
      FreeMem (hex_buffer, 512);
      FreeMem (path_buffer, 1024);
      return U64_ERR_GENERAL;
    }

  char *data_pos = strstr (path_buffer, "data=");
  if (data_pos)
    {
      U64_DEBUG ("Data parameter found: '%.20s'", data_pos);
      if (strlen (data_pos) <= 5)
        { /* "data=" is 5 chars */
          U64_DEBUG ("ERROR: Data parameter is empty!");
          FreeMem (hex_buffer, 512);
          FreeMem (path_buffer, 1024);
          return U64_ERR_GENERAL;
        }
    }

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_PUT;
  req.path = path_buffer; /* Use our safely built path */
  req.content_type = NULL;
  req.body = NULL;
  req.body_size = 0;

  /* Execute request */
  U64_DEBUG ("Executing HTTP request with safe path...");
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP result: %ld", result);
  U64_DEBUG ("HTTP status: %d", req.status_code);

  /* Process response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response: %s", req.response);

      /* Check for success response */
      if (strstr (req.response, "\"errors\"") && strstr (req.response, "[]"))
        {
          U64_DEBUG ("SUCCESS: Empty errors array detected");
          FreeMem (req.response, req.response_size + 1);
          FreeMem (hex_buffer, 512);
          FreeMem (path_buffer, 1024);
          conn->last_error = U64_OK;
          return U64_OK;
        }

      /* Check for the "write at least one byte" error */
      if (strstr (req.response, "write at least one byte"))
        {
          U64_DEBUG ("ERROR: Ultimate64 still thinks we're sending 0 bytes");
          U64_DEBUG (
              "This means our data parameter is not reaching the server");
        }

      FreeMem (req.response, req.response_size + 1);
    }

  /* Check HTTP status code */
  if (result == U64_OK && req.status_code == 200)
    {
      U64_DEBUG ("HTTP 200 OK - treating as success despite response issues");
      FreeMem (hex_buffer, 512);
      FreeMem (path_buffer, 1024);
      conn->last_error = U64_OK;
      return U64_OK;
    }

  /* Cleanup and return error */
  FreeMem (hex_buffer, 512);
  FreeMem (path_buffer, 1024);

  U64_DEBUG ("Request failed - result: %ld, status: %d", result,
             req.status_code);
  conn->last_error = (result != U64_OK) ? result : U64_ERR_GENERAL;
  return conn->last_error;
}

/* Read memory */
LONG
U64_ReadMem (U64Connection *conn, UWORD address, UBYTE *buffer, UWORD length)
{
  HttpRequest req;
  LONG result;
  char path[256];

  if (!conn || !buffer)
    {
      return U64_ERR_INVALID;
    }

  /* Check for address overflow */
  if (U64_CheckAddressOverflow (address, length) != U64_OK)
    {
      conn->last_error = U64_ERR_OVERFLOW;
      return conn->last_error;
    }

  U64_DEBUG ("Reading %d bytes from $%04X", length, address);

  /* Build path with query parameters */
  sprintf (path, "/v1/machine:readmem?address=%04X&length=%d", address,
           length);

  U64_DEBUG ("Memory read path: %s", path);

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_GET;
  req.path = path;

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("Memory read result: %ld", result);
  U64_DEBUG ("HTTP status: %d", req.status_code);

  if (result == U64_OK && req.response)
    {
      U64_DEBUG ("Response size: %lu", (unsigned long)req.response_size);

      /* Copy binary data to buffer */
      if (req.response_size >= length)
        {
          memcpy (buffer, req.response, length);
          U64_DEBUG ("Read %d bytes successfully", length);

          /* Debug: show first few bytes */
          if (length > 0)
            {
              U64_DEBUG ("First byte: $%02X", buffer[0]);
            }
          if (length > 1)
            {
              U64_DEBUG ("Second byte: $%02X", buffer[1]);
            }
        }
      else
        {
          U64_DEBUG ("Received %lu bytes, expected %d",
                     (unsigned long)req.response_size, length);
          result = U64_ERR_GENERAL;
        }
    }

  /* Free response */
  if (req.response)
    {
      FreeMem (req.response, req.response_size + 1);
    }

  conn->last_error = result;
  return result;
}

/* Poke single byte */
LONG
U64_Poke (U64Connection *conn, UWORD address, UBYTE value)
{
  U64_DEBUG ("Poking $%02X to $%04X", value, address);
  return U64_WriteMem (conn, address, &value, 1);
}

/* Peek single byte - FIXED VERSION */
UBYTE
U64_Peek (U64Connection *conn, UWORD address)
{
  UBYTE value = 0;
  LONG result;

  U64_DEBUG ("Peeking from $%04X", address);

  result = U64_ReadMem (conn, address, &value, 1);
  if (result == U64_OK)
    {
      U64_DEBUG ("Peeked $%02X from $%04X", value, address);
    }
  else
    {
      U64_DEBUG ("Peek failed: %s", U64_GetErrorString (result));
      /* Store error in connection for later retrieval */
      conn->last_error = result;
    }

  return value;
}

/* Read word (little-endian) */
UWORD
U64_ReadWord (U64Connection *conn, UWORD address)
{
  UBYTE bytes[2] = { 0, 0 };
  UWORD value;

  U64_DEBUG ("Reading word from $%04X", address);

  if (U64_ReadMem (conn, address, bytes, 2) == U64_OK)
    {
      /* Convert from little-endian */
      value = bytes[0] | (bytes[1] << 8);
      U64_DEBUG ("Read word $%04X from $%04X", value, address);
      return value;
    }

  U64_DEBUG ("Word read failed from $%04X", address);
  return 0;
}

LONG
U64_TypeText (U64Connection *conn, CONST_STRPTR text)
{
  UBYTE *petscii;
  ULONG petscii_len;
  ULONG chunk_size;
  ULONG offset;
  LONG result;

  if (!conn || !text)
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Typing text: %s", (char *)text);

  /* Check if BASIC is ready */
  if (!U64_IsBasicReady (conn))
    {
      conn->last_error = U64_ERR_GENERAL;
      return conn->last_error;
    }

  /* Convert to PETSCII */
  petscii = U64_StringToPETSCII (text, &petscii_len);
  if (!petscii)
    {
      conn->last_error = U64_ERR_MEMORY;
      return conn->last_error;
    }

  /* Send in chunks of 10 characters (C64 keyboard buffer limit) */
  offset = 0;
  while (offset < petscii_len)
    {
      UBYTE clear[2] = { 0, 0 };

      chunk_size = petscii_len - offset;
      if (chunk_size > 10)
        {
          chunk_size = 10;
        }

      /* Clear keyboard buffer */
      U64_WriteMem (conn, U64_KEYBOARD_LSTX, clear, 2);

      /* Write PETSCII to buffer */
      U64_WriteMem (conn, U64_KEYBOARD_BUFFER, &petscii[offset], chunk_size);

      /* Trigger typing */
      clear[0] = chunk_size;
      U64_WriteMem (conn, U64_KEYBOARD_NDX, clear, 1);

      /* Small delay for C64 to process */
      Delay (1); /* 20ms */

      offset += chunk_size;
    }

  U64_FreePETSCII (petscii);

  conn->last_error = U64_OK;
  return U64_OK;
}

/* Check if BASIC is ready */
BOOL
U64_IsBasicReady (U64Connection *conn)
{
  /* For now, always return TRUE */
  /* TODO: Implement proper check by reading system vector at 0x0302 */
  return TRUE;
}

/* Utility: Check address overflow */
LONG
U64_CheckAddressOverflow (UWORD address, UWORD length)
{
  ULONG sum;

  if (length == 0)
    {
      return U64_OK;
    }

  sum = (ULONG)address + (ULONG)length - 1;

  if (sum > 0xFFFF)
    {
      return U64_ERR_OVERFLOW;
    }

  return U64_OK;
}

/* Utility: Extract load address from PRG data */
UWORD
U64_ExtractLoadAddress (CONST UBYTE *data, ULONG size)
{
  UWORD address;

  if (!data || size < 2)
    {
      return 0;
    }

  /* Little-endian format */
  address = data[0] | (data[1] << 8);

  return address;
}

/* Get disk type from file extension */
U64DiskImageType
U64_GetDiskTypeFromExt (CONST_STRPTR filename)
{
  CONST_STRPTR ext;

  if (!filename)
    {
      return U64_DISK_D64;
    }

  ext = strrchr (filename, '.');
  if (!ext)
    {
      return U64_DISK_D64;
    }

  ext++; /* Skip the dot */

  if (stricmp (ext, "d64") == 0)
    return U64_DISK_D64;
  if (stricmp (ext, "g64") == 0)
    return U64_DISK_G64;
  if (stricmp (ext, "d71") == 0)
    return U64_DISK_D71;
  if (stricmp (ext, "g71") == 0)
    return U64_DISK_G71;
  if (stricmp (ext, "d81") == 0)
    return U64_DISK_D81;

  return U64_DISK_D64; /* Default */
}

/* Get disk type string */
CONST_STRPTR
U64_GetDiskTypeString (U64DiskImageType type)
{
  static const char *disk_types[] = { "d64", "g64", "d71", "g71", "d81" };

  if (type < 0 || type >= (sizeof (disk_types) / sizeof (disk_types[0])))
    {
      return "unknown";
    }

  return disk_types[type];
}

/* Get drive type string */
CONST_STRPTR
U64_GetDriveTypeString (U64DriveType type)
{
  static const char *drive_types[] = { "1541", "1571", "1581", "DOS" };

  if (type < 0 || type >= (sizeof (drive_types) / sizeof (drive_types[0])))
    {
      return "unknown";
    }

  return drive_types[type];
}

/* Validate MOD file format */
LONG
U64_ValidateMODFile (CONST UBYTE *data, ULONG size, STRPTR *validation_info)
{
  char *info;
  char format_id[5];
  ULONG i;
  BOOL valid_mod = FALSE;

  if (!data || size < 1084)
    { /* Minimum size for MOD header */
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "File too small: %lu bytes (minimum 1084)",
                       (unsigned long)size);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Check for MOD format identifier at offset 1080 */
  CopyMem ((APTR)&data[1080], format_id, 4);
  format_id[4] = '\0';

  /* Common MOD format identifiers */
  const char *mod_formats[]
      = { "M.K.", "M!K!", "4CHN", "6CHN", "8CHN", "FLT4", "FLT8",
          "2CHN", "3CHN", "5CHN", "7CHN", "9CHN", "10CH", "11CH",
          "12CH", "13CH", "14CH", "15CH", "16CH", "18CH", "20CH",
          "22CH", "24CH", "26CH", "28CH", "30CH", "32CH", NULL };

  for (i = 0; mod_formats[i] != NULL; i++)
    {
      if (strncmp (format_id, mod_formats[i], 4) == 0)
        {
          valid_mod = TRUE;
          break;
        }
    }

  /* Also check for classic ProTracker (no format ID) */
  if (!valid_mod)
    {
      /* For classic MODs, check if the data looks reasonable */
      BOOL looks_like_mod = TRUE;
      /* Check song length at offset 950 */
      if (data[950] > 128)
        looks_like_mod = FALSE;
      /* Check restart position at offset 951 */
      if (data[951] > 127)
        looks_like_mod = FALSE;

      if (looks_like_mod)
        {
          strcpy (format_id, "ORIG"); /* Original ProTracker format */
          valid_mod = TRUE;
        }
    }

  if (!valid_mod)
    {
      if (validation_info)
        {
          info = AllocMem (128, MEMF_PUBLIC);
          if (info)
            {
              sprintf (
                  info,
                  "Invalid MOD format ID: '%.4s' (expected M.K., 4CHN, etc.)",
                  format_id);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Extract song title from first 20 bytes */
  if (validation_info)
    {
      char title[21];
      CopyMem ((APTR)data, title, 20);
      title[20] = '\0';

      /* Remove trailing spaces and null chars */
      for (i = 19; i > 0 && (title[i] == ' ' || title[i] == '\0'); i--)
        {
          title[i] = '\0';
        }

      info = AllocMem (256, MEMF_PUBLIC);
      if (info)
        {
          sprintf (info, "MOD format: %s, title: '%.20s', size: %lu bytes",
                   format_id, title, (unsigned long)size);
          *validation_info = info;
        }
    }

  return U64_OK;
}

/* Enhanced PlayMOD function*/
LONG
U64_PlayMOD (U64Connection *conn, CONST UBYTE *data, ULONG size,
             STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== PlayMOD Start ===");
  U64_DEBUG ("File size: %lu bytes", (unsigned long)size);

  /* Validate MOD file header */
  if (size < 1084)
    {
      U64_DEBUG ("File too small to be valid MOD: %lu bytes",
                 (unsigned long)size);
      return U64_ERR_INVALID;
    }

  /* Check MOD format identifier */
  char format_check[5];
  CopyMem ((APTR)&data[1080], format_check, 4);
  format_check[4] = '\0';
  U64_DEBUG ("MOD format ID at offset 1080: '%.4s'", format_check);

  /* Extract and show title */
  char title[21];
  CopyMem ((APTR)data, title, 20);
  title[20] = '\0';
  U64_DEBUG ("MOD title: '%.20s'", title);

  /* Build path for MOD player endpoint */
  strcpy (path, "/v1/runners:modplay");

  U64_DEBUG ("Request path: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = (UBYTE *)data;
  req.body_size = size;

  U64_DEBUG ("Sending HTTP request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'", req.response);

      /* Check for JSON response */
      if (strstr (req.response, "{"))
        {
          U64_DEBUG ("Response appears to be JSON");

          /* Parse error array from JSON response */
          memset (&error_array, 0, sizeof (error_array));
          LONG parse_result = U64_ParseErrorArray (req.response, &error_array);
          U64_DEBUG ("JSON parsing result: %ld", parse_result);

          if (parse_result == U64_OK)
            {
              U64_DEBUG ("Successfully parsed JSON, found %lu errors",
                         (unsigned long)error_array.error_count);

              if (error_array.error_count > 0)
                {
                  U64_DEBUG ("=== MOD ERROR DETAILS ===");
                  for (ULONG i = 0; i < error_array.error_count; i++)
                    {
                      if (error_array.errors[i])
                        {
                          U64_DEBUG ("Error %lu: '%s'", (unsigned long)i,
                                     error_array.errors[i]);
                        }
                    }

                  /* Format error details for caller */
                  if (error_details)
                    {
                      *error_details = U64_FormatErrorArray (&error_array);
                      U64_DEBUG ("Formatted error string: '%s'",
                                 *error_details);
                    }

                  /* Free error array */
                  U64_FreeErrorArray (&error_array);

                  /* Free response */
                  FreeMem (req.response, req.response_size + 1);

                  /* Set specific error */
                  conn->last_error = U64_ERR_GENERAL;
                  U64_DEBUG ("=== PlayMOD Result: FAILED (errors found) ===");
                  return U64_ERR_GENERAL;
                }
              else
                {
                  U64_DEBUG ("No errors found in JSON - SUCCESS");
                  /* Success - empty errors array */
                  U64_FreeErrorArray (&error_array);
                  FreeMem (req.response, req.response_size + 1);
                  conn->last_error = U64_OK;
                  U64_DEBUG ("=== PlayMOD Result: SUCCESS ===");
                  return U64_OK;
                }
            }
          else
            {
              U64_DEBUG ("Failed to parse JSON: %ld", parse_result);
            }
        }
      else
        {
          U64_DEBUG (
              "Response is not JSON, checking for text success indicators");

          /* Check for plain text success */
          if (strstr (req.response, "success") || strstr (req.response, "ok")
              || strstr (req.response, "OK") || req.response_size < 10)
            {
              U64_DEBUG ("Found success indicator in response");
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_OK;
              U64_DEBUG ("=== PlayMOD Result: SUCCESS (text) ===");
              return U64_OK;
            }
        }

      FreeMem (req.response, req.response_size + 1);
    }
  else
    {
      U64_DEBUG ("No response body received");
    }

  /* Final HTTP status analysis */
  U64_DEBUG ("=== Final Status Check ===");
  if (result == U64_OK)
    {
      U64_DEBUG ("HTTP layer succeeded");
      switch (req.status_code)
        {
        case 200:
          U64_DEBUG ("HTTP 200 OK - SUCCESS");
          conn->last_error = U64_OK;
          U64_DEBUG ("=== PlayMOD Result: SUCCESS (HTTP 200) ===");
          return U64_OK;
        case 204:
          U64_DEBUG ("HTTP 204 No Content - SUCCESS");
          conn->last_error = U64_OK;
          U64_DEBUG ("=== PlayMOD Result: SUCCESS (HTTP 204) ===");
          return U64_OK;
        case 404:
          U64_DEBUG ("HTTP 404 Not Found - endpoint doesn't exist");
          conn->last_error = U64_ERR_NOTFOUND;
          U64_DEBUG ("=== PlayMOD Result: FAILED (404) ===");
          return U64_ERR_NOTFOUND;
        case 400:
          U64_DEBUG ("HTTP 400 Bad Request - invalid data or format");
          conn->last_error = U64_ERR_INVALID;
          U64_DEBUG ("=== PlayMOD Result: FAILED (400) ===");
          return U64_ERR_INVALID;
        case 500:
          U64_DEBUG ("HTTP 500 Internal Server Error - Ultimate64 error");
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== PlayMOD Result: FAILED (500) ===");
          return U64_ERR_GENERAL;
        default:
          U64_DEBUG ("HTTP %d - treating as error", req.status_code);
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== PlayMOD Result: FAILED (HTTP %d) ===",
                     req.status_code);
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      U64_DEBUG ("HTTP layer failed: %ld (%s)", result,
                 U64_GetErrorString (result));
      conn->last_error = result;
      U64_DEBUG ("=== PlayMOD Result: FAILED (HTTP layer) ===");
      return result;
    }
}
LONG
U64_ValidateCRTFile (CONST UBYTE *data, ULONG size, STRPTR *validation_info)
{
  char *info;
  UWORD header_length, version, cart_type;
  UBYTE exrom, game;
  char name[33];
  ULONG i;

  if (!data || size < 64)
    { /* Minimum CRT header size */
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "File too small: %lu bytes (minimum 64)",
                       (unsigned long)size);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Check CRT signature "C64 CARTRIDGE   " */
  if (strncmp ((char *)data, "C64 CARTRIDGE   ", 16) != 0)
    {
      if (validation_info)
        {
          info = AllocMem (128, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info,
                       "Invalid CRT signature: '%.16s' (expected 'C64 "
                       "CARTRIDGE   ')",
                       (char *)data);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Extract header information (big-endian) */
  header_length = (data[16] << 8) | data[17];
  version = (data[18] << 8) | data[19];
  cart_type = (data[20] << 8) | data[21];
  exrom = data[22];
  game = data[23];

  /* Extract cartridge name (32 bytes, null-terminated) */
  CopyMem ((APTR)&data[32], name, 32);
  name[32] = '\0';

  /* Remove trailing spaces and nulls */
  for (i = 31; i > 0 && (name[i] == ' ' || name[i] == '\0'); i--)
    {
      name[i] = '\0';
    }

  if (validation_info)
    {
      info = AllocMem (256, MEMF_PUBLIC);
      if (info)
        {
          sprintf (info,
                   "CRT v%d.%d, type %d, EXROM=%d, GAME=%d, name: '%.32s', "
                   "size: %lu bytes",
                   version >> 8, version & 0xFF, cart_type, exrom, game, name,
                   (unsigned long)size);
          *validation_info = info;
        }
    }

  return U64_OK;
}

/* Validate PRG file format */
LONG
U64_ValidatePRGFile (CONST UBYTE *data, ULONG size, STRPTR *validation_info)
{
  char *info;
  UWORD load_address;
  ULONG data_size;

  if (!data || size < 2)
    { /* Minimum PRG size: 2 bytes for load address */
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "File too small: %lu bytes (minimum 2)",
                       (unsigned long)size);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Extract load address (little-endian) */
  load_address = data[0] | (data[1] << 8);
  data_size = size - 2;

  /* Basic sanity checks */
  if (load_address < 0x0200)
    {
      if (validation_info)
        {
          info = AllocMem (128, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info,
                       "Unusual load address: $%04X (typically >= $0200)",
                       load_address);
              *validation_info = info;
            }
        }
      /* Don't fail, just warn */
    }

  if (validation_info && (!info || load_address >= 0x0200))
    {
      info = AllocMem (256, MEMF_PUBLIC);
      if (info)
        {
          UWORD end_address = load_address + data_size - 1;
          const char *type_guess = "Unknown";

          /* Guess program type based on load address */
          if (load_address == 0x0801)
            {
              type_guess = "BASIC program";
            }
          else if (load_address >= 0x0200 && load_address < 0x0400)
            {
              type_guess = "Low memory program";
            }
          else if (load_address >= 0x0800 && load_address < 0x1000)
            {
              type_guess = "BASIC/Low program";
            }
          else if (load_address >= 0x1000 && load_address < 0x8000)
            {
              type_guess = "Machine language";
            }
          else if (load_address >= 0xC000)
            {
              type_guess = "High memory/ROM";
            }

          sprintf (info, "PRG load: $%04X-$%04X (%lu bytes), type: %s",
                   load_address, end_address, (unsigned long)data_size,
                   type_guess);
          *validation_info = info;
        }
    }

  return U64_OK;
}

/*  LoadPRG function */
LONG
U64_LoadPRG (U64Connection *conn, CONST UBYTE *data, ULONG size,
             STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== LoadPRG Start ===");
  U64_DEBUG ("File size: %lu bytes", (unsigned long)size);

  /* Validate PRG file */
  if (size < 2)
    {
      U64_DEBUG ("File too small to be valid PRG: %lu bytes",
                 (unsigned long)size);
      return U64_ERR_INVALID;
    }

  /* Extract and show load address */
  UWORD load_address = data[0] | (data[1] << 8);
  U64_DEBUG ("PRG load address: $%04X", load_address);
  U64_DEBUG ("Data size: %lu bytes", (unsigned long)(size - 2));

  /* Build path for PRG loader endpoint */
  strcpy (path, "/v1/runners:load_prg");

  U64_DEBUG ("Request path: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = (UBYTE *)data;
  req.body_size = size;

  U64_DEBUG ("Sending HTTP request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'", req.response);

      /* Check for JSON response */
      if (strstr (req.response, "{"))
        {
          U64_DEBUG ("Response appears to be JSON");

          /* Parse error array from JSON response */
          memset (&error_array, 0, sizeof (error_array));
          LONG parse_result = U64_ParseErrorArray (req.response, &error_array);
          U64_DEBUG ("JSON parsing result: %ld", parse_result);

          if (parse_result == U64_OK)
            {
              U64_DEBUG ("Successfully parsed JSON, found %lu errors",
                         (unsigned long)error_array.error_count);

              if (error_array.error_count > 0)
                {
                  U64_DEBUG ("=== PRG LOAD ERROR DETAILS ===");
                  for (ULONG i = 0; i < error_array.error_count; i++)
                    {
                      if (error_array.errors[i])
                        {
                          U64_DEBUG ("Error %lu: '%s'", (unsigned long)i,
                                     error_array.errors[i]);
                        }
                    }

                  /* Format error details for caller */
                  if (error_details)
                    {
                      *error_details = U64_FormatErrorArray (&error_array);
                      U64_DEBUG ("Formatted error string: '%s'",
                                 *error_details);
                    }

                  /* Free error array */
                  U64_FreeErrorArray (&error_array);

                  /* Free response */
                  FreeMem (req.response, req.response_size + 1);

                  /* Set specific error */
                  conn->last_error = U64_ERR_GENERAL;
                  U64_DEBUG ("=== LoadPRG Result: FAILED (errors found) ===");
                  return U64_ERR_GENERAL;
                }
              else
                {
                  U64_DEBUG ("No errors found in JSON - SUCCESS");
                  /* Success - empty errors array */
                  U64_FreeErrorArray (&error_array);
                  FreeMem (req.response, req.response_size + 1);
                  conn->last_error = U64_OK;
                  U64_DEBUG ("=== LoadPRG Result: SUCCESS ===");
                  return U64_OK;
                }
            }
          else
            {
              U64_DEBUG ("Failed to parse JSON: %ld", parse_result);
            }
        }
      else
        {
          U64_DEBUG (
              "Response is not JSON, checking for text success indicators");

          /* Check for plain text success */
          if (strstr (req.response, "success") || strstr (req.response, "ok")
              || strstr (req.response, "OK") || req.response_size < 10)
            {
              U64_DEBUG ("Found success indicator in response");
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_OK;
              U64_DEBUG ("=== LoadPRG Result: SUCCESS (text) ===");
              return U64_OK;
            }
        }

      FreeMem (req.response, req.response_size + 1);
    }
  else
    {
      U64_DEBUG ("No response body received");
    }

  /* Final HTTP status analysis */
  U64_DEBUG ("=== Final Status Check ===");
  if (result == U64_OK)
    {
      U64_DEBUG ("HTTP layer succeeded");
      switch (req.status_code)
        {
        case 200:
        case 204:
          U64_DEBUG ("HTTP %d - SUCCESS", req.status_code);
          conn->last_error = U64_OK;
          U64_DEBUG ("=== LoadPRG Result: SUCCESS (HTTP %d) ===",
                     req.status_code);
          return U64_OK;
        case 404:
          U64_DEBUG ("HTTP 404 Not Found - endpoint doesn't exist");
          conn->last_error = U64_ERR_NOTFOUND;
          U64_DEBUG ("=== LoadPRG Result: FAILED (404) ===");
          return U64_ERR_NOTFOUND;
        case 400:
          U64_DEBUG ("HTTP 400 Bad Request - invalid data or format");
          conn->last_error = U64_ERR_INVALID;
          U64_DEBUG ("=== LoadPRG Result: FAILED (400) ===");
          return U64_ERR_INVALID;
        default:
          U64_DEBUG ("HTTP %d - treating as error", req.status_code);
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== LoadPRG Result: FAILED (HTTP %d) ===",
                     req.status_code);
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      U64_DEBUG ("HTTP layer failed: %ld (%s)", result,
                 U64_GetErrorString (result));
      conn->last_error = result;
      U64_DEBUG ("=== LoadPRG Result: FAILED (HTTP layer) ===");
      return result;
    }
}

/*  RunPRG function  */
LONG
U64_RunPRG (U64Connection *conn, CONST UBYTE *data, ULONG size,
            STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== RunPRG Start ===");
  U64_DEBUG ("File size: %lu bytes", (unsigned long)size);

  /* Validate PRG file */
  if (size < 2)
    {
      U64_DEBUG ("File too small to be valid PRG: %lu bytes",
                 (unsigned long)size);
      return U64_ERR_INVALID;
    }

  /* Extract and show load address */
  UWORD load_address = data[0] | (data[1] << 8);
  U64_DEBUG ("PRG load address: $%04X", load_address);
  U64_DEBUG ("Data size: %lu bytes", (unsigned long)(size - 2));

  /* Build path for PRG runner endpoint */
  strcpy (path, "/v1/runners:run_prg");

  U64_DEBUG ("Request path: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = (UBYTE *)data;
  req.body_size = size;

  U64_DEBUG ("Sending HTTP request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response (same pattern as LoadPRG) */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'", req.response);

      /* Check for JSON response */
      if (strstr (req.response, "{"))
        {
          U64_DEBUG ("Response appears to be JSON");

          /* Parse error array from JSON response */
          memset (&error_array, 0, sizeof (error_array));
          LONG parse_result = U64_ParseErrorArray (req.response, &error_array);

          if (parse_result == U64_OK && error_array.error_count > 0)
            {
              U64_DEBUG ("=== PRG RUN ERROR DETAILS ===");
              for (ULONG i = 0; i < error_array.error_count; i++)
                {
                  if (error_array.errors[i])
                    {
                      U64_DEBUG ("Error %lu: '%s'", (unsigned long)i,
                                 error_array.errors[i]);
                    }
                }

              if (error_details)
                {
                  *error_details = U64_FormatErrorArray (&error_array);
                }

              U64_FreeErrorArray (&error_array);
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_ERR_GENERAL;
              return U64_ERR_GENERAL;
            }
          else
            {
              U64_DEBUG ("No errors found in JSON - SUCCESS");
              U64_FreeErrorArray (&error_array);
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_OK;
              return U64_OK;
            }
        }

      FreeMem (req.response, req.response_size + 1);
    }

  /* Final HTTP status analysis */
  if (result == U64_OK)
    {
      switch (req.status_code)
        {
        case 200:
        case 204:
          conn->last_error = U64_OK;
          U64_DEBUG ("=== RunPRG Result: SUCCESS ===");
          return U64_OK;
        case 404:
          conn->last_error = U64_ERR_NOTFOUND;
          return U64_ERR_NOTFOUND;
        case 400:
          conn->last_error = U64_ERR_INVALID;
          return U64_ERR_INVALID;
        default:
          conn->last_error = U64_ERR_GENERAL;
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      conn->last_error = result;
      return result;
    }
}

/* RunCRT function */
LONG
U64_RunCRT (U64Connection *conn, CONST UBYTE *data, ULONG size,
            STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== RunCRT Start ===");
  U64_DEBUG ("File size: %lu bytes", (unsigned long)size);

  /* Validate CRT file */
  if (size < 64)
    {
      U64_DEBUG ("File too small to be valid CRT: %lu bytes",
                 (unsigned long)size);
      return U64_ERR_INVALID;
    }

  /* Check CRT signature */
  if (strncmp ((char *)data, "C64 CARTRIDGE   ", 16) == 0)
    {
      UWORD cart_type = (data[20] << 8) | data[21];
      U64_DEBUG ("Valid CRT file detected, type: %d", cart_type);
    }
  else
    {
      U64_DEBUG ("WARNING: No valid CRT signature found");
      U64_DEBUG ("First 16 bytes: '%.16s'", (char *)data);
    }

  /* Build path for CRT runner endpoint */
  strcpy (path, "/v1/runners:run_crt");

  U64_DEBUG ("Request path: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = (UBYTE *)data;
  req.body_size = size;

  U64_DEBUG ("Sending HTTP request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response (same pattern as other functions) */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'", req.response);

      if (strstr (req.response, "{"))
        {
          memset (&error_array, 0, sizeof (error_array));
          LONG parse_result = U64_ParseErrorArray (req.response, &error_array);

          if (parse_result == U64_OK && error_array.error_count > 0)
            {
              U64_DEBUG ("=== CRT RUN ERROR DETAILS ===");
              for (ULONG i = 0; i < error_array.error_count; i++)
                {
                  if (error_array.errors[i])
                    {
                      U64_DEBUG ("Error %lu: '%s'", (unsigned long)i,
                                 error_array.errors[i]);
                    }
                }

              if (error_details)
                {
                  *error_details = U64_FormatErrorArray (&error_array);
                }

              U64_FreeErrorArray (&error_array);
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_ERR_GENERAL;
              return U64_ERR_GENERAL;
            }
          else
            {
              U64_DEBUG ("No errors found in JSON - SUCCESS");
              U64_FreeErrorArray (&error_array);
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_OK;
              return U64_OK;
            }
        }

      FreeMem (req.response, req.response_size + 1);
    }

  /* Final HTTP status analysis */
  if (result == U64_OK)
    {
      switch (req.status_code)
        {
        case 200:
        case 204:
          conn->last_error = U64_OK;
          U64_DEBUG ("=== RunCRT Result: SUCCESS ===");
          return U64_OK;
        case 404:
          conn->last_error = U64_ERR_NOTFOUND;
          return U64_ERR_NOTFOUND;
        case 400:
          conn->last_error = U64_ERR_INVALID;
          return U64_ERR_INVALID;
        default:
          conn->last_error = U64_ERR_GENERAL;
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      conn->last_error = result;
      return result;
    }
}

LONG
U64_PlaySID (U64Connection *conn, CONST UBYTE *data, ULONG size,
             UBYTE song_num, STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== PlaySID Start ===");
  U64_DEBUG ("File size: %lu bytes", (unsigned long)size);
  U64_DEBUG ("Song number: %d", song_num);

  /* Validate SID file header */
  if (size < 4)
    {
      U64_DEBUG ("File too small to be valid SID: %lu bytes",
                 (unsigned long)size);
      return U64_ERR_INVALID;
    }

  /* Check SID magic bytes */
  if (data[0] == 'P' && data[1] == 'S' && data[2] == 'I' && data[3] == 'D')
    {
      U64_DEBUG ("Valid PSID header detected");
    }
  else if (data[0] == 'R' && data[1] == 'S' && data[2] == 'I'
           && data[3] == 'D')
    {
      U64_DEBUG ("Valid RSID header detected");
    }
  else
    {
      U64_DEBUG ("WARNING: No valid SID header found");
      U64_DEBUG ("First 4 bytes: %02X %02X %02X %02X", data[0], data[1],
                 data[2], data[3]);
    }

  /* Build path */
  if (song_num > 0)
    {
      snprintf (path, sizeof (path), "/v1/runners:sidplay?songnr=%ld",
                (long)song_num);
    }
  else
    {
      strcpy (path, "/v1/runners:sidplay");
    }

  U64_DEBUG ("Request path: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = (UBYTE *)data;
  req.body_size = size;

  U64_DEBUG ("Sending HTTP request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("HTTP request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'",
                 req.response); /* Limit output length */

      /* Check for JSON response */
      if (strstr (req.response, "{"))
        {
          U64_DEBUG ("Response appears to be JSON");

          /* Parse error array from JSON response */
          memset (&error_array, 0, sizeof (error_array));
          LONG parse_result = U64_ParseErrorArray (req.response, &error_array);
          U64_DEBUG ("JSON parsing result: %ld", parse_result);

          if (parse_result == U64_OK)
            {
              U64_DEBUG ("Successfully parsed JSON, found %lu errors",
                         (unsigned long)error_array.error_count);

              if (error_array.error_count > 0)
                {
                  U64_DEBUG ("=== ERROR DETAILS ===");
                  for (ULONG i = 0; i < error_array.error_count; i++)
                    {
                      if (error_array.errors[i])
                        {
                          U64_DEBUG ("Error %lu: '%s'", (unsigned long)i,
                                     error_array.errors[i]);
                        }
                    }

                  /* Format error details for caller */
                  if (error_details)
                    {
                      *error_details = U64_FormatErrorArray (&error_array);
                      U64_DEBUG ("Formatted error string: '%s'",
                                 *error_details);
                    }

                  /* Free error array */
                  U64_FreeErrorArray (&error_array);

                  /* Free response */
                  FreeMem (req.response, req.response_size + 1);

                  /* Set specific error */
                  conn->last_error = U64_ERR_GENERAL;
                  U64_DEBUG ("=== PlaySID Result: FAILED (errors found) ===");
                  return U64_ERR_GENERAL;
                }
              else
                {
                  U64_DEBUG ("No errors found in JSON - SUCCESS");
                  /* Success - empty errors array */
                  U64_FreeErrorArray (&error_array);
                  FreeMem (req.response, req.response_size + 1);
                  conn->last_error = U64_OK;
                  U64_DEBUG ("=== PlaySID Result: SUCCESS ===");
                  return U64_OK;
                }
            }
          else
            {
              U64_DEBUG ("Failed to parse JSON: %ld", parse_result);
            }
        }
      else
        {
          U64_DEBUG (
              "Response is not JSON, checking for text success indicators");

          /* Check for plain text success */
          if (strstr (req.response, "success") || strstr (req.response, "ok")
              || strstr (req.response, "OK") || req.response_size < 10)
            {
              U64_DEBUG ("Found success indicator in response");
              FreeMem (req.response, req.response_size + 1);
              conn->last_error = U64_OK;
              U64_DEBUG ("=== PlaySID Result: SUCCESS (text) ===");
              return U64_OK;
            }
        }

      FreeMem (req.response, req.response_size + 1);
    }
  else
    {
      U64_DEBUG ("No response body received");
    }

  /* Final HTTP status analysis */
  U64_DEBUG ("=== Final Status Check ===");
  if (result == U64_OK)
    {
      U64_DEBUG ("HTTP layer succeeded");
      switch (req.status_code)
        {
        case 200:
          U64_DEBUG ("HTTP 200 OK - SUCCESS");
          conn->last_error = U64_OK;
          U64_DEBUG ("=== PlaySID Result: SUCCESS (HTTP 200) ===");
          return U64_OK;
        case 204:
          U64_DEBUG ("HTTP 204 No Content - SUCCESS");
          conn->last_error = U64_OK;
          U64_DEBUG ("=== PlaySID Result: SUCCESS (HTTP 204) ===");
          return U64_OK;
        case 404:
          U64_DEBUG ("HTTP 404 Not Found - endpoint doesn't exist");
          conn->last_error = U64_ERR_NOTFOUND;
          U64_DEBUG ("=== PlaySID Result: FAILED (404) ===");
          return U64_ERR_NOTFOUND;
        case 400:
          U64_DEBUG ("HTTP 400 Bad Request - invalid data or format");
          conn->last_error = U64_ERR_INVALID;
          U64_DEBUG ("=== PlaySID Result: FAILED (400) ===");
          return U64_ERR_INVALID;
        case 500:
          U64_DEBUG ("HTTP 500 Internal Server Error - Ultimate64 error");
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== PlaySID Result: FAILED (500) ===");
          return U64_ERR_GENERAL;
        default:
          U64_DEBUG ("HTTP %d - treating as error", req.status_code);
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== PlaySID Result: FAILED (HTTP %d) ===",
                     req.status_code);
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      U64_DEBUG ("HTTP layer failed: %ld (%s)", result,
                 U64_GetErrorString (result));
      conn->last_error = result;
      U64_DEBUG ("=== PlaySID Result: FAILED (HTTP layer) ===");
      return result;
    }
}
