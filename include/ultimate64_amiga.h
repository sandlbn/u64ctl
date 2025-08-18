#ifndef ULTIMATE64_AMIGA_H
#define ULTIMATE64_AMIGA_H

/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/dos.h>

#ifdef USE_BSDSOCKET
#include <netinet/in.h>
#include <proto/socket.h>
#include <sys/socket.h>
#endif

/* Library version */
#define ULTIMATE64_VERSION_MAJOR 0
#define ULTIMATE64_VERSION_MINOR 2
#define ULTIMATE64_VERSION_PATCH 0

/* Error codes */
#define U64_OK 0
#define U64_ERR_GENERAL -1
#define U64_ERR_MEMORY -2
#define U64_ERR_NETWORK -3
#define U64_ERR_NOTFOUND -4
#define U64_ERR_INVALID -5
#define U64_ERR_OVERFLOW -6
#define U64_ERR_ACCESS -7
#define U64_ERR_NOTIMPL -8
#define U64_ERR_TIMEOUT -9

/* Constants */
#define U64_BASIC_LOAD_ADDR 0x0801
#define U64_KEYBOARD_BUFFER 0x0277
#define U64_KEYBOARD_NDX 0x00C6
#define U64_KEYBOARD_LSTX 0x00C5
#define U64_MAX_PATH 256
#define U64_MAX_HOST 256

/* Device types */
typedef enum
{
  U64_PRODUCT_UNKNOWN = 0,
  U64_PRODUCT_ULTIMATE64,
  U64_PRODUCT_ULTIMATE2,
  U64_PRODUCT_ULTIMATE2PLUS
} U64ProductType;

/* Drive types */
typedef enum
{
  U64_DRIVE_1541 = 0,
  U64_DRIVE_1571,
  U64_DRIVE_1581,
  U64_DRIVE_DOS
} U64DriveType;

/* Disk image types */
typedef enum
{
  U64_DISK_D64 = 0,
  U64_DISK_G64,
  U64_DISK_D71,
  U64_DISK_G71,
  U64_DISK_D81
} U64DiskImageType;

/* Mount modes */
typedef enum
{
  U64_MOUNT_RW = 0, /* Read/Write */
  U64_MOUNT_RO,     /* Read Only */
  U64_MOUNT_UL      /* Unlinked */
} U64MountMode;

/* Device information structure */
typedef struct
{
  U64ProductType product;
  STRPTR product_name;
  STRPTR firmware_version;
  STRPTR fpga_version;
  STRPTR core_version; /* May be NULL for Ultimate-II */
  STRPTR hostname;
  STRPTR unique_id; /* May be NULL if disabled */
} U64DeviceInfo;

/* Drive information structure */
typedef struct
{
  UBYTE bus_id;
  BOOL enabled;
  U64DriveType drive_type;
  STRPTR rom;        /* ROM file, may be NULL */
  STRPTR image_file; /* Current image file, may be NULL */
  STRPTR image_path; /* Full path to image, may be NULL */
  STRPTR last_error; /* Last error message, may be NULL */
} U64Drive;

/* Error array structure */
typedef struct
{
  STRPTR *errors;
  ULONG error_count;
} U64ErrorArray;

/* Connection handle (opaque) */
typedef struct U64Connection U64Connection;

/* Library initialization/cleanup */
BOOL U64_InitLibrary (void);
void U64_CleanupLibrary (void);

void U64_SetVerboseMode (BOOL verbose);

/* Connection management */
U64Connection *U64_Connect (CONST_STRPTR host, CONST_STRPTR password);
void U64_Disconnect (U64Connection *conn);
LONG U64_GetLastError (U64Connection *conn);
CONST_STRPTR U64_GetErrorString (LONG error);

/* Device information */
LONG U64_GetDeviceInfo (U64Connection *conn, U64DeviceInfo *info);
void U64_FreeDeviceInfo (U64DeviceInfo *info);
CONST_STRPTR U64_GetVersion (U64Connection *conn);

/* Machine control */
LONG U64_Reset (U64Connection *conn);
LONG U64_Reboot (U64Connection *conn);
LONG U64_PowerOff (U64Connection *conn);
LONG U64_Pause (U64Connection *conn);
LONG U64_Resume (U64Connection *conn);
LONG U64_MenuButton (U64Connection *conn);

/* Memory operations */
LONG U64_WriteMem (U64Connection *conn, UWORD address, CONST UBYTE *data,
                   UWORD length);
LONG U64_ReadMem (U64Connection *conn, UWORD address, UBYTE *buffer,
                  UWORD length);
LONG U64_Poke (U64Connection *conn, UWORD address, UBYTE value);
UBYTE U64_Peek (U64Connection *conn, UWORD address);
UWORD U64_ReadWord (U64Connection *conn, UWORD address);

/* Program loading and execution */
LONG U64_LoadFile (U64Connection *conn, CONST_STRPTR filename,
                   UWORD *load_address);
LONG U64_RunFile (U64Connection *conn, CONST_STRPTR filename);

/* Enhanced program loading with error details */
LONG U64_LoadPRG (U64Connection *conn, CONST UBYTE *data, ULONG size,
                  STRPTR *error_details);
LONG U64_RunPRG (U64Connection *conn, CONST UBYTE *data, ULONG size,
                 STRPTR *error_details);
LONG U64_RunCRT (U64Connection *conn, CONST UBYTE *data, ULONG size,
                 STRPTR *error_details);

/* Drive operations */
LONG U64_GetDriveList (U64Connection *conn, U64Drive **drives, ULONG *count);
void U64_FreeDriveList (U64Drive *drives, ULONG count);
/* Enhanced disk operations with error reporting */
LONG U64_MountDisk (U64Connection *conn, CONST_STRPTR filename,
                    CONST_STRPTR drive_id, U64MountMode mode, BOOL run,
                    STRPTR *error_details);
LONG U64_UnmountDisk (U64Connection *conn, CONST_STRPTR drive_id,
                      STRPTR *error_details);
/* Disk image validation */
LONG U64_ValidateDiskImage (CONST UBYTE *data, ULONG size,
                            U64DiskImageType type, STRPTR *validation_info);

/* Drive status checking */
LONG U64_GetDriveStatus (U64Connection *conn, CONST_STRPTR drive_id,
                         BOOL *mounted, STRPTR *image_name,
                         U64MountMode *mode);

/* Convenience macros for common drive operations */
#define U64_MountDiskA(conn, filename, mode, run, errors)                     \
  U64_MountDisk (conn, filename, "a", mode, run, errors)

#define U64_MountDiskB(conn, filename, mode, run, errors)                     \
  U64_MountDisk (conn, filename, "b", mode, run, errors)

#define U64_UnmountDiskA(conn, errors) U64_UnmountDisk (conn, "a", errors)

#define U64_UnmountDiskB(conn, errors) U64_UnmountDisk (conn, "b", errors)

/* Keyboard emulation */
LONG U64_TypeText (U64Connection *conn, CONST_STRPTR text);
BOOL U64_IsBasicReady (U64Connection *conn);

/* Audio playback */
LONG U64_PlaySID (U64Connection *conn, CONST UBYTE *data, ULONG size,
                  UBYTE song_num, STRPTR *error_details);
LONG U64_PlayMOD (U64Connection *conn, CONST UBYTE *data, ULONG size,
                  STRPTR *error_details);

/* File validation */
LONG U64_ValidateSIDFile (CONST UBYTE *data, ULONG size,
                          STRPTR *validation_info);
LONG U64_ValidateMODFile (CONST UBYTE *data, ULONG size,
                          STRPTR *validation_info);
LONG U64_ValidatePRGFile (CONST UBYTE *data, ULONG size,
                          STRPTR *validation_info);
LONG U64_ValidateCRTFile (CONST UBYTE *data, ULONG size,
                          STRPTR *validation_info);

/* Error handling functions */
LONG U64_ParseErrorArray (CONST_STRPTR json, U64ErrorArray *error_array);
void U64_FreeErrorArray (U64ErrorArray *error_array);
STRPTR U64_FormatErrorArray (U64ErrorArray *error_array);

/* Utility functions */
U64DiskImageType U64_GetDiskTypeFromExt (CONST_STRPTR filename);
CONST_STRPTR U64_GetDiskTypeString (U64DiskImageType type);
CONST_STRPTR U64_GetDriveTypeString (U64DriveType type);
LONG U64_CheckAddressOverflow (UWORD address, UWORD length);
UWORD U64_ExtractLoadAddress (CONST UBYTE *data, ULONG size);

/* PETSCII conversion functions */
UBYTE *U64_StringToPETSCII (CONST_STRPTR str, ULONG *out_len);
STRPTR U64_PETSCIIToString (CONST UBYTE *petscii, ULONG len);
void U64_FreePETSCII (UBYTE *petscii);

/* VIC Stream functions (if supported) */
#ifdef U64_VICSTREAM_SUPPORT
typedef struct
{
  UWORD width;
  UWORD height;
  UBYTE *data; /* RGB data */
} U64Frame;

LONG U64_CaptureFrame (U64Connection *conn, U64Frame *frame);
void U64_FreeFrame (U64Frame *frame);
LONG U64_SaveFrameIFF (U64Frame *frame, CONST_STRPTR filename);
#endif

/* Async support for MUI */
#ifdef U64_ASYNC_SUPPORT
typedef void (*U64AsyncCallback) (U64Connection *conn, LONG result,
                                  APTR userdata);

LONG U64_ResetAsync (U64Connection *conn, U64AsyncCallback callback,
                     APTR userdata);
LONG U64_LoadPRGAsync (U64Connection *conn, CONST UBYTE *data, ULONG size,
                       U64AsyncCallback callback, APTR userdata);
BOOL U64_CheckAsync (U64Connection *conn);
void U64_CancelAsync (U64Connection *conn);
#endif

#endif /* ULTIMATE64_AMIGA_H */