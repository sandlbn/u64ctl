#ifndef ULTIMATE64_PRIVATE_H
#define ULTIMATE64_PRIVATE_H

#include "ultimate64_amiga.h"
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/types.h>

/* Connection structure (internal) */
struct U64Connection
{
  STRPTR host;
  STRPTR password;
  UWORD port;
  LONG last_error;

  /* For HTTP communication */
  STRPTR url_prefix;

  /* Network connection */
  APTR net_connection;

/* Async support */
#ifdef U64_ASYNC_SUPPORT
  struct MsgPort *reply_port;
  struct IORequest *io_request;
  U64AsyncCallback async_callback;
  APTR async_userdata;
  BOOL async_pending;
#endif
};

#ifdef USE_BSDSOCKET
extern struct Library *SocketBase;
extern int errno_storage;
#endif

/* HTTP methods */
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTP_PUT 2
#define HTTP_DELETE 3

/* HTTP response codes */
#define HTTP_OK 200
#define HTTP_NO_CONTENT 204
#define HTTP_BAD_REQUEST 400
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_NOT_IMPLEMENTED 501
#define HTTP_TIMEOUT 504

/* Buffer sizes */
#define HTTP_BUFFER_SIZE 4096
#define HTTP_HEADER_SIZE 1024
#define JSON_BUFFER_SIZE 2048

/* Internal HTTP request structure */
typedef struct
{
  UBYTE method;
  STRPTR path;
  STRPTR content_type;
  UBYTE *body;
  ULONG body_size;
  STRPTR response;
  ULONG response_size;
  UWORD status_code;
} HttpRequest;

/* Internal functions */
LONG U64_HttpRequest (U64Connection *conn, HttpRequest *req);
LONG U64_HttpPostMultipart (U64Connection *conn, CONST_STRPTR path,
                            CONST_STRPTR field_name, CONST_STRPTR filename,
                            CONST_STRPTR file_content_type,
                            CONST UBYTE *file_data, ULONG file_size,
                            CONST_STRPTR *extra_fields,
                            CONST_STRPTR *extra_values, ULONG num_extras);
LONG U64_ParseJSON (CONST_STRPTR json, CONST_STRPTR key, STRPTR value,
                    ULONG value_size);
STRPTR U64_BuildURL (U64Connection *conn, CONST_STRPTR path);
void U64_FreeURL (STRPTR url);

/* Network abstraction layer */
LONG U64_NetInit (void);
void U64_NetCleanup (void);
LONG U64_NetConnect (U64Connection *conn);
void U64_NetDisconnect (U64Connection *conn);
LONG U64_NetSend (U64Connection *conn, CONST UBYTE *data, ULONG size);
LONG U64_NetReceive (U64Connection *conn, UBYTE *buffer, ULONG size);
LONG U64_NetReceiveLine (U64Connection *conn, STRPTR buffer, ULONG max_size);

/* JSON parsing helpers */
typedef struct
{
  CONST_STRPTR json;
  ULONG length;
  ULONG position;
} JsonParser;

BOOL U64_JsonInit (JsonParser *parser, CONST_STRPTR json);
BOOL U64_JsonFindKey (JsonParser *parser, CONST_STRPTR key);
BOOL U64_JsonGetString (JsonParser *parser, STRPTR buffer, ULONG buffer_size);
BOOL U64_JsonGetNumber (JsonParser *parser, LONG *value);
BOOL U64_JsonGetBool (JsonParser *parser, BOOL *value);
LONG U64_ParseDeviceInfo (CONST_STRPTR json, U64DeviceInfo *info);
void U64_FreeDeviceInfo (U64DeviceInfo *info);
LONG U64_ParseDeviceInfo (CONST_STRPTR json, U64DeviceInfo *info);
LONG U64_DownloadToFile(CONST_STRPTR url, CONST_STRPTR local_filename, 
                        void (*progress_callback)(ULONG bytes, APTR userdata), 
                        APTR userdata);
/* Debug system that respects global verbose flag */
extern BOOL g_u64_verbose_mode;

/* Debug functions */
void U64_SetVerboseMode (BOOL verbose);
void U64_DebugPrintf (CONST_STRPTR format, ...);

#define U64_DEBUG(format, ...)                                                \
  do                                                                          \
    {                                                                         \
      if (g_u64_verbose_mode)                                                 \
        {                                                                     \
          U64_DebugPrintf (format, ##__VA_ARGS__);                            \
        }                                                                     \
    }                                                                         \
  while (0)

#endif /* ULTIMATE64_PRIVATE_H */