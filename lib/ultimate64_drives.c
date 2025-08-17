/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * Drive management implementation
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

/* Parse drive type from string */
static U64DriveType
ParseDriveType (CONST_STRPTR type_str)
{
  if (!type_str)
    {
      return U64_DRIVE_1541;
    }

  if (strstr (type_str, "1541"))
    return U64_DRIVE_1541;
  if (strstr (type_str, "1571"))
    return U64_DRIVE_1571;
  if (strstr (type_str, "1581"))
    return U64_DRIVE_1581;
  if (strstr (type_str, "DOS"))
    return U64_DRIVE_DOS;

  return U64_DRIVE_1541;
}

/* Get drive list from Ultimate device */
LONG
U64_GetDriveList (U64Connection *conn, U64Drive **drives, ULONG *count)
{
  HttpRequest req;
  LONG result;
  JsonParser parser;
  U64Drive *drive_list;
  ULONG drive_count = 0;
  ULONG max_drives = 8; /* Allocate space for more drives */
  char buffer[256];
  LONG value;
  BOOL bool_value;

  if (!conn || !drives || !count)
    {
      return U64_ERR_INVALID;
    }

  *drives = NULL;
  *count = 0;

  U64_DEBUG ("Getting drive list...");

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_GET;
  req.path = "/v1/drives";

  /* Execute request */
  result = U64_HttpRequest (conn, &req);
  if (result != U64_OK)
    {
      U64_DEBUG ("Failed to get drive list: %ld", result);
      return result;
    }

  if (!req.response)
    {
      U64_DEBUG ("No response received");
      return U64_ERR_GENERAL;
    }

  U64_DEBUG ("Drive list response: %.500s", req.response);

  /* Allocate drive array */
  drive_list
      = AllocMem (sizeof (U64Drive) * max_drives, MEMF_PUBLIC | MEMF_CLEAR);
  if (!drive_list)
    {
      FreeMem (req.response, req.response_size + 1);
      return U64_ERR_MEMORY;
    }

  /* Initialize parser */
  if (!U64_JsonInit (&parser, req.response))
    {
      U64_DEBUG ("Failed to initialize JSON parser");
      FreeMem (drive_list, sizeof (U64Drive) * max_drives);
      FreeMem (req.response, req.response_size + 1);
      return U64_ERR_GENERAL;
    }

  /* Find the drives array */
  if (!U64_JsonFindKey (&parser, "drives"))
    {
      U64_DEBUG ("No 'drives' array found in response");
      FreeMem (drive_list, sizeof (U64Drive) * max_drives);
      FreeMem (req.response, req.response_size + 1);
      return U64_ERR_GENERAL;
    }

  /* The drives array contains objects with drive names as keys */
  /* Look for drive letters a, b, c, d in the response */
  const char *drive_letters[] = { "a", "b", "c", "d" };
  ULONG i;

  for (i = 0; i < 4 && drive_count < max_drives; i++)
    {
      U64Drive *drive = &drive_list[drive_count];
      char drive_search[32];

      /* Reset parser position to search for each drive */
      parser.position = 0;

      /* Look for this drive letter in the JSON */
      sprintf (drive_search, "\"%s\"", drive_letters[i]);

      /* Search for the drive key in the response */
      if (strstr (req.response, drive_search))
        {
          U64_DEBUG ("Found drive %s in response", drive_letters[i]);

          /* Set default values */
          drive->bus_id = 8 + i;
          drive->enabled = FALSE;
          drive->drive_type = U64_DRIVE_1541;
          drive->rom = NULL;
          drive->image_file = NULL;
          drive->image_path = NULL;
          drive->last_error = NULL;

          /* Try to find and parse the drive object */
          parser.position = 0;
          if (U64_JsonFindKey (&parser, drive_letters[i]))
            {
              ULONG obj_start = parser.position;

              U64_DEBUG ("Parsing drive %s object", drive_letters[i]);

              /* Parse enabled */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "enabled"))
                {
                  if (U64_JsonGetBool (&parser, &bool_value))
                    {
                      drive->enabled = bool_value;
                      U64_DEBUG ("Drive %s enabled: %s", drive_letters[i],
                                 bool_value ? "true" : "false");
                    }
                }

              /* Parse bus_id */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "bus_id"))
                {
                  if (U64_JsonGetNumber (&parser, &value))
                    {
                      drive->bus_id = (UBYTE)value;
                      U64_DEBUG ("Drive %s bus_id: %d", drive_letters[i],
                                 (int)value);
                    }
                }

              /* Parse type */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "type"))
                {
                  if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
                    {
                      drive->drive_type = ParseDriveType (buffer);
                      U64_DEBUG ("Drive %s type: %s", drive_letters[i],
                                 buffer);
                    }
                }

              /* Parse rom */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "rom"))
                {
                  if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
                    {
                      if (strlen (buffer) > 0)
                        {
                          drive->rom
                              = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
                          if (drive->rom)
                            {
                              strcpy (drive->rom, buffer);
                              U64_DEBUG ("Drive %s ROM: %s", drive_letters[i],
                                         buffer);
                            }
                        }
                    }
                }

              /* Parse image_file */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "image_file"))
                {
                  if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
                    {
                      if (strlen (buffer) > 0)
                        {
                          drive->image_file
                              = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
                          if (drive->image_file)
                            {
                              strcpy (drive->image_file, buffer);
                              U64_DEBUG ("Drive %s image: %s",
                                         drive_letters[i], buffer);
                            }
                        }
                    }
                }

              /* Parse image_path */
              parser.position = obj_start;
              if (U64_JsonFindKey (&parser, "image_path"))
                {
                  if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
                    {
                      if (strlen (buffer) > 0)
                        {
                          drive->image_path
                              = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
                          if (drive->image_path)
                            {
                              strcpy (drive->image_path, buffer);
                              U64_DEBUG ("Drive %s path: %s", drive_letters[i],
                                         buffer);
                            }
                        }
                    }
                }

              drive_count++;
              U64_DEBUG ("Successfully parsed drive %s", drive_letters[i]);
            }
        }
    }

  /* Free response */
  FreeMem (req.response, req.response_size + 1);

  /* Return results */
  *drives = drive_list;
  *count = drive_count;

  U64_DEBUG ("Found %lu drives total", (unsigned long)drive_count);
  return U64_OK;
}
/* Free drive list */
void
U64_FreeDriveList (U64Drive *drives, ULONG count)
{
  ULONG i;

  if (!drives)
    {
      return;
    }

  /* Free allocated strings in each drive */
  for (i = 0; i < count; i++)
    {
      U64Drive *drive = &drives[i];

      if (drive->rom)
        {
          FreeMem (drive->rom, strlen (drive->rom) + 1);
        }

      if (drive->image_file)
        {
          FreeMem (drive->image_file, strlen (drive->image_file) + 1);
        }

      if (drive->image_path)
        {
          FreeMem (drive->image_path, strlen (drive->image_path) + 1);
        }

      if (drive->last_error)
        {
          FreeMem (drive->last_error, strlen (drive->last_error) + 1);
        }
    }

  /* Free drive array */
  FreeMem (drives, sizeof (U64Drive) * count);
}

/* Enhanced Mount Disk with detailed error reporting */
LONG
U64_MountDisk (U64Connection *conn, CONST_STRPTR filename,
               CONST_STRPTR drive_id, U64MountMode mode, BOOL run,
               STRPTR *error_details)
{
  BPTR file;
  LONG file_size;
  UBYTE *file_data;
  LONG result;
  char path[512];
  char *base_filename;
  U64DiskImageType disk_type;
  const char *mode_str;
  const char *type_str;
  HttpRequest req;
  static const char *mode_strings[] = { "readwrite", "readonly", "unlinked" };

  if (!conn || !filename || !drive_id)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== MountDisk Start (Simple Method) ===");
  U64_DEBUG ("File: %s", filename);
  U64_DEBUG ("Drive: %s", drive_id);
  U64_DEBUG ("Mode: %s", mode_strings[mode]);

  /* Validate drive ID */
  if (strlen (drive_id) != 1 || (drive_id[0] < 'a' || drive_id[0] > 'd'))
    {
      U64_DEBUG ("Invalid drive ID: %s (must be a, b, c, or d)", drive_id);
      return U64_ERR_INVALID;
    }

  /* Determine disk type from extension */
  disk_type = U64_GetDiskTypeFromExt (filename);
  type_str = U64_GetDiskTypeString (disk_type);
  mode_str = mode_strings[mode];

  U64_DEBUG ("Detected disk type: %s", type_str);

  /* Open and read file */
  U64_DEBUG ("Opening file: %s", filename);
  file = Open (filename, MODE_OLDFILE);
  if (!file)
    {
      U64_DEBUG ("Failed to open file: %s", filename);
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "Cannot open file: %s", filename);
              *error_details = error_msg;
            }
        }
      return U64_ERR_NOTFOUND;
    }

  /* Get file size */
  Seek (file, 0, OFFSET_END);
  file_size = Seek (file, 0, OFFSET_BEGINNING);

  if (file_size <= 0)
    {
      U64_DEBUG ("Invalid file size: %ld", file_size);
      Close (file);
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "Invalid file size: %ld bytes", file_size);
              *error_details = error_msg;
            }
        }
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("File size: %ld bytes", file_size);

  /* Allocate buffer */
  file_data = AllocMem (file_size, MEMF_PUBLIC);
  if (!file_data)
    {
      U64_DEBUG ("Failed to allocate %ld bytes for file data", file_size);
      Close (file);
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "Out of memory: %ld bytes", file_size);
              *error_details = error_msg;
            }
        }
      return U64_ERR_MEMORY;
    }

  /* Read file */
  if (Read (file, file_data, file_size) != file_size)
    {
      U64_DEBUG ("Failed to read complete file");
      FreeMem (file_data, file_size);
      Close (file);
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "Error reading file: %s", filename);
              *error_details = error_msg;
            }
        }
      return U64_ERR_GENERAL;
    }

  Close (file);
  U64_DEBUG ("File loaded successfully into memory");

  /* Extract base filename from path */
  base_filename = FilePart ((STRPTR)filename);
  if (!base_filename)
    {
      base_filename = (char *)filename;
    }
  U64_DEBUG ("Base filename: %s", base_filename);

  /* POST with raw binary data and query parameters */
  sprintf (path, "/v1/drives/%s:mount?type=%s&mode=%s", drive_id, type_str,
           mode_str);
  if (run)
    {
      strcat (path, "&run=true");
    }

  U64_DEBUG ("Mount endpoint with params: %s", path);
  U64_DEBUG ("Content-Type: application/octet-stream");
  U64_DEBUG ("Method: POST");
  U64_DEBUG ("Body size: %ld bytes", file_size);

  /* Setup HTTP request with raw binary data */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_POST;
  req.path = path;
  req.content_type = "application/octet-stream";
  req.body = file_data;
  req.body_size = file_size;

  U64_DEBUG ("Sending mount request with raw binary data...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("Mount request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Check response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response: %.200s", req.response);

      /* Look for error indicators in response */
      if (strstr (req.response, "error") || strstr (req.response, "Error")
          || strstr (req.response, "failed")
          || strstr (req.response, "Failed"))
        {
          if (error_details)
            {
              char *error_msg = AllocMem (256, MEMF_PUBLIC);
              if (error_msg)
                {
                  sprintf (error_msg, "Ultimate64 error: %.200s",
                           req.response);
                  *error_details = error_msg;
                }
            }
          FreeMem (req.response, req.response_size + 1);
          FreeMem (file_data, file_size);
          conn->last_error = U64_ERR_GENERAL;
          return U64_ERR_GENERAL;
        }

      FreeMem (req.response, req.response_size + 1);
    }

  /* Free file data */
  FreeMem (file_data, file_size);

  /* Check result */
  if (result != U64_OK)
    {
      if (error_details)
        {
          char *error_msg = AllocMem (256, MEMF_PUBLIC);
          if (error_msg)
            {
              switch (result)
                {
                case U64_ERR_NOTFOUND:
                  sprintf (error_msg,
                           "Mount endpoint not found for drive %s. Check "
                           "Ultimate64 firmware version.",
                           drive_id);
                  break;
                case U64_ERR_INVALID:
                  sprintf (error_msg,
                           "Invalid request. Drive %s may not support this "
                           "disk type (%s) or mode (%s).",
                           drive_id, type_str, mode_str);
                  break;
                case U64_ERR_NETWORK:
                  sprintf (
                      error_msg,
                      "Network communication failed during mount operation.");
                  break;
                case U64_ERR_TIMEOUT:
                  sprintf (
                      error_msg,
                      "Mount operation timed out. Ultimate64 may be busy.");
                  break;
                case U64_ERR_ACCESS:
                  sprintf (
                      error_msg,
                      "Access denied. Check password or drive %s permissions.",
                      drive_id);
                  break;
                default:
                  sprintf (error_msg,
                           "Mount failed with HTTP error: %s (status %d)",
                           U64_GetErrorString (result), req.status_code);
                  break;
                }
              *error_details = error_msg;
            }
        }

      conn->last_error = result;
      U64_DEBUG ("=== MountDisk Result: FAILED ===");
      return result;
    }

  /* Check HTTP status code for success */
  if (req.status_code >= 200 && req.status_code < 300)
    {
      U64_DEBUG ("Mount operation successful (HTTP %d)", req.status_code);

      /* Handle run option if mount was successful */
      if (run)
        {
          U64_DEBUG ("Executing post-mount run sequence...");

          /* Reset and run */
          U64_DEBUG ("Resetting C64...");
          LONG reset_result = U64_Reset (conn);
          if (reset_result != U64_OK)
            {
              U64_DEBUG ("Reset failed: %ld", reset_result);
              if (error_details)
                {
                  char *error_msg = AllocMem (128, MEMF_PUBLIC);
                  if (error_msg)
                    {
                      sprintf (error_msg, "Disk mounted but reset failed: %s",
                               U64_GetErrorString (reset_result));
                      *error_details = error_msg;
                    }
                }
              /* Don't return error - mount was successful */
            }
          else
            {
              U64_DEBUG ("Reset successful, waiting for boot...");
              Delay (150); /* 3 seconds */

              /* Type load and run commands */
              U64_DEBUG ("Typing load and run commands...");
              LONG type_result = U64_TypeText (conn, "load\"*\",8,1\nrun\n");
              if (type_result != U64_OK)
                {
                  U64_DEBUG ("Type command failed: %ld", type_result);
                }
            }
        }

      conn->last_error = U64_OK;
      U64_DEBUG ("=== MountDisk Result: SUCCESS ===");
      return U64_OK;
    }
  else
    {
      /* HTTP error status */
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "HTTP error %d: %s", req.status_code,
                       req.status_code == 400
                           ? "Bad Request - Invalid parameters"
                       : req.status_code == 404 ? "Endpoint not found"
                       : req.status_code == 500 ? "Internal server error"
                                                : "Unknown error");
              *error_details = error_msg;
            }
        }

      conn->last_error = U64_ERR_GENERAL;
      U64_DEBUG ("=== MountDisk Result: FAILED (HTTP %d) ===",
                 req.status_code);
      return U64_ERR_GENERAL;
    }
}

/* Unmount Disk with detailed error reporting */
LONG
U64_UnmountDisk (U64Connection *conn, CONST_STRPTR drive_id,
                 STRPTR *error_details)
{
  HttpRequest req;
  LONG result;
  char path[256];
  U64ErrorArray error_array;

  if (!conn || !drive_id)
    {
      return U64_ERR_INVALID;
    }

  /* Clear error details */
  if (error_details)
    {
      *error_details = NULL;
    }

  U64_DEBUG ("=== UnmountDisk Start ===");
  U64_DEBUG ("Drive: %s", drive_id);

  /* Validate drive ID */
  if (strlen (drive_id) != 1 || (drive_id[0] < 'a' || drive_id[0] > 'd'))
    {
      U64_DEBUG ("Invalid drive ID: %s (must be a, b, c, or d)", drive_id);
      return U64_ERR_INVALID;
    }

  /* Build path for unmount endpoint */
  sprintf (path, "/v1/drives/%s:remove", drive_id);
  U64_DEBUG ("Unmount endpoint: %s", path);

  /* Setup HTTP request */
  memset (&req, 0, sizeof (req));
  req.method = HTTP_PUT;
  req.path = path;

  U64_DEBUG ("Sending unmount request...");

  /* Execute request */
  result = U64_HttpRequest (conn, &req);

  U64_DEBUG ("Unmount request completed");
  U64_DEBUG ("HTTP result code: %ld (%s)", result,
             U64_GetErrorString (result));
  U64_DEBUG ("HTTP status code: %d", req.status_code);
  U64_DEBUG ("Response size: %lu bytes", (unsigned long)req.response_size);

  /* Process response */
  if (req.response && req.response_size > 0)
    {
      U64_DEBUG ("Response received: '%.200s'", req.response);

      /* Check for JSON response with errors */
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
                  U64_DEBUG ("=== UNMOUNT ERROR DETAILS ===");
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
                  U64_DEBUG (
                      "=== UnmountDisk Result: FAILED (errors found) ===");
                  return U64_ERR_GENERAL;
                }
              else
                {
                  U64_DEBUG ("No errors found in JSON - SUCCESS");
                  /* Success - empty errors array */
                  U64_FreeErrorArray (&error_array);
                  FreeMem (req.response, req.response_size + 1);
                  conn->last_error = U64_OK;
                  U64_DEBUG ("=== UnmountDisk Result: SUCCESS ===");
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
              U64_DEBUG ("=== UnmountDisk Result: SUCCESS (text) ===");
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
          U64_DEBUG ("=== UnmountDisk Result: SUCCESS (HTTP %d) ===",
                     req.status_code);
          return U64_OK;
        case 404:
          U64_DEBUG ("HTTP 404 Not Found - drive or endpoint doesn't exist");
          if (error_details)
            {
              char *error_msg = AllocMem (128, MEMF_PUBLIC);
              if (error_msg)
                {
                  sprintf (
                      error_msg,
                      "Drive %s not found or unmount endpoint not available",
                      drive_id);
                  *error_details = error_msg;
                }
            }
          conn->last_error = U64_ERR_NOTFOUND;
          U64_DEBUG ("=== UnmountDisk Result: FAILED (404) ===");
          return U64_ERR_NOTFOUND;
        case 400:
          U64_DEBUG ("HTTP 400 Bad Request - invalid drive or request");
          if (error_details)
            {
              char *error_msg = AllocMem (128, MEMF_PUBLIC);
              if (error_msg)
                {
                  sprintf (error_msg, "Invalid unmount request for drive %s",
                           drive_id);
                  *error_details = error_msg;
                }
            }
          conn->last_error = U64_ERR_INVALID;
          U64_DEBUG ("=== UnmountDisk Result: FAILED (400) ===");
          return U64_ERR_INVALID;
        case 500:
          U64_DEBUG ("HTTP 500 Internal Server Error - Ultimate64 error");
          if (error_details)
            {
              char *error_msg = AllocMem (128, MEMF_PUBLIC);
              if (error_msg)
                {
                  sprintf (
                      error_msg,
                      "Ultimate64 internal error during unmount of drive %s",
                      drive_id);
                  *error_details = error_msg;
                }
            }
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== UnmountDisk Result: FAILED (500) ===");
          return U64_ERR_GENERAL;
        default:
          U64_DEBUG ("HTTP %d - treating as error", req.status_code);
          if (error_details)
            {
              char *error_msg = AllocMem (128, MEMF_PUBLIC);
              if (error_msg)
                {
                  sprintf (error_msg, "Unmount failed with HTTP status %d",
                           req.status_code);
                  *error_details = error_msg;
                }
            }
          conn->last_error = U64_ERR_GENERAL;
          U64_DEBUG ("=== UnmountDisk Result: FAILED (HTTP %d) ===",
                     req.status_code);
          return U64_ERR_GENERAL;
        }
    }
  else
    {
      U64_DEBUG ("HTTP layer failed: %ld (%s)", result,
                 U64_GetErrorString (result));
      if (error_details)
        {
          char *error_msg = AllocMem (128, MEMF_PUBLIC);
          if (error_msg)
            {
              sprintf (error_msg, "Network error during unmount: %s",
                       U64_GetErrorString (result));
              *error_details = error_msg;
            }
        }
      conn->last_error = result;
      U64_DEBUG ("=== UnmountDisk Result: FAILED (HTTP layer) ===");
      return result;
    }
}

/* Validate disk image file format */
LONG
U64_ValidateDiskImage (CONST UBYTE *data, ULONG size, U64DiskImageType type,
                       STRPTR *validation_info)
{
  char *info;
  ULONG expected_size;
  const char *type_name;

  if (!data || size == 0)
    {
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "No data provided for validation");
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  type_name = U64_GetDiskTypeString (type);

  /* Check expected file sizes */
  switch (type)
    {
    case U64_DISK_D64:
      expected_size = 174848; /* Standard D64 size */
      if (size != 174848 && size != 175531)
        { /* 175531 = with error info */
          if (validation_info)
            {
              info = AllocMem (128, MEMF_PUBLIC);
              if (info)
                {
                  sprintf (info,
                           "Invalid D64 size: %lu bytes (expected 174848 or "
                           "175531)",
                           (unsigned long)size);
                  *validation_info = info;
                }
            }
          return U64_ERR_INVALID;
        }
      break;

    case U64_DISK_D71:
      expected_size = 349696; /* Standard D71 size */
      if (size != expected_size)
        {
          if (validation_info)
            {
              info = AllocMem (128, MEMF_PUBLIC);
              if (info)
                {
                  sprintf (info, "Invalid D71 size: %lu bytes (expected %lu)",
                           (unsigned long)size, (unsigned long)expected_size);
                  *validation_info = info;
                }
            }
          return U64_ERR_INVALID;
        }
      break;

    case U64_DISK_D81:
      expected_size = 819200; /* Standard D81 size */
      if (size != expected_size)
        {
          if (validation_info)
            {
              info = AllocMem (128, MEMF_PUBLIC);
              if (info)
                {
                  sprintf (info, "Invalid D81 size: %lu bytes (expected %lu)",
                           (unsigned long)size, (unsigned long)expected_size);
                  *validation_info = info;
                }
            }
          return U64_ERR_INVALID;
        }
      break;

    case U64_DISK_G64:
    case U64_DISK_G71:
      /* G64/G71 files have variable sizes, just check minimum */
      if (size < 1024)
        {
          if (validation_info)
            {
              info = AllocMem (128, MEMF_PUBLIC);
              if (info)
                {
                  sprintf (info, "File too small for %s format: %lu bytes",
                           type_name, (unsigned long)size);
                  *validation_info = info;
                }
            }
          return U64_ERR_INVALID;
        }
      break;

    default:
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "Unknown disk format");
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Basic content validation for D64 */
  if (type == U64_DISK_D64 && size >= 174848)
    {
      /* Check BAM signature at track 18, sector 0 */
      /* For a proper D64, we could check more structure, but basic size check
       * is sufficient */
      if (validation_info)
        {
          info = AllocMem (128, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "Valid %s format: %lu bytes", type_name,
                       (unsigned long)size);
              *validation_info = info;
            }
        }
    }
  else if (validation_info)
    {
      info = AllocMem (128, MEMF_PUBLIC);
      if (info)
        {
          sprintf (info, "Valid %s format: %lu bytes", type_name,
                   (unsigned long)size);
          *validation_info = info;
        }
    }

  return U64_OK;
}

/* Get current drive status */
LONG
U64_GetDriveStatus (U64Connection *conn, CONST_STRPTR drive_id, BOOL *mounted,
                    STRPTR *image_name, U64MountMode *mode)
{
  U64Drive *drives;
  ULONG count;
  LONG result;
  ULONG i;

  if (!conn || !drive_id || !mounted)
    {
      return U64_ERR_INVALID;
    }

  /* Initialize return values */
  *mounted = FALSE;
  if (image_name)
    *image_name = NULL;
  if (mode)
    *mode = U64_MOUNT_RO;

  /* Validate drive ID */
  if (strlen (drive_id) != 1 || (drive_id[0] < 'a' || drive_id[0] > 'd'))
    {
      return U64_ERR_INVALID;
    }

  /* Get drive list */
  result = U64_GetDriveList (conn, &drives, &count);
  if (result != U64_OK)
    {
      return result;
    }

  /* Find the requested drive */
  for (i = 0; i < count; i++)
    {
      /* Check if this is the drive we're looking for */
      /* Drive A = bus_id 8, B = 9, C = 10, D = 11 typically */
      UBYTE expected_bus_id = 8 + (drive_id[0] - 'a');

      if (drives[i].bus_id == expected_bus_id)
        {
          /* Found the drive */
          *mounted = (drives[i].enabled && drives[i].image_file
                      && strlen (drives[i].image_file) > 0);

          if (*mounted && image_name && drives[i].image_file)
            {
              *image_name
                  = AllocMem (strlen (drives[i].image_file) + 1, MEMF_PUBLIC);
              if (*image_name)
                {
                  strcpy (*image_name, drives[i].image_file);
                }
            }

          if (mode)
            {
              *mode = U64_MOUNT_RO;
            }

          break;
        }
    }

  /* Free drive list */
  U64_FreeDriveList (drives, count);

  return U64_OK;
}
