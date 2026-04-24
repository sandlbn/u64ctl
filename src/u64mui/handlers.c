/* Ultimate64 Control - Do* event handlers
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <libraries/asl.h>

#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mui_app.h"
#include "file_utils.h"
#include "string_utils.h"

/* Connect to Ultimate device */
void
DoConnect (struct AppData *data)
{
  char status[256];

  if (strlen (data->host) == 0)
    {
      UpdateStatus (
          data,
          (CONST_STRPTR) "Please configure host address in Settings menu",
          TRUE);
      return;
    }

  /* Disconnect if already connected */
  if (data->connection)
    {
      U64_Disconnect (data->connection);
      data->connection = NULL;
    }


  /* Connect. Note: U64_Connect only allocates a connection struct —
   * it doesn't touch the network, so it succeeds even with a wrong
   * host or no TCP stack. We probe the device with a real HTTP call
   * (GetDeviceInfo -> GET /v1/info) and only surface "Connected" if
   * that round-trip works. Otherwise we tear the stub down and
   * report the failure, so the button never lies. */
  UpdateStatus (data, (CONST_STRPTR) "Connecting...", TRUE);
  data->connection = U64_Connect (
      (CONST_STRPTR)data->host,
      (strlen (data->password) > 0) ? (CONST_STRPTR)data->password : NULL);
  if (data->connection)
    {
      U64DeviceInfo info;
      LONG probe_rc = U64_GetDeviceInfo (data->connection, &info);
      if (probe_rc != U64_OK)
        {
          sprintf (status, "Connection failed: %s (%s:80 unreachable?)",
                   (char *)U64_GetErrorString (probe_rc), data->host);
          UpdateStatus (data, (CONST_STRPTR)status, TRUE);
          U64_Disconnect (data->connection);
          data->connection = NULL;
          return;
        }

      sprintf (status, "Connected to %s", data->host);
      UpdateStatus (data, (CONST_STRPTR)status, TRUE);

      sprintf (status, "Device: %s (firmware %s)",
               info.product_name ? (char *)info.product_name : "Ultimate",
               info.firmware_version ? (char *)info.firmware_version
                                     : "unknown");
      UpdateStatus (data, (CONST_STRPTR)status, TRUE);
      U64_FreeDeviceInfo (&info);

      /* Enable all controls */
      set (data->btn_reset, MUIA_Disabled, FALSE);
      set (data->btn_reboot, MUIA_Disabled, FALSE);
      set (data->btn_poweroff, MUIA_Disabled, FALSE);
      set (data->btn_pause, MUIA_Disabled, FALSE);
      set (data->btn_resume, MUIA_Disabled, FALSE);
      set (data->btn_menu, MUIA_Disabled, FALSE);
      set (data->btn_load_prg, MUIA_Disabled, FALSE);
      set (data->btn_run_prg, MUIA_Disabled, FALSE);
      set (data->btn_run_crt, MUIA_Disabled, FALSE);
      set (data->btn_type, MUIA_Disabled, FALSE);
      set (data->btn_mount, MUIA_Disabled, FALSE);
      set (data->btn_unmount, MUIA_Disabled, FALSE);
      set (data->btn_peek, MUIA_Disabled, FALSE);
      set (data->btn_poke, MUIA_Disabled, FALSE);
      set (data->btn_play_sid, MUIA_Disabled, FALSE);
      set (data->btn_play_mod, MUIA_Disabled, FALSE);
      set (data->btn_drives_status, MUIA_Disabled, FALSE);
      set (data->btn_connect, MUIA_Text_Contents, (CONST_STRPTR) "Disconnect");

      /* Update disk display */
      UpdateDiskDisplay (data);
    }
  else
    {
      UpdateStatus (data, (CONST_STRPTR) "Connection failed", TRUE);
    }
}

/* Disconnect from Ultimate device */
void
DoDisconnect (struct AppData *data)
{
  if (data->connection)
    {
      U64_Disconnect (data->connection);
      data->connection = NULL;
      UpdateStatus (data, (CONST_STRPTR) "Disconnected", TRUE);

      /* Disable all controls */
      set (data->btn_reset, MUIA_Disabled, TRUE);
      set (data->btn_reboot, MUIA_Disabled, TRUE);
      set (data->btn_poweroff, MUIA_Disabled, TRUE);
      set (data->btn_pause, MUIA_Disabled, TRUE);
      set (data->btn_resume, MUIA_Disabled, TRUE);
      set (data->btn_menu, MUIA_Disabled, TRUE);
      set (data->btn_load_prg, MUIA_Disabled, TRUE);
      set (data->btn_run_prg, MUIA_Disabled, TRUE);
      set (data->btn_run_crt, MUIA_Disabled, TRUE);
      set (data->btn_type, MUIA_Disabled, TRUE);
      set (data->btn_mount, MUIA_Disabled, TRUE);
      set (data->btn_unmount, MUIA_Disabled, TRUE);
      set (data->btn_peek, MUIA_Disabled, TRUE);
      set (data->btn_poke, MUIA_Disabled, TRUE);
      set (data->btn_play_sid, MUIA_Disabled, TRUE);
      set (data->btn_play_mod, MUIA_Disabled, TRUE);
      set (data->btn_drives_status, MUIA_Disabled, TRUE);
      set (data->btn_connect, MUIA_Text_Contents, (CONST_STRPTR) "Connect");

      /* Clear disk display */
      set (data->txt_disk_status, MUIA_Text_Contents,
           (CONST_STRPTR) "Not connected");
    }
}

/* Machine control functions */
void
DoReset (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_Reset (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "Machine reset", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Reset failed", TRUE);
        }
    }
}

void
DoReboot (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_Reboot (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "Device rebooted", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Reboot failed", TRUE);
        }
    }
}

void
DoPowerOff (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_PowerOff (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "Powered off", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Power off failed", TRUE);
        }
    }
}

void
DoPause (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_Pause (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "C64 paused", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Pause failed", TRUE);
        }
    }
}

void
DoResume (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_Resume (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "C64 resumed", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Resume failed", TRUE);
        }
    }
}

void
DoMenu (struct AppData *data)
{
  if (data->connection)
    {
      if (U64_MenuButton (data->connection) == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "Menu button pressed", TRUE);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Menu button failed", TRUE);
        }
    }
}

/* File operation functions */
void
DoLoadPRG (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];

  if (!data->connection)
    return;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             (CONST_STRPTR) "Select PRG file to load",
                             ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
                             (CONST_STRPTR) "#?.prg", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      UBYTE *file_data;
      ULONG file_size;
      STRPTR error_details = NULL;

      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      file_data = U64_ReadFile ((CONST_STRPTR)filename, &file_size);
      if (file_data)
        {
          if (U64_LoadPRG (data->connection, file_data, file_size,
                           &error_details)
              == U64_OK)
            {
              UpdateStatus (data, (CONST_STRPTR) "PRG file loaded", TRUE);
            }
          else
            {
              char error_msg[512];
              sprintf (error_msg, "Load failed: %s",
                       error_details ? (char *)error_details
                                     : "Unknown error");
              UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
              if (error_details)
                FreeMem (error_details, strlen ((char *)error_details) + 1);
            }
          FreeVec (file_data);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

void
DoRunPRG (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];

  if (!data->connection)
    return;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             (CONST_STRPTR) "Select PRG file to run",
                             ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
                             (CONST_STRPTR) "#?.prg", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      UBYTE *file_data;
      ULONG file_size;
      STRPTR error_details = NULL;

      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      file_data = U64_ReadFile ((CONST_STRPTR)filename, &file_size);
      if (file_data)
        {
          if (U64_RunPRG (data->connection, file_data, file_size,
                          &error_details)
              == U64_OK)
            {
              UpdateStatus (data, (CONST_STRPTR) "PRG file running", TRUE);
            }
          else
            {
              char error_msg[512];
              sprintf (error_msg, "Run failed: %s",
                       error_details ? (char *)error_details
                                     : "Unknown error");
              UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
              if (error_details)
                FreeMem (error_details, strlen ((char *)error_details) + 1);
            }
          FreeVec (file_data);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

void
DoRunCRT (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];

  if (!data->connection)
    return;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             (CONST_STRPTR) "Select CRT file to run",
                             ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
                             (CONST_STRPTR) "#?.crt", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      UBYTE *file_data;
      ULONG file_size;
      STRPTR error_details = NULL;

      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      file_data = U64_ReadFile ((CONST_STRPTR)filename, &file_size);
      if (file_data)
        {
          if (U64_RunCRT (data->connection, file_data, file_size,
                          &error_details)
              == U64_OK)
            {
              UpdateStatus (data, (CONST_STRPTR) "CRT file started", TRUE);
            }
          else
            {
              char error_msg[512];
              sprintf (error_msg, "CRT run failed: %s",
                       error_details ? (char *)error_details
                                     : "Unknown error");
              UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
              if (error_details)
                FreeMem (error_details, strlen ((char *)error_details) + 1);
            }
          FreeVec (file_data);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read CRT file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

/* Type text - converts to uppercase and adds RETURN */
void
DoType (struct AppData *data)
{
  STRPTR text;
  STRPTR text_with_return;
  ULONG text_len;
  int i;

  if (!data->connection)
    return;

  get (data->str_type, MUIA_String_Contents, &text);

  if (text && strlen ((char *)text) > 0)
    {
      text_len = strlen ((char *)text);

      /* Allocate buffer for text + RETURN + null terminator */
      text_with_return = AllocMem (text_len + 2, MEMF_PUBLIC | MEMF_CLEAR);
      if (!text_with_return)
        {
          UpdateStatus (data, (CONST_STRPTR) "Out of memory", TRUE);
          return;
        }

      /* Copy text and convert to uppercase */
      for (i = 0; i < text_len; i++)
        {
          char c = text[i];
          /* Convert lowercase to uppercase, leave everything else as-is */
          if (c >= 'a' && c <= 'z')
            {
              text_with_return[i] = c - 'a' + 'A';
            }
          else
            {
              text_with_return[i] = c;
            }
        }

      /* Add RETURN character */
      text_with_return[text_len] = '\r';     /* Add carriage return */
      text_with_return[text_len + 1] = '\0'; /* Null terminate */

      /* Send to Ultimate64 */
      if (U64_TypeText (data->connection, (CONST_STRPTR)text_with_return)
          == U64_OK)
        {
          UpdateStatus (data, (CONST_STRPTR) "Text typed", TRUE);
          set (data->str_type, MUIA_String_Contents, (CONST_STRPTR) "");
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Type failed", TRUE);
        }

      FreeMem (text_with_return, text_len + 2);
    }
}

/* Memory operations */
void
DoPeek (struct AppData *data)
{
  STRPTR addr_str;
  UWORD address;
  UBYTE value;
  char result[128];

  if (!data->connection)
    return;

  get (data->str_peek_addr, MUIA_String_Contents, &addr_str);

  if (!addr_str || strlen (addr_str) == 0)
    {
      UpdateStatus (data, (CONST_STRPTR) "Enter address to peek", TRUE);
      return;
    }

  address = ParseAddress (addr_str);
  value = U64_Peek (data->connection, address);

  if (U64_GetLastError (data->connection) == U64_OK)
    {
      sprintf (result, "$%04X: $%02X (%d)", address, value, value);
      set (data->txt_memory_result, MUIA_Text_Contents, result);
      UpdateStatus (data, (CONST_STRPTR)result, TRUE);
    }
  else
    {
      UpdateStatus (data, (CONST_STRPTR) "Peek failed", TRUE);
    }
}

void
DoPoke (struct AppData *data)
{
  STRPTR addr_str, val_str;
  UWORD address;
  UBYTE value;
  char result[128];

  if (!data->connection)
    return;

  get (data->str_poke_addr, MUIA_String_Contents, &addr_str);
  get (data->str_poke_value, MUIA_String_Contents, &val_str);

  if (!addr_str || strlen (addr_str) == 0)
    {
      UpdateStatus (data, (CONST_STRPTR) "Enter address to poke", TRUE);
      return;
    }

  if (!val_str || strlen (val_str) == 0)
    {
      UpdateStatus (data, (CONST_STRPTR) "Enter value to poke", TRUE);
      return;
    }

  address = ParseAddress (addr_str);
  value = ParseValue (val_str);

  if (U64_Poke (data->connection, address, value) == U64_OK)
    {
      sprintf (result, "Poked $%02X to $%04X", value, address);
      set (data->txt_memory_result, MUIA_Text_Contents, result);
      UpdateStatus (data, (CONST_STRPTR)result, TRUE);

      /* Clear input fields */
      set (data->str_poke_addr, MUIA_String_Contents, (CONST_STRPTR) "");
      set (data->str_poke_value, MUIA_String_Contents, (CONST_STRPTR) "");
    }
  else
    {
      UpdateStatus (data, (CONST_STRPTR) "Poke failed", TRUE);
    }
}

/* Mount disk image */
void
DoMount (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];
  LONG drive_idx, mode_idx;
  STRPTR error_details = NULL;

  if (!data->connection)
    return;

  get (data->cyc_drive, MUIA_Cycle_Active, &drive_idx);
  get (data->cyc_mode, MUIA_Cycle_Active, &mode_idx);

  req = AllocAslRequestTags (
      ASL_FileRequest, ASLFR_TitleText, (CONST_STRPTR) "Select disk image",
      ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
      (CONST_STRPTR) "#?.(d64|g64|d71|g71|d81)", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      if (U64_MountDisk (data->connection, (CONST_STRPTR)filename,
                         (CONST_STRPTR)drive_ids[drive_idx],
                         mode_values[mode_idx], FALSE, &error_details)
          == U64_OK)
        {
          char msg[512];
          sprintf (msg, "Mounted %s to drive %s", req->rf_File,
                   drive_ids[drive_idx]);
          UpdateStatus (data, (CONST_STRPTR)msg, TRUE);
          /* Update disk display after successful mount */
          UpdateDiskDisplay (data);
        }
      else
        {
          char error_msg[512];
          sprintf (error_msg, "Mount failed: %s",
                   error_details ? (char *)error_details : "Unknown error");
          UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
          if (error_details)
            FreeMem (error_details, strlen ((char *)error_details) + 1);
        }
    }

  if (req)
    FreeAslRequest (req);
}

/* Unmount disk */
void
DoUnmount (struct AppData *data)
{
  LONG drive_idx;
  STRPTR error_details = NULL;
  char msg[128];

  if (!data->connection)
    return;

  get (data->cyc_drive, MUIA_Cycle_Active, &drive_idx);

  if (U64_UnmountDisk (data->connection, (CONST_STRPTR)drive_ids[drive_idx],
                       &error_details)
      == U64_OK)
    {
      sprintf (msg, "Drive %s unmounted", drive_ids[drive_idx]);
      UpdateStatus (data, (CONST_STRPTR)msg, TRUE);
      /* Update disk display after successful unmount */
      UpdateDiskDisplay (data);
    }
  else
    {
      char error_msg[512];
      sprintf (error_msg, "Unmount failed: %s",
               error_details ? (char *)error_details : "Unknown error");
      UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
      if (error_details)
        FreeMem (error_details, strlen ((char *)error_details) + 1);
    }
}

/* Show drives status */
void
DoDrivesStatus (struct AppData *data)
{
  if (!data->connection)
    return;

  /* Update the disk display and also show in output */
  UpdateDiskDisplay (data);

  /* Also add to output log */
  STRPTR disk_status;
  get (data->txt_disk_status, MUIA_Text_Contents, &disk_status);
  if (disk_status)
    {
      UpdateStatus (data, (CONST_STRPTR) "Drive status updated", TRUE);
    }
}

/* Music playback functions */
void
DoPlaySID (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];

  if (!data->connection)
    return;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             (CONST_STRPTR) "Select SID file to play",
                             ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
                             (CONST_STRPTR) "#?.sid", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      UBYTE *file_data;
      ULONG file_size;
      STRPTR song_str;
      UBYTE song_num = 0;
      STRPTR error_details = NULL;

      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      get (data->str_song_num, MUIA_String_Contents, &song_str);
      if (song_str && strlen (song_str) > 0)
        {
          song_num = (UBYTE)atoi (song_str);
        }

      file_data = U64_ReadFile ((CONST_STRPTR)filename, &file_size);
      if (file_data)
        {
          if (U64_PlaySID (data->connection, file_data, file_size, song_num,
                           &error_details)
              == U64_OK)
            {
              char msg[256];
              sprintf (msg, "Playing SID: %s (song %d)", req->rf_File,
                       song_num);
              UpdateStatus (data, (CONST_STRPTR)msg, TRUE);
            }
          else
            {
              char error_msg[512];
              sprintf (error_msg, "SID playback failed: %s",
                       error_details ? (char *)error_details
                                     : "Unknown error");
              UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
              if (error_details)
                FreeMem (error_details, strlen ((char *)error_details) + 1);
            }
          FreeVec (file_data);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read SID file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

void
DoPlayMOD (struct AppData *data)
{
  struct FileRequester *req;
  char filename[256];

  if (!data->connection)
    return;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             (CONST_STRPTR) "Select MOD file to play",
                             ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern,
                             (CONST_STRPTR) "#?.mod", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      UBYTE *file_data;
      ULONG file_size;
      STRPTR error_details = NULL;

      strcpy (filename, req->rf_Dir);
      AddPart ((STRPTR)filename, req->rf_File, sizeof (filename));

      file_data = U64_ReadFile ((CONST_STRPTR)filename, &file_size);
      if (file_data)
        {
          if (U64_PlayMOD (data->connection, file_data, file_size,
                           &error_details)
              == U64_OK)
            {
              char msg[256];
              sprintf (msg, "Playing MOD: %s", req->rf_File);
              UpdateStatus (data, (CONST_STRPTR)msg, TRUE);
            }
          else
            {
              char error_msg[512];
              sprintf (error_msg, "MOD playback failed: %s",
                       error_details ? (char *)error_details
                                     : "Unknown error");
              UpdateStatus (data, (CONST_STRPTR)error_msg, TRUE);
              if (error_details)
                FreeMem (error_details, strlen ((char *)error_details) + 1);
            }
          FreeVec (file_data);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read MOD file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}
