/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * HTTP protocol implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* HTTP method strings */
static const char *http_methods[] = { "GET", "POST", "PUT", "DELETE" };

/* Build HTTP request header  */
static STRPTR
U64_BuildHttpRequest (U64Connection *conn, HttpRequest *req)
{
  STRPTR header;
  ULONG header_size;
  char content_length[32];

  /* Calculate header size with extra buffer */
  header_size = 1024;
  if (req->path)
    header_size += strlen ((char *)req->path);
  if (conn->password)
    header_size += strlen ((char *)conn->password) + 32;

  /* Allocate header buffer */
  header = AllocMem (header_size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!header)
    {
      U64_DEBUG ("Failed to allocate header buffer (%lu bytes)",
                 (unsigned long)header_size);
      return NULL;
    }

  U64_DEBUG ("Building HTTP request header...");
  U64_DEBUG ("Method: %s", http_methods[req->method]);
  U64_DEBUG ("Path: %s", req->path ? (char *)req->path : "/");
  U64_DEBUG ("Host: %s:%d", (char *)conn->host, conn->port);

  /* Build request line */
  sprintf ((char *)header, "%s %s HTTP/1.1\r\n", http_methods[req->method],
           req->path ? (char *)req->path : "/");

  /* Add Host header */
  strcat ((char *)header, "Host: ");
  strcat ((char *)header, (char *)conn->host);
  if (conn->port != 80)
    {
      char port_str[16];
      sprintf (port_str, ":%d", conn->port);
      strcat ((char *)header, port_str);
    }
  strcat ((char *)header, "\r\n");

  /* Add User-Agent */
  strcat ((char *)header, "User-Agent: Ultimate64-Amiga/1.0\r\n");

  /* Add Accept header */
  strcat ((char *)header, "Accept: */*\r\n");

  /* Add Content-Type if provided */
  if (req->content_type)
    {
      U64_DEBUG ("Content-Type: %s", (char *)req->content_type);
      strcat ((char *)header, "Content-Type: ");
      strcat ((char *)header, (char *)req->content_type);
      strcat ((char *)header, "\r\n");
    }

  /* Add Content-Length for POST/PUT */
  if (req->method == HTTP_POST || req->method == HTTP_PUT)
    {
      sprintf (content_length, "Content-Length: %lu\r\n",
               (unsigned long)req->body_size);
      strcat ((char *)header, content_length);
      U64_DEBUG ("Content-Length: %lu", (unsigned long)req->body_size);
    }

  /* Add password header if needed */
  if (conn->password)
    {
      U64_DEBUG ("Adding X-password header");
      strcat ((char *)header, "X-password: ");
      strcat ((char *)header, (char *)conn->password);
      strcat ((char *)header, "\r\n");
    }

  /* Connection header */
  strcat ((char *)header, "Connection: close\r\n");

  /* End of headers */
  strcat ((char *)header, "\r\n");

  U64_DEBUG ("HTTP header built successfully (%lu bytes)",
             (unsigned long)strlen ((char *)header));

  return header;
}

/* U64_HttpRequest function */
LONG
U64_HttpRequest (U64Connection *conn, HttpRequest *req)
{
  STRPTR header;
  UBYTE *response_buffer = NULL;
  LONG result;
  ULONG total_size = 0;
  char line[1024];
  ULONG content_length = 0;
  ULONG header_len;
  int chunk_count = 0;
  const int MAX_CHUNKS = 100;

  if (!conn || !req)
    {
      U64_DEBUG ("Invalid parameters to HttpRequest");
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Starting HTTP request: %s %s", http_methods[req->method],
             req->path ? (char *)req->path : "/");

  /* Initialize response fields */
  req->response = NULL;
  req->response_size = 0;
  req->status_code = 0;

  /* Initialize network if needed */
  result = U64_NetInit ();
  if (result != U64_OK)
    {
      U64_DEBUG ("Network initialization failed: %ld", result);
      return result;
    }

  /* Disconnect any existing connection */
  U64_NetDisconnect (conn);

  /* Connect to server */
  U64_DEBUG ("Connecting to %s:%d...", (char *)conn->host, conn->port);
  result = U64_NetConnect (conn);
  if (result != U64_OK)
    {
      U64_DEBUG ("Failed to connect to server: %ld", result);
      return result;
    }
  U64_DEBUG ("Connected successfully");

  /* Build HTTP request header */
  header = U64_BuildHttpRequest (conn, req);
  if (!header)
    {
      U64_DEBUG ("Failed to build HTTP header");
      U64_NetDisconnect (conn);
      return U64_ERR_MEMORY;
    }

  header_len = strlen ((char *)header);
  U64_DEBUG ("Sending HTTP header (%lu bytes)...", (unsigned long)header_len);

  /* Send header */
  result = U64_NetSend (conn, (UBYTE *)header, header_len);
  FreeMem (header, header_len + 1);

  if (result != U64_OK)
    {
      U64_DEBUG ("Failed to send HTTP header: %ld", result);
      U64_NetDisconnect (conn);
      return result;
    }
  U64_DEBUG ("Header sent successfully");

  /* Send body if present */
  if (req->body && req->body_size > 0)
    {
      U64_DEBUG ("Sending HTTP body (%lu bytes)...",
                 (unsigned long)req->body_size);

      result = U64_NetSend (conn, req->body, req->body_size);
      if (result != U64_OK)
        {
          U64_DEBUG ("Failed to send HTTP body: %ld", result);
          U64_NetDisconnect (conn);
          return result;
        }
      U64_DEBUG ("Body sent successfully");
    }

  /* Read status line */
  U64_DEBUG ("Reading HTTP status line...");
  result = U64_NetReceiveLine (conn, (STRPTR)line, sizeof (line));
  if (result < 0)
    {
      U64_DEBUG ("Failed to read status line: %ld", result);
      U64_NetDisconnect (conn);
      return (result == U64_ERR_TIMEOUT) ? U64_ERR_TIMEOUT : U64_ERR_NETWORK;
    }

  U64_DEBUG ("Status line: %s", line);

  /* Parse status code */
  if (strncmp (line, "HTTP/", 5) == 0)
    {
      char *space = strchr (line, ' ');
      if (space)
        {
          req->status_code = atoi (space + 1);
          U64_DEBUG ("HTTP Status: %d", req->status_code);
        }
      else
        {
          U64_DEBUG ("Invalid status line format");
          U64_NetDisconnect (conn);
          return U64_ERR_INVALID;
        }
    }
  else
    {
      U64_DEBUG ("Invalid HTTP response");
      U64_NetDisconnect (conn);
      return U64_ERR_INVALID;
    }

  /* Read headers with minimal logging */
  while (chunk_count < MAX_CHUNKS)
    {
      result = U64_NetReceiveLine (conn, (STRPTR)line, sizeof (line));
      if (result < 0)
        {
          U64_DEBUG ("Failed to read header line: %ld", result);
          U64_NetDisconnect (conn);
          return (result == U64_ERR_TIMEOUT) ? U64_ERR_TIMEOUT
                                             : U64_ERR_NETWORK;
        }

      chunk_count++;

      /* Empty line marks end of headers */
      if (strlen (line) == 0)
        {
          U64_DEBUG ("Headers complete");
          break;
        }

      /* Look for Content-Length header */
      if (strnicmp (line, "Content-Length:", 15) == 0)
        {
          content_length = atol (line + 15);
          U64_DEBUG ("Content-Length: %lu", (unsigned long)content_length);
        }
    }

  /* Allocate response buffer */
  ULONG buffer_size = 8192;
  if (content_length > 0 && content_length < 32 * 1024)
    {
      buffer_size = content_length + 256;
    }

  response_buffer = AllocMem (buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!response_buffer)
    {
      U64_DEBUG ("Failed to allocate response buffer");
      U64_NetDisconnect (conn);
      return U64_ERR_MEMORY;
    }

  /* Read response body */
  if (content_length > 0 && content_length < (buffer_size - 256))
    {
      result = U64_NetReceive (conn, response_buffer, content_length);
      if (result < 0)
        {
          U64_DEBUG ("Failed to read response body: %ld", result);
          FreeMem (response_buffer, buffer_size);
          U64_NetDisconnect (conn);
          return (result == U64_ERR_TIMEOUT) ? U64_ERR_TIMEOUT
                                             : U64_ERR_NETWORK;
        }
      total_size = result;
    }
  else
    {
      /* Read until connection closes */
      total_size = 0;
      chunk_count = 0;

      while (total_size < (buffer_size - 256) && chunk_count < 20)
        {
          ULONG chunk_size = (buffer_size - total_size - 256);
          if (chunk_size > 1024)
            chunk_size = 1024;

          result = U64_NetReceive (conn, response_buffer + total_size,
                                   chunk_size);
          if (result <= 0)
            {
              break;
            }
          total_size += result;
          chunk_count++;

          if (chunk_count % 5 == 0)
            {
              Delay (1);
            }
        }
    }

  /* Null terminate response */
  if (response_buffer && total_size < (buffer_size - 10))
    {
      response_buffer[total_size] = '\0';
    }

  /* Store response */
  req->response = (STRPTR)response_buffer;
  req->response_size = total_size;

  U64_DEBUG ("HTTP request complete: %lu bytes, status %d",
             (unsigned long)total_size, req->status_code);

  /* Disconnect */
  U64_NetDisconnect (conn);

  /* Return based on status code */
  switch (req->status_code)
    {
    case 200:
    case 204:
      return U64_OK;
    case 400:
      return U64_ERR_INVALID;
    case 403:
      return U64_ERR_ACCESS;
    case 404:
      return U64_ERR_NOTFOUND;
    case 500:
    case 501:
      return U64_ERR_NOTIMPL;
    case 504:
      return U64_ERR_TIMEOUT;
    default:
      if (req->status_code >= 200 && req->status_code < 300)
        {
          return U64_OK;
        }
      return U64_ERR_GENERAL;
    }
}

/* Also add this debug function to verify the request before sending */
static void
U64_DebugHttpRequest (HttpRequest *req)
{
  printf ("[DEBUG] === HTTP Request Debug ===\n");
  printf ("[DEBUG] Method: %d (%s)\n", req->method,
          req->method < 4 ? http_methods[req->method] : "UNKNOWN");
  printf ("[DEBUG] Path: %s\n", req->path ? (char *)req->path : "NULL");
  printf ("[DEBUG] Content-Type: %s\n",
          req->content_type ? (char *)req->content_type : "NULL");
  printf ("[DEBUG] Body pointer: %p\n", req->body);
  printf ("[DEBUG] Body size: %lu bytes\n", (unsigned long)req->body_size);

  if (req->body && req->body_size >= 4)
    {
      printf ("[DEBUG] Body first 4 bytes: %02X %02X %02X %02X\n",
              req->body[0], req->body[1], req->body[2], req->body[3]);
    }
  printf ("[DEBUG] === End Request Debug ===\n");
}

/* Build URL from connection and path */
STRPTR
U64_BuildURL (U64Connection *conn, CONST_STRPTR path)
{
  STRPTR url;
  ULONG url_size;

  if (!conn || !path)
    {
      return NULL;
    }

  /* Calculate URL size */
  url_size = strlen ((char *)conn->url_prefix) + strlen ((char *)path) + 2;

  /* Allocate URL buffer */
  url = AllocMem (url_size, MEMF_PUBLIC);
  if (!url)
    {
      return NULL;
    }

  /* Build URL */
  strcpy ((char *)url, (char *)conn->url_prefix);
  if (path[0] != '/')
    {
      strcat ((char *)url, "/");
    }
  strcat ((char *)url, (char *)path);

  return url;
}

/* Free URL */
void
U64_FreeURL (STRPTR url)
{
  if (url)
    {
      FreeMem (url, strlen ((char *)url) + 1);
    }
}

/* Generate random boundary for multipart form data */
static void
U64_GenerateBoundary (STRPTR boundary, ULONG size)
{
  static const char chars[]
      = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  ULONG i;
  ULONG time_val;

  /* Use system time as seed */
  time_val = ((ULONG)FindTask (NULL)) ^ ((ULONG)SysBase->LastAlert[0]);

  strcpy ((char *)boundary, "----AmigaBoundary");

  /* Add random characters */
  for (i = strlen ((char *)boundary); i < size - 1 && i < 40; i++)
    {
      time_val = (time_val * 1103515245 + 12345) & 0x7fffffff;
      boundary[i] = chars[time_val % (sizeof (chars) - 1)];
    }
  boundary[i] = '\0';
}

/* Build multipart form data */
static UBYTE *
U64_BuildMultipartForm (CONST_STRPTR boundary, CONST_STRPTR field_name,
                        CONST_STRPTR filename, CONST_STRPTR content_type,
                        CONST UBYTE *data, ULONG data_size,
                        CONST_STRPTR *extra_fields, CONST_STRPTR *extra_values,
                        ULONG num_extras, ULONG *total_size)
{
  UBYTE *form_data;
  ULONG form_size;
  char *ptr;
  ULONG i;

  /* Calculate total size */
  form_size = 0;

  /* Extra fields */
  for (i = 0; i < num_extras; i++)
    {
      form_size += strlen ("--") + strlen ((char *)boundary) + strlen ("\r\n");
      form_size += strlen ("Content-Disposition: form-data; name=\"\"")
                   + strlen ((char *)extra_fields[i]) + strlen ("\r\n\r\n");
      form_size += strlen ((char *)extra_values[i]) + strlen ("\r\n");
    }

  /* File field */
  form_size += strlen ("--") + strlen ((char *)boundary) + strlen ("\r\n");
  form_size
      += strlen ("Content-Disposition: form-data; name=\"\"; filename=\"\"")
         + strlen ((char *)field_name) + strlen ((char *)filename)
         + strlen ("\r\n");
  form_size += strlen ("Content-Type: ") + strlen ((char *)content_type)
               + strlen ("\r\n\r\n");
  form_size += data_size + strlen ("\r\n");

  /* Final boundary */
  form_size += strlen ("--") + strlen ((char *)boundary) + strlen ("--\r\n");

  /* Allocate buffer */
  form_data = AllocMem (form_size + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!form_data)
    {
      return NULL;
    }

  ptr = (char *)form_data;

  /* Add extra fields */
  for (i = 0; i < num_extras; i++)
    {
      sprintf (ptr, "--%s\r\n", (char *)boundary);
      ptr += strlen (ptr);
      sprintf (ptr, "Content-Disposition: form-data; name=\"%s\"\r\n\r\n",
               (char *)extra_fields[i]);
      ptr += strlen (ptr);
      sprintf (ptr, "%s\r\n", (char *)extra_values[i]);
      ptr += strlen (ptr);
    }

  /* Add file field */
  sprintf (ptr, "--%s\r\n", (char *)boundary);
  ptr += strlen (ptr);
  sprintf (ptr,
           "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n",
           (char *)field_name, (char *)filename);
  ptr += strlen (ptr);
  sprintf (ptr, "Content-Type: %s\r\n\r\n", (char *)content_type);
  ptr += strlen (ptr);

  /* Copy binary data */
  memcpy (ptr, data, data_size);
  ptr += data_size;

  /* Add trailing CRLF */
  sprintf (ptr, "\r\n");
  ptr += strlen (ptr);

  /* Add final boundary */
  sprintf (ptr, "--%s--\r\n", (char *)boundary);
  ptr += strlen (ptr);

  *total_size = ptr - (char *)form_data;

  return form_data;
}

/* Execute HTTP POST with multipart form data */
LONG
U64_HttpPostMultipart (U64Connection *conn, CONST_STRPTR path,
                       CONST_STRPTR field_name, CONST_STRPTR filename,
                       CONST_STRPTR file_content_type, CONST UBYTE *file_data,
                       ULONG file_size, CONST_STRPTR *extra_fields,
                       CONST_STRPTR *extra_values, ULONG num_extras)
{
  HttpRequest req;
  UBYTE *form_data;
  ULONG form_size;
  char boundary[64];
  char content_type[128];
  LONG result;

  if (!conn || !path || !field_name || !filename || !file_data)
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Creating multipart POST for %s", (char *)path);

  /* Generate boundary */
  U64_GenerateBoundary ((STRPTR)boundary, sizeof (boundary));

  /* Build multipart form data */
  form_data = U64_BuildMultipartForm (
      (CONST_STRPTR)boundary, field_name, filename,
      file_content_type ? file_content_type
                        : (CONST_STRPTR) "application/octet-stream",
      file_data, file_size, extra_fields, extra_values, num_extras,
      &form_size);
  if (!form_data)
    {
      return U64_ERR_MEMORY;
    }

  U64_DEBUG ("Multipart form size: %lu bytes", (unsigned long)form_size);

  /* Build Content-Type header */
  sprintf (content_type, "multipart/form-data; boundary=%s", boundary);

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = (STRPTR)path;
  req.content_type = (STRPTR)content_type;
  req.body = form_data;
  req.body_size = form_size;

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  /* Free form data */
  FreeMem (form_data, form_size + 1);

  /* Free response if any (caller doesn't need it for multipart posts) */
  if (req.response)
    {
      FreeMem (req.response, req.response_size + 1);
    }

  return result;
}