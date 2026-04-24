/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * HTTP protocol implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

/* For non-blocking connect: IoctlSocket/FIONBIO live in sys/ioctl.h,
 * and EINPROGRESS/EWOULDBLOCK in sys/errno.h (bsdsocket.library). */
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

#define READ_CHUNK_SIZE 1024
#define INITIAL_BUFFER_SIZE 4096
#define MAX_BUFFER_SIZE (10 * 1024 * 1024) /* 10MB max */

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
#ifdef USE_BSDSOCKET
  int sockfd = -1;
  char *response_buffer = NULL;
  char *chunk_buffer = NULL;
  char *new_buffer = NULL;
  LONG result = U64_ERR_GENERAL;
  struct sockaddr_in server_addr;
  struct hostent *server = NULL;
  char request_header[1024];
  BOOL dns_ok = FALSE;
  size_t buffer_size = INITIAL_BUFFER_SIZE;
  size_t total_size = 0;
  int bytes_received;
  char *json_start;
  int retry_count = 0;
  /* Ultimate64 keeps its HTTP socket open after responding (keep-alive),
   * so recv() never returns 0. We used to wait MAX_RETRIES * 30s for
   * a close that never came — the user saw minutes of apparent hang
   * after mount/reset commands. Now we parse Content-Length from the
   * headers and break as soon as we've read the body; retries are
   * reduced to 1 for cases where Content-Length is absent. */
  const int MAX_RETRIES = 1;
  int chunk_count = 0;
  LONG content_length = -1;   /* -1 = unknown, else stop after this many body bytes */
  ULONG headers_end = 0;      /* offset of \r\n\r\n in response_buffer */
  extern struct Library *SocketBase;
  extern int errno_storage;

  if (!conn || !req)
    {
      U64_DEBUG ("Invalid parameters to HttpRequest");
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("=== HTTP Request Start ===");
  U64_DEBUG ("Method: %s", http_methods[req->method]);
  U64_DEBUG ("Path: %s", req->path ? (char *)req->path : "/");
  U64_DEBUG ("Host: %s:%d", (char *)conn->host, conn->port);

  /* Initialize response fields */
  req->response = NULL;
  req->response_size = 0;
  req->status_code = 0;

  /* Check if socket library is available */
  if (!SocketBase)
    {
      U64_DEBUG ("Socket library not initialized");
      return U64_ERR_NETWORK;
    }

  /* Create socket first */
  sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    {
      U64_DEBUG ("Failed to create socket: errno=%d", errno_storage);
      return U64_ERR_NETWORK;
    }

  U64_DEBUG ("Socket created: %d", sockfd);

  /* Set socket options BEFORE connect - CRITICAL */
  struct timeval timeout;
  timeout.tv_sec = 30;
  timeout.tv_usec = 0;

  if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout))
      < 0)
    {
      U64_DEBUG ("Failed to set receive timeout");
    }
  if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout))
      < 0)
    {
      U64_DEBUG ("Failed to set send timeout");
    }

  /* Allocate buffers */
  chunk_buffer = AllocMem (READ_CHUNK_SIZE, MEMF_PUBLIC);
  response_buffer = AllocMem (buffer_size, MEMF_PUBLIC | MEMF_CLEAR);

  if (!chunk_buffer || !response_buffer)
    {
      /* Try with smaller buffer if allocation failed */
      if (!response_buffer && buffer_size > 1024)
        {
          if (response_buffer)
            FreeMem (response_buffer, buffer_size);
          buffer_size = 1024;
          response_buffer = AllocMem (buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
        }

      if (!chunk_buffer || !response_buffer)
        {
          U64_DEBUG ("Failed to allocate buffers");
          result = U64_ERR_MEMORY;
          goto cleanup;
        }
    }

  U64_DEBUG ("Buffers allocated: chunk=%p (%d), response=%p (%lu)",
             chunk_buffer, READ_CHUNK_SIZE, response_buffer,
             (unsigned long)buffer_size);

  /* DNS resolution with protection - PROVEN WORKING METHOD */
  U64_DEBUG ("Starting DNS resolution for: %s", (char *)conn->host);

  Forbid ();
  if (conn && conn->host)
    {
      server = gethostbyname ((char *)conn->host);
      if (server && server->h_addr)
        {
          dns_ok = TRUE;
          U64_DEBUG ("DNS resolution successful");
        }
    }
  Permit ();

  if (!dns_ok || !server)
    {
      U64_DEBUG ("DNS lookup failed for: %s", (char *)conn->host);
      result = U64_ERR_NETWORK;
      goto cleanup;
    }

  /* Setup server address */
  memset (&server_addr, 0, sizeof (server_addr));
  server_addr.sin_family = AF_INET;
  CopyMem (server->h_addr, &server_addr.sin_addr.s_addr, server->h_length);
  server_addr.sin_port = htons (conn->port);

  U64_DEBUG ("Connecting to server...");
  /* Non-blocking connect with 10s bound — blocking connect() on an
   * unreachable Ultimate (wrong IP, device off) stalls the GUI for
   * the OS-level default minutes. */
  {
    LONG nb = 1;
    IoctlSocket (sockfd, FIONBIO, (char *)&nb);
    int crc = connect (sockfd,
                       (struct sockaddr *)&server_addr,
                       sizeof (server_addr));
    if (crc < 0 && Errno () != EINPROGRESS && Errno () != EWOULDBLOCK)
      {
        U64_DEBUG ("Failed to connect: errno=%d", errno_storage);
        result = U64_ERR_NETWORK; goto cleanup;
      }
    if (crc < 0)
      {
        struct timeval ct = { 10, 0 };
        ULONG wmask = 1L << sockfd;
        int ready = WaitSelect (sockfd + 1, NULL, &wmask, NULL, &ct, NULL);
        if (ready <= 0) { result = U64_ERR_NETWORK; goto cleanup; }
        LONG so_err = 0;
        LONG slen = sizeof (so_err);
        if (getsockopt (sockfd, SOL_SOCKET, SO_ERROR, &so_err, &slen) < 0
            || so_err != 0)
          { result = U64_ERR_NETWORK; goto cleanup; }
      }
    nb = 0;
    IoctlSocket (sockfd, FIONBIO, (char *)&nb);
  }

  U64_DEBUG ("Connected successfully");

  /* Build HTTP request header */
  int header_len = snprintf (request_header, sizeof (request_header),
                             "%s %s HTTP/1.1\r\n"
                             "Host: %s:%d\r\n"
                             "User-Agent: Ultimate64-Amiga/1.0\r\n"
                             "Accept: */*\r\n"
                             "Connection: close\r\n",
                             http_methods[req->method],
                             req->path ? (char *)req->path : "/",
                             (char *)conn->host, conn->port);

  /* Add Content-Type if provided */
  if (req->content_type)
    {
      header_len += snprintf (
          request_header + header_len, sizeof (request_header) - header_len,
          "Content-Type: %s\r\n", (char *)req->content_type);
    }

  /* Add Content-Length for POST/PUT */
  if (req->method == HTTP_POST || req->method == HTTP_PUT)
    {
      header_len += snprintf (
          request_header + header_len, sizeof (request_header) - header_len,
          "Content-Length: %lu\r\n", (unsigned long)req->body_size);
    }

  /* Add password header if needed */
  if (conn->password)
    {
      header_len += snprintf (request_header + header_len,
                              sizeof (request_header) - header_len,
                              "X-password: %s\r\n", (char *)conn->password);
    }

  /* End of headers */
  strcat (request_header, "\r\n");
  header_len += 2;

  U64_DEBUG ("Sending HTTP header (%d bytes)...", header_len);

  /* Send header (loop — headers are small but we use the same pattern
   * as the body send for safety). */
  {
    int hdr_sent = 0;
    while (hdr_sent < header_len)
      {
        int chunk = send (sockfd, request_header + hdr_sent,
                          header_len - hdr_sent, 0);
        if (chunk < 0)
          {
            if (Errno () == EAGAIN || Errno () == EWOULDBLOCK)
              {
                struct timeval wt = { 10, 0 };
                ULONG wmask = 1L << sockfd;
                if (WaitSelect (sockfd + 1, NULL, &wmask, NULL, &wt,
                                NULL) <= 0)
                  { result = U64_ERR_NETWORK; goto cleanup; }
                continue;
              }
            U64_DEBUG ("Failed to send header: errno=%d", errno_storage);
            result = U64_ERR_NETWORK; goto cleanup;
          }
        if (chunk == 0) { result = U64_ERR_NETWORK; goto cleanup; }
        hdr_sent += chunk;
      }
  }

  U64_DEBUG ("Header sent successfully");

  /* Send body if present. A single send() on a ~170KB .d64 upload
   * typically returns after writing only what fits in the kernel TCP
   * send buffer (often 16-32KB on bsdsocket). The old code treated
   * any non-negative return as a complete send — so the Ultimate got
   * a truncated multipart body, waited forever for more bytes, and
   * our recv() then waited forever for a response that never came.
   * This matched the user's "mount stuck AND Ultimate hangs" report.
   * Fix: loop until all body bytes are flushed, yielding to
   * WaitSelect on EAGAIN/EWOULDBLOCK. */
  if (req->body && req->body_size > 0)
    {
      U64_DEBUG ("Sending HTTP body (%lu bytes)...",
                 (unsigned long)req->body_size);

      ULONG body_sent = 0;
      UBYTE *body_ptr = (UBYTE *)req->body;
      while (body_sent < req->body_size)
        {
          int chunk = send (sockfd, body_ptr + body_sent,
                            req->body_size - body_sent, 0);
          if (chunk < 0)
            {
              if (Errno () == EAGAIN || Errno () == EWOULDBLOCK)
                {
                  /* Socket full; wait up to 30s for room. */
                  struct timeval wt = { 30, 0 };
                  ULONG wmask = 1L << sockfd;
                  if (WaitSelect (sockfd + 1, NULL, &wmask, NULL, &wt,
                                  NULL) <= 0)
                    {
                      U64_DEBUG ("send timeout at %lu / %lu",
                                 (unsigned long)body_sent,
                                 (unsigned long)req->body_size);
                      result = U64_ERR_NETWORK; goto cleanup;
                    }
                  continue;
                }
              U64_DEBUG ("Failed to send body at %lu/%lu: errno=%d",
                         (unsigned long)body_sent,
                         (unsigned long)req->body_size, errno_storage);
              result = U64_ERR_NETWORK;
              goto cleanup;
            }
          if (chunk == 0)
            {
              /* Peer closed the socket mid-upload. */
              U64_DEBUG ("send returned 0 at %lu/%lu (peer closed)",
                         (unsigned long)body_sent,
                         (unsigned long)req->body_size);
              result = U64_ERR_NETWORK;
              goto cleanup;
            }
          body_sent += (ULONG)chunk;
        }
      U64_DEBUG ("Body fully sent (%lu bytes)",
                 (unsigned long)req->body_size);
    }

  /* Receive response using PROVEN WORKING METHOD */
  U64_DEBUG ("Receiving response...");

  while (chunk_count < 20000) /* Prevent infinite loops */
    {
      struct timeval select_timeout;
      ULONG read_mask;

      select_timeout.tv_sec = 30;
      select_timeout.tv_usec = 0;
      read_mask = 1L << sockfd;

      int ready = WaitSelect (sockfd + 1, &read_mask, NULL, NULL,
                              &select_timeout, NULL);
      if (ready < 0)
        {
          U64_DEBUG ("WaitSelect error");
          break;
        }
      if (ready == 0)
        {
          U64_DEBUG ("Receive timeout (chunk %d)", chunk_count);
          retry_count++;
          if (retry_count >= MAX_RETRIES)
            {
              U64_DEBUG ("Max retries reached");
              break;
            }
          continue;
        }

      if (read_mask & (1L << sockfd))
        {
          bytes_received = recv (sockfd, chunk_buffer, READ_CHUNK_SIZE - 1, 0);
          if (bytes_received < 0)
            {
              U64_DEBUG ("Error receiving data: errno=%d", errno_storage);
              break;
            }
          if (bytes_received == 0)
            {
              U64_DEBUG ("Connection closed by server");
              break;
            }

          chunk_count++;
        U64_DEBUG("Chunk %d: received %d bytes, total now %lu, buffer size %lu", 
                  chunk_count, bytes_received, 
                  (unsigned long)(total_size + bytes_received), 
                  (unsigned long)buffer_size);


          /* Check if buffer needs to grow */
          if (total_size + bytes_received + 1 > buffer_size)
            {
              size_t new_size = buffer_size * 2;
              if (new_size > MAX_BUFFER_SIZE)
                {
                  U64_DEBUG ("Response too large, truncating at %lu bytes",
                             (unsigned long)buffer_size);
                  bytes_received = buffer_size - total_size - 1;
                  if (bytes_received <= 0)
                    break;
                }
              else
                {
                  U64_DEBUG ("Growing buffer from %lu to %lu bytes",
                             (unsigned long)buffer_size,
                             (unsigned long)new_size);

                  new_buffer = AllocMem (new_size, MEMF_PUBLIC | MEMF_CLEAR);
                  if (!new_buffer)
                    {
                      U64_DEBUG ("Failed to grow response buffer");
                      break;
                    }

                  /* Copy existing data */
                  if (total_size > 0)
                    {
                      CopyMem (response_buffer, new_buffer, total_size);
                    }

                  /* Free old buffer and use new one */
                  FreeMem (response_buffer, buffer_size);
                  response_buffer = new_buffer;
                  buffer_size = new_size;
                }
            }

          CopyMem (chunk_buffer, response_buffer + total_size, bytes_received);
          total_size += bytes_received;
          response_buffer[total_size] = '\0';

          retry_count = 0; /* Reset retry count on successful receive */

          /* Once the headers have fully arrived, pick up Content-Length
           * so we can stop reading exactly when the body is complete.
           * Ultimate64 keeps the TCP connection open (keep-alive), so
           * without this we'd WaitSelect until timeout. */
          if (headers_end == 0)
            {
              char *eoh = strstr (response_buffer, "\r\n\r\n");
              if (eoh)
                {
                  headers_end = (ULONG)(eoh - response_buffer) + 4;
                  /* Save and restore the byte at eoh so case-insensitive
                   * strstr on "Content-Length:" stays inside headers. */
                  char save = response_buffer[headers_end];
                  response_buffer[headers_end] = '\0';
                  char *cl = strstr (response_buffer, "Content-Length:");
                  if (!cl) cl = strstr (response_buffer, "content-length:");
                  if (cl)
                    {
                      cl += 15;     /* length of "Content-Length:" */
                      while (*cl == ' ' || *cl == '\t') cl++;
                      content_length = atol (cl);
                      U64_DEBUG ("Content-Length: %ld", content_length);
                    }
                  response_buffer[headers_end] = save;
                }
            }
          /* Early exit: body is complete per Content-Length. */
          if (headers_end > 0 && content_length >= 0
              && total_size >= headers_end + (ULONG)content_length)
            {
              U64_DEBUG ("Body complete (CL=%ld), breaking", content_length);
              break;
            }
        }
    }

  if (total_size == 0)
    {
      U64_DEBUG ("No data received");
      result = U64_ERR_NETWORK;
      goto cleanup;
    }

  U64_DEBUG ("Total received: %lu bytes", (unsigned long)total_size);

  /* Parse HTTP status line */
  char *status_line = response_buffer;
  char *line_end = strstr (status_line, "\r\n");
  if (line_end)
    {
      *line_end = '\0';
      U64_DEBUG ("Status line: %s", status_line);

      /* Parse status code */
      if (strncmp (status_line, "HTTP/", 5) == 0)
        {
          char *space = strchr (status_line, ' ');
          if (space)
            {
              req->status_code = atoi (space + 1);
              U64_DEBUG ("HTTP Status: %d", req->status_code);
            }
        }
      *line_end = '\r'; /* Restore for JSON parsing */
    }

  /* Find start of JSON content */
  json_start = strstr (response_buffer, "\r\n\r\n");
  if (json_start)
    {
      json_start += 4;
      U64_DEBUG ("Found JSON content");
    }
  else
    {
      U64_DEBUG ("Could not find JSON content, using entire response");
      json_start = response_buffer;
    }

  /* Create final response */
  size_t json_len = strlen (json_start);
  if (json_len > 0)
    {
      req->response = AllocMem (json_len + 1, MEMF_PUBLIC);
      if (req->response)
        {
          strcpy (req->response, json_start);
          req->response_size = json_len;
          U64_DEBUG ("Response prepared: %lu bytes", (unsigned long)json_len);
        }
      else
        {
          U64_DEBUG ("Failed to allocate final response buffer");
          result = U64_ERR_MEMORY;
          goto cleanup;
        }
    }

  /* Determine result based on status code */
  if (req->status_code >= 200 && req->status_code < 300)
    {
      result = U64_OK;
    }
  else
    {
      switch (req->status_code)
        {
        case 400:
          result = U64_ERR_INVALID;
          break;
        case 403:
          result = U64_ERR_ACCESS;
          break;
        case 404:
          result = U64_ERR_NOTFOUND;
          break;
        case 500:
        case 501:
          result = U64_ERR_NOTIMPL;
          break;
        case 504:
          result = U64_ERR_TIMEOUT;
          break;
        default:
          result = U64_ERR_GENERAL;
          break;
        }
    }

cleanup:
  /* Close socket */
  if (sockfd >= 0)
    {
      U64_DEBUG ("Closing socket");
      CloseSocket (sockfd);
    }

  /* Free buffers */
  if (response_buffer)
    {
      FreeMem (response_buffer, buffer_size);
    }
  if (chunk_buffer)
    {
      FreeMem (chunk_buffer, READ_CHUNK_SIZE);
    }

  U64_DEBUG ("=== HTTP Request Complete: result=%ld, status=%d ===", result,
             req->status_code);

  return result;

#else /* !USE_BSDSOCKET */
  U64_DEBUG ("Network not supported (USE_BSDSOCKET not defined)");
  return U64_ERR_NOTIMPL;
#endif
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

  /* Emit the file field FIRST, then the extra text fields.
   *
   * The Ultimate64 HTTP firmware's multipart parser only registers fields
   * that appear AFTER the file part; any fields emitted before it are
   * silently dropped. Verified empirically against firmware at 10.0.0.139:
   *   extras-first  ->  {"errors":["Invalid Type ''"]}  (type lost)
   *   file-first    ->  {"errors":[]}                  (mount succeeds)
   * This also matches what reqwest produces for the canonical Rust crate's
   *   curl -F file=@... -F mode=... -F type=...
   * example.
   */

  /* File field */
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

  /* Extra fields after the file part */
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
                       CONST_STRPTR *extra_values, ULONG num_extras,
                       HttpRequest *result_req)
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

  /* Hand response back to caller if requested, else free it. */
  if (result_req)
    {
      result_req->status_code = req.status_code;
      result_req->response = req.response;
      result_req->response_size = req.response_size;
    }
  else if (req.response)
    {
      FreeMem (req.response, req.response_size + 1);
    }

  return result;
}

/* Thin wrapper — kept for existing callers; new code should use the _Ex form
 * so it can attach request headers (needed for e.g. Assembly64 client-id). */
LONG U64_DownloadToFile(CONST_STRPTR url, CONST_STRPTR local_filename,
                        void (*progress_callback)(ULONG bytes, APTR userdata),
                        APTR userdata)
{
    return U64_DownloadToFileEx(url, local_filename, NULL,
                                progress_callback, userdata);
}

LONG U64_DownloadToFileEx(CONST_STRPTR url, CONST_STRPTR local_filename,
                          CONST_STRPTR extra_headers,
                          void (*progress_callback)(ULONG bytes, APTR userdata),
                          APTR userdata)
{
    /* Same defensive check as U64_HttpGetURL — no TCP stack, fail fast
     * rather than hanging on the first socket() call. */
    extern struct Library *SocketBase;
    if (!SocketBase) return U64_ERR_NETWORK;

    LONG result = U64_ERR_GENERAL;
    BPTR file = 0;
    char hostname[256];
    char path[512];
    char *host_start, *path_start;
    int redirect_count = 0;
    const int MAX_REDIRECTS = 5;
    char current_url[1024];
    
    /* Socket variables for manual streaming */
    int sockfd = -1;
    char *chunk_buffer = NULL;
    char *header_buffer = NULL;
    BOOL headers_parsed = FALSE;
    ULONG total_downloaded = 0;
    int status_code = 0;
    ULONG header_pos = 0;
    LONG content_length = -1;   /* -1 unknown; else stop reading after this many body bytes */

    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    U64_DEBUG("=== STREAMING DOWNLOAD START ===");
    
    /* Allocate small buffers only */
    chunk_buffer = AllocMem(READ_CHUNK_SIZE, MEMF_PUBLIC);
    header_buffer = AllocMem(4096, MEMF_PUBLIC | MEMF_CLEAR); /* Only 4KB for headers */
    
    if (!chunk_buffer || !header_buffer) {
        U64_DEBUG("Failed to allocate small buffers");
        result = U64_ERR_MEMORY;
        goto cleanup;
    }
    
    while (redirect_count < MAX_REDIRECTS) {
        U64_DEBUG("Download attempt %d, URL: %s", redirect_count + 1, current_url);
        
        if (strncmp(current_url, "http://", 7) == 0) {
            host_start = current_url + 7;
        } else if (strncmp(current_url, "https://", 8) == 0) {
            host_start = current_url + 8;
        } else {
            result = U64_ERR_INVALID;
            goto cleanup;
        }

        path_start = strchr(host_start, '/');
        if (!path_start) {
            strcpy(hostname, host_start);
            strcpy(path, "/");
        } else {
            ULONG host_len = path_start - host_start;
            CopyMem(host_start, hostname, host_len);
            hostname[host_len] = '\0';
            strcpy(path, path_start);
        }

        /* Manual socket connection for streaming */
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            result = U64_ERR_NETWORK;
            goto cleanup;
        }

        /* Set timeouts */
        struct timeval timeout;
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        /* DNS and connect - using your proven method */
        struct sockaddr_in server_addr;
        struct hostent *server;
        
        Forbid();
        server = gethostbyname(hostname);
        Permit();
        
        if (!server) {
            result = U64_ERR_NETWORK;
            goto cleanup;
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        CopyMem(server->h_addr, &server_addr.sin_addr.s_addr, server->h_length);
        server_addr.sin_port = htons(80);

        /* Non-blocking connect with bounded wait (see U64_HttpGetURL
         * for the rationale — blocking connect() can hang the UI for
         * minutes on AmigaOS emulation when the host is unreachable). */
        {
            LONG nb = 1;
            IoctlSocket(sockfd, FIONBIO, (char *)&nb);
            int crc = connect(sockfd,
                              (struct sockaddr *)&server_addr,
                              sizeof(server_addr));
            if (crc < 0 && Errno() != EINPROGRESS
                        && Errno() != EWOULDBLOCK) {
                result = U64_ERR_NETWORK; goto cleanup;
            }
            if (crc < 0) {
                struct timeval ct = { 10, 0 };
                ULONG wmask = 1L << sockfd;
                int ready = WaitSelect(sockfd + 1, NULL, &wmask, NULL,
                                       &ct, NULL);
                if (ready <= 0) { result = U64_ERR_NETWORK; goto cleanup; }
                LONG so_err = 0;
                LONG slen = sizeof(so_err);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                               &so_err, &slen) < 0 || so_err != 0) {
                    result = U64_ERR_NETWORK; goto cleanup;
                }
            }
            nb = 0;
            IoctlSocket(sockfd, FIONBIO, (char *)&nb);
        }

        /* Send HTTP request. extra_headers, if present, is an already-formed
         * raw CRLF-terminated header block (e.g. "client-id: u64manager\r\n")
         * inserted between Accept: and Connection:. */
        char request[1536];
        int len = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "User-Agent: Ultimate64-Amiga/1.0\r\n"
                          "Accept: */*\r\n"
                          "%s"
                          "Connection: close\r\n"
                          "\r\n",
                          path, hostname,
                          extra_headers ? (char *)extra_headers : "");

        if (send(sockfd, request, len, 0) < 0) {
            result = U64_ERR_NETWORK;
            goto cleanup;
        }

        /* Stream response directly to file */
        headers_parsed = FALSE;
        header_pos = 0;
        total_downloaded = 0;
        status_code = 0;

        int chunk_count = 0;
        while (chunk_count < 10000) { 
            struct timeval select_timeout;
            ULONG read_mask;

            select_timeout.tv_sec = 30; /* Longer timeout */
            select_timeout.tv_usec = 0;
            read_mask = 1L << sockfd;

            int ready = WaitSelect(sockfd + 1, &read_mask, NULL, NULL, &select_timeout, NULL);
            if (ready <= 0) {
                U64_DEBUG("Select timeout or error after %d chunks", chunk_count);
                break;
            }

            int bytes_received = recv(sockfd, chunk_buffer, READ_CHUNK_SIZE - 1, 0);
            if (bytes_received <= 0) {
                U64_DEBUG("Connection closed after receiving %lu bytes", (unsigned long)total_downloaded);
                break;
            }

            chunk_count++;
            chunk_buffer[bytes_received] = '\0';

            if (!headers_parsed) {
                /* Still reading headers */
                if (header_pos + bytes_received < 4095) {
                    CopyMem(chunk_buffer, header_buffer + header_pos, bytes_received);
                    header_pos += bytes_received;
                    header_buffer[header_pos] = '\0';

                    /* Look for end of headers */
                    char *header_end = strstr(header_buffer, "\r\n\r\n");
                    if (header_end) {
                        headers_parsed = TRUE;

                        /* Parse status */
                        if (strncmp(header_buffer, "HTTP/", 5) == 0) {
                            char *space = strchr(header_buffer, ' ');
                            if (space) {
                                status_code = atoi(space + 1);
                            }
                        }

                        U64_DEBUG("Headers parsed, status: %d", status_code);

                        /* Handle redirects */
                        if (status_code >= 301 && status_code <= 308) {
                            char *loc = strstr(header_buffer, "Location: ");
                            if (!loc) loc = strstr(header_buffer, "location: ");
                            if (loc) {
                                loc += 10;
                                char *end = strstr(loc, "\r\n");
                                if (end && ((size_t)(end - loc)) < sizeof(current_url) - 1) {
                                    CopyMem(loc, current_url, end - loc);
                                    current_url[end - loc] = '\0';
                                    CloseSocket(sockfd);
                                    sockfd = -1;
                                    redirect_count++;
                                    U64_DEBUG("Redirecting to: %s", current_url);
                                    goto next_redirect;
                                }
                            }
                        }

                        if (status_code != 200) {
                            U64_DEBUG("HTTP error: %d", status_code);
                            result = U64_ERR_GENERAL;
                            goto cleanup;
                        }

                        /* Parse Content-Length — nginx keep-alive means we
                         * can't rely on recv()==0 to end the transfer. */
                        {
                            char *cl = strstr(header_buffer, "Content-Length:");
                            if (!cl) cl = strstr(header_buffer, "content-length:");
                            if (cl) {
                                cl = strchr(cl, ':');
                                if (cl) {
                                    cl++;
                                    while (*cl == ' ' || *cl == '\t') cl++;
                                    content_length = atol(cl);
                                }
                            }
                        }

                        /* Open output file */
                        file = Open(local_filename, MODE_NEWFILE);
                        if (!file) {
                            result = U64_ERR_ACCESS;
                            goto cleanup;
                        }

                        /* Call progress callback to indicate start */
                        if (progress_callback) {
                            progress_callback(0, userdata);
                        }

                        /* Write any body data that came with headers */
                        char *body_start = header_end + 4;
                        LONG body_bytes = (header_buffer + header_pos) - body_start;
                        if (body_bytes > 0) {
                            Write(file, body_start, body_bytes);
                            total_downloaded += body_bytes;
                            
                            /* Call progress callback for header body data */
                            if (progress_callback) {
                                progress_callback(total_downloaded, userdata);
                            }
                        }
                    }
                } else {
                    U64_DEBUG("Headers too large");
                    result = U64_ERR_GENERAL;
                    goto cleanup;
                }
            } else {
                /* Headers done, write body directly to file */
                LONG written = Write(file, chunk_buffer, bytes_received);
                if (written != bytes_received) {
                    U64_DEBUG("Write error: %ld of %d bytes", written, bytes_received);
                    result = U64_ERR_GENERAL;
                    goto cleanup;
                }

                total_downloaded += bytes_received;
                
                /* Call progress callback more frequently - every 10 chunks (~10KB) */
                if (progress_callback && (chunk_count % 10 == 0)) {
                    progress_callback(total_downloaded, userdata);
                }
                
                if (chunk_count % 1000 == 0) {
                    U64_DEBUG("Downloaded %lu bytes (%d chunks)",
                             (unsigned long)total_downloaded, chunk_count);
                }
            }

            /* End of body per Content-Length — avoid waiting on keep-alive. */
            if (headers_parsed && content_length >= 0
                && total_downloaded >= (ULONG)content_length) {
                break;
            }
        }

        if (headers_parsed && status_code == 200 && total_downloaded > 0) {
            U64_DEBUG("SUCCESS: %lu bytes downloaded", (unsigned long)total_downloaded);
            
            /* Final progress callback */
            if (progress_callback) {
                progress_callback(total_downloaded, userdata);
            }
            
            result = U64_OK;
        }
        break;

next_redirect:
        continue;
    }

cleanup:
    if (file) Close(file);
    if (sockfd >= 0) CloseSocket(sockfd);
    if (chunk_buffer) FreeMem(chunk_buffer, READ_CHUNK_SIZE);
    if (header_buffer) FreeMem(header_buffer, 4096);

    U64_DEBUG("=== STREAMING DOWNLOAD END: %ld ===", result);
    return result;
}

/* Memory-buffered HTTP GET for small responses (JSON, short text).
 *
 * Parallels U64_DownloadToFileEx but accumulates the response body into an
 * AllocVec'd buffer and returns it. The caller FreeVec's *out_buffer on
 * success; on failure *out_buffer is untouched. */
LONG U64_HttpGetURL(CONST_STRPTR url, CONST_STRPTR extra_headers,
                    UBYTE **out_buffer, ULONG *out_size, UWORD *out_status)
{
    /* Without bsdsocket.library open, calling socket() dispatches
     * through a NULL library base and locks the whole task (no timeout,
     * no signal delivery). Bail out here so the caller gets a clean
     * U64_ERR_NETWORK instead of a frozen UI. This is the single most
     * common failure mode on AmigaOS where the TCP stack (Roadshow /
     * AmiTCP / Miami) hasn't been started yet. */
    extern struct Library *SocketBase;
    if (!SocketBase) {
        if (out_buffer) *out_buffer = NULL;
        if (out_size)   *out_size = 0;
        if (out_status) *out_status = 0;
        return U64_ERR_NETWORK;
    }

    LONG result = U64_ERR_GENERAL;
    char hostname[256];
    char path[512];
    char *host_start, *path_start;
    int redirect_count = 0;
    const int MAX_REDIRECTS = 5;
    char current_url[1024];

    int sockfd = -1;
    char *chunk_buffer = NULL;
    char *header_buffer = NULL;
    BOOL headers_parsed = FALSE;
    int status_code = 0;
    ULONG header_pos = 0;
    LONG content_length = -1;   /* -1 = unknown, else read exactly this many body bytes */

    UBYTE *body = NULL;
    ULONG body_cap = 0;
    ULONG body_len = 0;

    if (!url || !out_buffer || !out_size) return U64_ERR_INVALID;
    *out_buffer = NULL;
    *out_size = 0;
    if (out_status) *out_status = 0;

    strncpy(current_url, (char *)url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    chunk_buffer = AllocMem(READ_CHUNK_SIZE, MEMF_PUBLIC);
    header_buffer = AllocMem(4096, MEMF_PUBLIC | MEMF_CLEAR);
    if (!chunk_buffer || !header_buffer) { result = U64_ERR_MEMORY; goto cleanup; }

    while (redirect_count < MAX_REDIRECTS) {
        if (strncmp(current_url, "http://", 7) == 0)       host_start = current_url + 7;
        else if (strncmp(current_url, "https://", 8) == 0) host_start = current_url + 8;
        else { result = U64_ERR_INVALID; goto cleanup; }

        path_start = strchr(host_start, '/');
        if (!path_start) { strcpy(hostname, host_start); strcpy(path, "/"); }
        else {
            ULONG host_len = path_start - host_start;
            CopyMem(host_start, hostname, host_len);
            hostname[host_len] = '\0';
            strcpy(path, path_start);
        }

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) { result = U64_ERR_NETWORK; goto cleanup; }

        /* 8s is plenty for a small JSON response; keeps the GUI responsive
         * when Assembly64 is slow or the /search/entries endpoint returns
         * 500 with a stuck keep-alive. */
        struct timeval tv = { 8, 0 };
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct sockaddr_in server_addr;
        struct hostent *server;
        Forbid(); server = gethostbyname(hostname); Permit();
        if (!server) { result = U64_ERR_NETWORK; goto cleanup; }
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        CopyMem(server->h_addr, &server_addr.sin_addr.s_addr, server->h_length);
        server_addr.sin_port = htons(80);

        /* Non-blocking connect with bounded wait. A blocking connect() on
         * an unreachable host can stall the whole process for minutes on
         * AmigaOS emulation (Roadshow/AmiTCP) — SO_SNDTIMEO/SO_RCVTIMEO
         * only affect send/recv, not the TCP handshake. We flip the
         * socket to non-blocking, kick connect(), then WaitSelect() on
         * the write mask for at most 8s. That's how the user sees a
         * quick "Search failed" instead of a frozen GUI. */
        {
            LONG nb = 1;
            IoctlSocket(sockfd, FIONBIO, (char *)&nb);
            int crc = connect(sockfd,
                              (struct sockaddr *)&server_addr,
                              sizeof(server_addr));
            if (crc < 0 && Errno() != EINPROGRESS
                        && Errno() != EWOULDBLOCK) {
                result = U64_ERR_NETWORK; goto cleanup;
            }
            if (crc < 0) {
                struct timeval ct = { 8, 0 };
                ULONG wmask = 1L << sockfd;
                int ready = WaitSelect(sockfd + 1, NULL, &wmask, NULL,
                                       &ct, NULL);
                if (ready <= 0) {   /* 0=timeout, -1=error */
                    result = U64_ERR_NETWORK; goto cleanup;
                }
                /* Check SO_ERROR to see if the connect() itself
                 * succeeded — WaitSelect wakes on connect refused too. */
                LONG so_err = 0;
                LONG len = sizeof(so_err);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                               &so_err, &len) < 0 || so_err != 0) {
                    result = U64_ERR_NETWORK; goto cleanup;
                }
            }
            /* Back to blocking for send()/recv() — the subsequent
             * read loop already uses its own WaitSelect timeout. */
            nb = 0;
            IoctlSocket(sockfd, FIONBIO, (char *)&nb);
        }

        char request[1536];
        int len = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "User-Agent: Ultimate64-Amiga/1.0\r\n"
                          "Accept: */*\r\n"
                          "%s"
                          "Connection: close\r\n"
                          "\r\n",
                          path, hostname,
                          extra_headers ? (char *)extra_headers : "");
        if (send(sockfd, request, len, 0) < 0) { result = U64_ERR_NETWORK; goto cleanup; }

        headers_parsed = FALSE;
        header_pos = 0;
        status_code = 0;
        body_len = 0;  /* drop anything from a prior redirect hop */

        int chunks = 0;
        while (chunks < 10000) {
            struct timeval st = { 8, 0 };  /* per-chunk read timeout */
            ULONG mask = 1L << sockfd;
            int ready = WaitSelect(sockfd + 1, &mask, NULL, NULL, &st, NULL);
            if (ready <= 0) break;

            int got = recv(sockfd, chunk_buffer, READ_CHUNK_SIZE - 1, 0);
            if (got <= 0) break;
            chunks++;
            chunk_buffer[got] = '\0';

            if (!headers_parsed) {
                if (header_pos + got < 4095) {
                    CopyMem(chunk_buffer, header_buffer + header_pos, got);
                    header_pos += got;
                    header_buffer[header_pos] = '\0';

                    char *hend = strstr(header_buffer, "\r\n\r\n");
                    if (hend) {
                        headers_parsed = TRUE;
                        if (strncmp(header_buffer, "HTTP/", 5) == 0) {
                            char *sp = strchr(header_buffer, ' ');
                            if (sp) status_code = atoi(sp + 1);
                        }

                        if (status_code >= 301 && status_code <= 308) {
                            char *loc = strstr(header_buffer, "Location: ");
                            if (!loc) loc = strstr(header_buffer, "location: ");
                            if (loc) {
                                loc += 10;
                                char *e = strstr(loc, "\r\n");
                                if (e && (size_t)(e - loc) < sizeof(current_url) - 1) {
                                    CopyMem(loc, current_url, e - loc);
                                    current_url[e - loc] = '\0';
                                    CloseSocket(sockfd); sockfd = -1;
                                    redirect_count++;
                                    goto next_redirect;
                                }
                            }
                        }
                        /* Keep going even on non-2xx: capture the error
                         * body so the caller can surface useful diagnostics
                         * (Assembly64 puts its errorCode there). The final
                         * status is reported via the return code / out
                         * buffer; a non-2xx status keeps result at
                         * U64_ERR_GENERAL so callers can distinguish. */

                        /* Parse Content-Length so we can stop reading as soon
                         * as the advertised body has been delivered — nginx
                         * ignores our Connection: close and keeps the socket
                         * open, so we can't rely on recv()==0 to signal end. */
                        {
                            char *cl = strstr(header_buffer, "Content-Length:");
                            if (!cl) cl = strstr(header_buffer, "content-length:");
                            if (cl) {
                                cl = strchr(cl, ':');
                                if (cl) {
                                    cl++;
                                    while (*cl == ' ' || *cl == '\t') cl++;
                                    content_length = atol(cl);
                                }
                            }
                        }

                        /* Carry over any body bytes that already arrived. */
                        char *bstart = hend + 4;
                        LONG already = (header_buffer + header_pos) - bstart;
                        if (already > 0) {
                            if (body_len + already + 1 > body_cap) {
                                ULONG new_cap = body_cap ? body_cap * 2 : 4096;
                                while (new_cap < body_len + (ULONG)already + 1) new_cap *= 2;
                                UBYTE *nb = AllocVec(new_cap, MEMF_PUBLIC);
                                if (!nb) { result = U64_ERR_MEMORY; goto cleanup; }
                                if (body) { CopyMem(body, nb, body_len); FreeVec(body); }
                                body = nb; body_cap = new_cap;
                            }
                            CopyMem(bstart, body + body_len, already);
                            body_len += already;
                        }
                    }
                } else { result = U64_ERR_GENERAL; goto cleanup; }
            } else {
                /* Body chunk — grow buffer and append. */
                if (body_len + got + 1 > body_cap) {
                    ULONG new_cap = body_cap ? body_cap * 2 : 4096;
                    while (new_cap < body_len + (ULONG)got + 1) new_cap *= 2;
                    UBYTE *nb = AllocVec(new_cap, MEMF_PUBLIC);
                    if (!nb) { result = U64_ERR_MEMORY; goto cleanup; }
                    if (body) { CopyMem(body, nb, body_len); FreeVec(body); }
                    body = nb; body_cap = new_cap;
                }
                CopyMem(chunk_buffer, body + body_len, got);
                body_len += got;
            }

            /* Stop as soon as the declared body has been received — avoids
             * a 30s wait for a FIN the server won't send under keep-alive. */
            if (headers_parsed && content_length >= 0
                && body_len >= (ULONG)content_length) {
                break;
            }
        }

        if (headers_parsed) {
            if (out_status) *out_status = (UWORD)status_code;
            if (body) body[body_len] = '\0';  /* null-term for safe string use */
            *out_buffer = body;
            *out_size = body_len;
            body = NULL;  /* ownership transferred */
            result = (status_code >= 200 && status_code < 300)
                     ? U64_OK : U64_ERR_GENERAL;
        }
        break;

next_redirect:
        continue;
    }

cleanup:
    if (sockfd >= 0) CloseSocket(sockfd);
    if (chunk_buffer) FreeMem(chunk_buffer, READ_CHUNK_SIZE);
    if (header_buffer) FreeMem(header_buffer, 4096);
    if (body) FreeVec(body);  /* only reached when we never got a headers-parsed response */
    return result;
}