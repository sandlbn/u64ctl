/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * Network communication implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#ifdef USE_BSDSOCKET
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <proto/socket.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Socket library base */
#ifdef USE_BSDSOCKET
struct Library *SocketBase = NULL;
static int errno_storage = 0;
#endif

/* Network connection structure */
struct NetConnection
{
#ifdef USE_BSDSOCKET
  LONG socket;
  struct sockaddr_in server_addr;
#endif
  BOOL connected;
  UBYTE *recv_buffer;
  ULONG recv_buffer_size;
  ULONG recv_buffer_pos;
  ULONG recv_buffer_len;
};

/* Initialize network subsystem */
LONG
U64_NetInit (void)
{
  U64_DEBUG ("Starting network initialization");

#ifdef USE_BSDSOCKET
  U64_DEBUG ("USE_BSDSOCKET is defined");
  /* Open bsdsocket.library */
  if (!SocketBase)
    {
      U64_DEBUG ("Opening bsdsocket.library");
      SocketBase = OpenLibrary ("bsdsocket.library", 4L);
      if (!SocketBase)
        {
          U64_DEBUG ("Failed to open bsdsocket.library");
          return U64_ERR_NETWORK;
        }

      U64_DEBUG ("bsdsocket.library opened, initializing");
      /* Initialize socket library with proper tags */
      if (SocketBaseTags (SBTM_SETVAL (SBTC_ERRNOPTR (sizeof (errno_storage))),
                          (ULONG)&errno_storage, SBTM_SETVAL (SBTC_LOGTAGPTR),
                          (ULONG) "Ultimate64", TAG_DONE))
        {
          U64_DEBUG ("Failed to initialize socket library");
          CloseLibrary (SocketBase);
          SocketBase = NULL;
          return U64_ERR_NETWORK;
        }

      U64_DEBUG ("Network subsystem initialized successfully");
    }
  else
    {
      U64_DEBUG ("SocketBase already initialized");
    }
#else
  U64_DEBUG ("USE_BSDSOCKET not defined - network not available");
  return U64_ERR_NOTIMPL;
#endif

  return U64_OK;
}

/* Cleanup network subsystem - SAFE VERSION */
void
U64_NetCleanup (void)
{
  U64_DEBUG ("Starting network cleanup");

#ifdef USE_BSDSOCKET
  if (SocketBase)
    {
      U64_DEBUG ("Closing bsdsocket.library");
      CloseLibrary (SocketBase);
      SocketBase = NULL;
      U64_DEBUG ("bsdsocket.library closed");
    }
  else
    {
      U64_DEBUG ("SocketBase was already NULL");
    }
#else
  U64_DEBUG ("Network not supported");
#endif

  U64_DEBUG ("Network cleanup complete");
}

/* Connect to Ultimate device */
LONG
U64_NetConnect (U64Connection *conn)
{
#ifdef USE_BSDSOCKET
  struct NetConnection *net;
  struct hostent *host;
  LONG sock;
  struct timeval tv;
  ULONG addr;

  if (!conn || !SocketBase)
    {
      return U64_ERR_INVALID;
    }

  /* Check if already connected */
  if (conn->net_connection)
    {
      net = (struct NetConnection *)conn->net_connection;
      if (net->connected)
        {
          return U64_OK;
        }
    }

  /* Allocate network connection structure */
  net = AllocMem (sizeof (struct NetConnection), MEMF_PUBLIC | MEMF_CLEAR);
  if (!net)
    {
      return U64_ERR_MEMORY;
    }

  /* Allocate receive buffer */
  net->recv_buffer_size = 8192;
  net->recv_buffer = AllocMem (net->recv_buffer_size, MEMF_PUBLIC);
  if (!net->recv_buffer)
    {
      FreeMem (net, sizeof (struct NetConnection));
      return U64_ERR_MEMORY;
    }

  /* Create socket */
  sock = socket (AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    {
      U64_DEBUG ("Failed to create socket: errno=%d", errno_storage);
      FreeMem (net->recv_buffer, net->recv_buffer_size);
      FreeMem (net, sizeof (struct NetConnection));
      return U64_ERR_NETWORK;
    }

  /* Set socket timeouts */
  tv.tv_sec = 30; /* 30 second timeout */
  tv.tv_usec = 0;
  if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0)
    {
      U64_DEBUG ("Failed to set receive timeout");
    }
  if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) < 0)
    {
      U64_DEBUG ("Failed to set send timeout");
    }

  /* Setup server address structure */
  memset (&net->server_addr, 0, sizeof (net->server_addr));
  net->server_addr.sin_family = AF_INET;
  net->server_addr.sin_port = htons (conn->port);

  /* DNS resolution with protection */
  U64_DEBUG ("Resolving hostname: %s", (char *)conn->host);

  Forbid ();
  host = gethostbyname ((char *)conn->host);
  if (host && host->h_addr)
    {
      /* Copy resolved address */
      memcpy (&net->server_addr.sin_addr, host->h_addr, host->h_length);
      addr = net->server_addr.sin_addr.s_addr;
    }
  else
    {
      /* Try as IP address */
      addr = inet_addr ((char *)conn->host);
      if (addr == INADDR_NONE)
        {
          Permit ();
          U64_DEBUG ("Failed to resolve hostname: %s", (char *)conn->host);
          CloseSocket (sock);
          FreeMem (net->recv_buffer, net->recv_buffer_size);
          FreeMem (net, sizeof (struct NetConnection));
          return U64_ERR_NETWORK;
        }
      net->server_addr.sin_addr.s_addr = addr;
    }
  Permit ();

  U64_DEBUG ("Connecting to server...");

  /* Connect to server */
  if (connect (sock, (struct sockaddr *)&net->server_addr,
               sizeof (net->server_addr))
      < 0)
    {
      U64_DEBUG ("Failed to connect: errno=%d", errno_storage);
      CloseSocket (sock);
      FreeMem (net->recv_buffer, net->recv_buffer_size);
      FreeMem (net, sizeof (struct NetConnection));
      return U64_ERR_NETWORK;
    }

  net->socket = sock;
  net->connected = TRUE;
  net->recv_buffer_pos = 0;
  net->recv_buffer_len = 0;

  /* Store connection in U64Connection structure */
  conn->net_connection = net;

  U64_DEBUG ("Connected successfully");
  return U64_OK;
#else
  return U64_ERR_NOTIMPL;
#endif
}

/* Disconnect from Ultimate device - SAFE VERSION */
void
U64_NetDisconnect (U64Connection *conn)
{
#ifdef USE_BSDSOCKET
  struct NetConnection *net;

  if (!conn)
    {
      U64_DEBUG ("NULL connection");
      return;
    }

  if (!conn->net_connection)
    {
      U64_DEBUG ("No network connection to disconnect");
      return;
    }

  net = (struct NetConnection *)conn->net_connection;
  U64_DEBUG ("Disconnecting network connection");

  /* Close socket safely */
  if (net->socket >= 0)
    {
      U64_DEBUG ("Closing socket %ld", net->socket);
      CloseSocket (net->socket);
      net->socket = -1; /* Mark as closed */
    }

  /* Mark as disconnected */
  net->connected = FALSE;

  /* Free receive buffer safely */
  if (net->recv_buffer)
    {
      U64_DEBUG ("Freeing receive buffer (%lu bytes)",
                 (unsigned long)net->recv_buffer_size);
      FreeMem (net->recv_buffer, net->recv_buffer_size);
      net->recv_buffer = NULL; /* Prevent double-free */
      net->recv_buffer_size = 0;
    }

  /* Free network structure */
  U64_DEBUG ("Freeing network structure");
  FreeMem (net, sizeof (struct NetConnection));

  /* Clear the pointer */
  conn->net_connection = NULL;

  U64_DEBUG ("Disconnect complete");
#else
  U64_DEBUG ("Network not supported");
#endif
}

/* Send data over network */
LONG
U64_NetSend (U64Connection *conn, CONST UBYTE *data, ULONG size)
{
#ifdef USE_BSDSOCKET
  struct NetConnection *net;
  LONG sent;
  LONG total_sent = 0;
  ULONG write_mask;
  struct timeval timeout;
  int ready;
  int retry_count = 0;
  const int MAX_RETRIES = 5;

  if (!conn || !data || size == 0)
    {
      return U64_ERR_INVALID;
    }

  net = (struct NetConnection *)conn->net_connection;
  if (!net || !net->connected)
    {
      return U64_ERR_NETWORK;
    }

  U64_DEBUG ("Sending %lu bytes", (unsigned long)size);

  /* Send data in chunks if necessary */
  while (total_sent < (LONG)size && retry_count < MAX_RETRIES)
    {
      /* Use WaitSelect to check if socket is ready for writing */
      timeout.tv_sec = 5; /* Shorter timeout */
      timeout.tv_usec = 0;
      write_mask = 1L << net->socket;

      ready = WaitSelect (net->socket + 1, NULL, &write_mask, NULL, &timeout,
                          NULL);
      if (ready < 0)
        {
          U64_DEBUG ("WaitSelect error during send");
          net->connected = FALSE;
          return U64_ERR_NETWORK;
        }
      if (ready == 0)
        {
          U64_DEBUG ("Send timeout (retry %d)", retry_count);
          retry_count++;
          if (retry_count >= MAX_RETRIES)
            {
              U64_DEBUG ("Send timeout - max retries reached");
              net->connected = FALSE;
              return U64_ERR_TIMEOUT;
            }
          continue;
        }

      if (write_mask & (1L << net->socket))
        {
          sent = send (net->socket, data + total_sent, size - total_sent, 0);
          if (sent < 0)
            {
              if (errno_storage == EINTR)
                {
                  continue; /* Retry on interrupt */
                }
              U64_DEBUG ("Send error: errno=%d", errno_storage);
              net->connected = FALSE;
              return U64_ERR_NETWORK;
            }
          if (sent == 0)
            {
              U64_DEBUG ("Send returned 0 - connection closed");
              net->connected = FALSE;
              return U64_ERR_NETWORK;
            }
          total_sent += sent;
          retry_count = 0; /* Reset retry count on successful send */
          U64_DEBUG ("Sent %ld bytes (total: %ld/%lu)", sent, total_sent,
                     (unsigned long)size);
        }
    }

  if (total_sent < (LONG)size)
    {
      U64_DEBUG ("Incomplete send: %ld/%lu bytes", total_sent,
                 (unsigned long)size);
      return U64_ERR_NETWORK;
    }

  return U64_OK;
#else
  return U64_ERR_NOTIMPL;
#endif
}

/* Receive data from network */
LONG
U64_NetReceive (U64Connection *conn, UBYTE *buffer, ULONG size)
{
#ifdef USE_BSDSOCKET
  struct NetConnection *net;
  LONG received;
  LONG total_received = 0;
  ULONG read_mask;
  struct timeval timeout;
  int ready;
  int retry_count = 0;
  const int MAX_RETRIES = 5;

  if (!conn || !buffer || size == 0)
    {
      return U64_ERR_INVALID;
    }

  net = (struct NetConnection *)conn->net_connection;
  if (!net || !net->connected)
    {
      return U64_ERR_NETWORK;
    }

  U64_DEBUG ("Receiving up to %lu bytes", (unsigned long)size);

  /* Receive data with timeout */
  while (total_received < (LONG)size && retry_count < MAX_RETRIES)
    {
      /* Use WaitSelect to check if data is available */
      timeout.tv_sec = 5; /* Shorter timeout */
      timeout.tv_usec = 0;
      read_mask = 1L << net->socket;

      ready = WaitSelect (net->socket + 1, &read_mask, NULL, NULL, &timeout,
                          NULL);
      if (ready < 0)
        {
          U64_DEBUG ("WaitSelect error during receive");
          net->connected = FALSE;
          return total_received > 0 ? total_received : U64_ERR_NETWORK;
        }
      if (ready == 0)
        {
          U64_DEBUG ("Receive timeout (retry %d)", retry_count);
          retry_count++;
          if (retry_count >= MAX_RETRIES)
            {
              U64_DEBUG ("Receive timeout - max retries reached");
              return total_received > 0 ? total_received : U64_ERR_TIMEOUT;
            }
          continue;
        }

      if (read_mask & (1L << net->socket))
        {
          received = recv (net->socket, buffer + total_received,
                           size - total_received, 0);
          if (received < 0)
            {
              if (errno_storage == EINTR)
                {
                  continue; /* Retry on interrupt */
                }
              U64_DEBUG ("Receive error: errno=%d", errno_storage);
              net->connected = FALSE;
              return total_received > 0 ? total_received : U64_ERR_NETWORK;
            }
          if (received == 0)
            {
              /* Connection closed */
              net->connected = FALSE;
              U64_DEBUG ("Connection closed by peer");
              return total_received > 0 ? total_received : U64_ERR_NETWORK;
            }
          total_received += received;
          retry_count = 0; /* Reset retry count on successful receive */
          U64_DEBUG ("Received %ld bytes (total: %ld/%lu)", received,
                     total_received, (unsigned long)size);

          /* For HTTP, we might get the full response in smaller chunks */
          /* Don't wait for the full size if we got some data */
          break;
        }
    }

  return total_received;
#else
  return U64_ERR_NOTIMPL;
#endif
}

/* Receive line from network (for HTTP headers) */
LONG
U64_NetReceiveLine (U64Connection *conn, STRPTR buffer, ULONG max_size)
{
#ifdef USE_BSDSOCKET
  struct NetConnection *net;
  ULONG pos = 0;
  UBYTE c;
  LONG result;
  ULONG read_mask;
  struct timeval timeout;
  int ready;
  int retry_count = 0;
  const int MAX_RETRIES = 10;

  if (!conn || !buffer || max_size == 0)
    {
      return U64_ERR_INVALID;
    }

  net = (struct NetConnection *)conn->net_connection;
  if (!net || !net->connected)
    {
      return U64_ERR_NETWORK;
    }

  /* Read until we find \r\n or buffer is full */
  while (pos < max_size - 1 && retry_count < MAX_RETRIES)
    {
      /* Check if we have data in our internal buffer */
      if (net->recv_buffer_pos < net->recv_buffer_len)
        {
          c = net->recv_buffer[net->recv_buffer_pos++];
        }
      else
        {
          /* Need to read more data */
          timeout.tv_sec = 3; /* Shorter timeout for line reading */
          timeout.tv_usec = 0;
          read_mask = 1L << net->socket;

          ready = WaitSelect (net->socket + 1, &read_mask, NULL, NULL,
                              &timeout, NULL);
          if (ready < 0)
            {
              U64_DEBUG ("WaitSelect error during line receive");
              net->connected = FALSE;
              break;
            }
          if (ready == 0)
            {
              U64_DEBUG ("Line receive timeout (retry %d)", retry_count);
              retry_count++;
              if (retry_count >= MAX_RETRIES)
                {
                  U64_DEBUG ("Line receive timeout - max retries reached");
                  break;
                }
              continue;
            }

          if (read_mask & (1L << net->socket))
            {
              result = recv (net->socket, net->recv_buffer,
                             net->recv_buffer_size, 0);
              if (result < 0)
                {
                  if (errno_storage == EINTR)
                    {
                      continue;
                    }
                  U64_DEBUG ("Line receive error: errno=%d", errno_storage);
                  net->connected = FALSE;
                  break;
                }
              if (result == 0)
                {
                  /* Connection closed */
                  net->connected = FALSE;
                  U64_DEBUG ("Connection closed during line receive");
                  break;
                }

              net->recv_buffer_len = result;
              net->recv_buffer_pos = 0;
              retry_count = 0; /* Reset retry count on successful receive */
              c = net->recv_buffer[net->recv_buffer_pos++];
            }
          else
            {
              continue;
            }
        }

      if (c == '\r')
        {
          /* Expect \n next */
          continue;
        }
      if (c == '\n')
        {
          /* End of line */
          break;
        }

      buffer[pos++] = c;
    }

  buffer[pos] = '\0';
  U64_DEBUG ("Received line (%lu chars): %s", (unsigned long)pos, buffer);
  return pos;
#else
  return U64_ERR_NOTIMPL;
#endif
}