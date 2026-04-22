/* Ultimate64 Control - Command Line Interface
 * Command dispatcher and handlers.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include "cli.h"

#include "file_utils.h"
#include "string_utils.h"

#include <proto/dos.h>
#include <proto/exec.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Execute command */
int
ExecuteCommand (U64Connection *conn, U64CommandType cmd, LONG *args,
                char *env_host, char *env_password, UWORD env_port,
                char *host_arg, char *password_arg)
{
  LONG result;
  char *file;
  char *text;
  char *drive;
  char *address_str;
  LONG *song;
  UWORD address;

  PrintVerbose ("Executing command type: %d", cmd);

  /* Get arguments */
  file = (char *)args[ARG_FILE];
  text = (char *)args[ARG_TEXT];
  drive = (char *)args[ARG_DRIVE];
  address_str = (char *)args[ARG_ADDRESS];
  song = (LONG *)args[ARG_SONG];

  switch (cmd)
    {
    case U64CMD_CONFIG:
      PrintInfo ("Current u64ctl Configuration:");
      PrintInfo ("  Host:     %s", env_host ? env_host : "not set");
      PrintInfo ("  Port:     %d", env_port);
      PrintInfo ("  Password: %s", env_password ? "***" : "not set");
      PrintInfo ("");
      PrintInfo ("ENV: variables:");
      PrintInfo ("  ENV:%s", ENV_ULTIMATE64_HOST);
      PrintInfo ("  ENV:%s", ENV_ULTIMATE64_PASSWORD);
      PrintInfo ("  ENV:%s", ENV_ULTIMATE64_PORT);
      return 0;

    case U64CMD_SETHOST:
      if (!host_arg)
        {
          PrintError ("HOST argument required for sethost command");
          return 5;
        }
      /* Preserve the current port when setting host */
      if (SaveSettings (host_arg, env_password, env_port ? env_port : 80))
        {
          PrintInfo ("Host set to: %s", host_arg);
        }
      else
        {
          PrintError ("Failed to save host setting");
          return 10;
        }
      return 0;

    case U64CMD_SETPASSWORD:
      if (!password_arg)
        {
          PrintError ("PASSWORD argument required for setpassword command");
          return 5;
        }
      if (SaveSettings (env_host, password_arg, env_port))
        {
          PrintInfo ("Password updated");
        }
      else
        {
          PrintError ("Failed to save password");
          return 10;
        }
      return 0;

    case U64CMD_SETPORT:
      {
        char *port_str = (char *)args[ARG_TEXT];
        if (!port_str)
          {
            PrintError (
                "TEXT argument required for setport command (port number)");
            return 5;
          }
        ULONG new_port = (ULONG)atoi (port_str);
        if (new_port == 0 || new_port > 65535)
          {
            PrintError ("Invalid port number: %s", port_str);
            return 5;
          }
        if (SaveSettings (env_host, env_password, (UWORD)new_port))
          {
            PrintInfo ("Port set to: %ld", new_port);
          }
        else
          {
            PrintError ("Failed to save port setting");
            return 10;
          }
        return 0;
      }

    case U64CMD_CLEARCONFIG:
      PrintInfo ("Clearing u64ctl configuration...");
      DeleteVar (ENV_ULTIMATE64_HOST, GVF_GLOBAL_ONLY);
      DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
      DeleteVar (ENV_ULTIMATE64_PORT, GVF_GLOBAL_ONLY);
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_HOST);
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_PORT);
      PrintInfo ("Configuration cleared");
      return 0;

    case U64CMD_INFO:
      PrintVerbose ("Executing INFO command");
      {
        U64DeviceInfo info;
        result = U64_GetDeviceInfo (conn, &info);
        if (result == U64_OK)
          {
            PrintInfo ("Device: %s", info.product_name
                                         ? (char *)info.product_name
                                         : "Unknown");
            PrintInfo ("Firmware: %s", info.firmware_version
                                           ? (char *)info.firmware_version
                                           : "Unknown");
            PrintInfo ("FPGA: %s", info.fpga_version
                                       ? (char *)info.fpga_version
                                       : "Unknown");
            if (info.core_version)
              {
                PrintInfo ("Core: %s", (char *)info.core_version);
              }
            PrintInfo ("Hostname: %s",
                       info.hostname ? (char *)info.hostname : "Unknown");
            if (info.unique_id)
              {
                PrintInfo ("ID: %s", (char *)info.unique_id);
              }
            U64_FreeDeviceInfo (&info);
          }
        else
          {
            PrintError ("Failed to get device info: %s",
                        U64_GetErrorString (result));
            return 10;
          }
        break;
      }

    case U64CMD_RESET:
      PrintVerbose ("Executing RESET command");
      PrintVerbose ("Resetting C64...");
      result = U64_Reset (conn);
      if (result != U64_OK)
        {
          PrintError ("Reset failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("C64 reset");
      break;

    case U64CMD_REBOOT:
      PrintVerbose ("Executing REBOOT command");
      PrintVerbose ("Rebooting Ultimate device...");
      result = U64_Reboot (conn);
      if (result != U64_OK)
        {
          PrintError ("Reboot failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("Device rebooted");
      break;

    case U64CMD_POWEROFF:
      PrintVerbose ("Executing POWEROFF command");
      PrintVerbose ("Powering off...");
      result = U64_PowerOff (conn);
      if (result != U64_OK)
        {
          PrintError ("Power off failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("Powered off");
      break;

    case U64CMD_PAUSE:
      PrintVerbose ("Executing PAUSE command");
      result = U64_Pause (conn);
      if (result != U64_OK)
        {
          PrintError ("Pause failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("C64 paused");
      break;

    case U64CMD_RESUME:
      PrintVerbose ("Executing RESUME command");
      result = U64_Resume (conn);
      if (result != U64_OK)
        {
          PrintError ("Resume failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("C64 resumed");
      break;

    case U64CMD_MENU:
      PrintVerbose ("Executing MENU command");
      result = U64_MenuButton (conn);
      if (result != U64_OK)
        {
          PrintError ("Menu button failed: %s", U64_GetErrorString (result));
          return 10;
        }
      PrintInfo ("Menu button pressed");
      break;

    case U64CMD_LOAD:
      PrintVerbose ("Executing LOAD command");
      if (!file)
        {
          PrintError ("FILE argument required for load command");
          return 5;
        }
      {
        UBYTE *data;
        ULONG size;
        STRPTR error_details = NULL;
        STRPTR validation_info = NULL;
        CONST_STRPTR ext;

        PrintVerbose ("Loading file: %s", file);
        data = U64_ReadFile (file, &size);
        if (!data)
          {
            PrintError ("Failed to load file: %s", file);
            return 10;
          }

        PrintVerbose ("File loaded successfully: %lu bytes",
                      (unsigned long)size);

        /* Determine file type by extension */
        ext = strrchr (file, '.');
        if (ext && stricmp (ext, ".crt") == 0)
          {
            PrintError ("CRT files should be run, not loaded. Use 'run' "
                        "command instead.");
            FreeVec (data);
            return 5;
          }
        else
          {
            /* Assume PRG file */
            if (U64_ValidatePRGFile (data, size, &validation_info) == U64_OK)
              {
                if (validation_info)
                  {
                    PrintVerbose ("PRG validation: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
              }
            else
              {
                if (validation_info)
                  {
                    PrintError ("PRG validation failed: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
                else
                  {
                    PrintError ("PRG validation failed");
                  }
                FreeVec (data);
                return 10;
              }

            PrintVerbose ("Loading PRG file ...");
            result = U64_LoadPRG (conn, data, size, &error_details);
          }

        /* Free file data immediately after use */
        FreeVec (data);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("Load failed with u64ctl errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("Load failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError ("The load endpoint was not found. Check "
                                "u64ctl firmware version.");
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid file format or corrupted data.");
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError ("Request timed out. u64ctl may be busy.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        PrintInfo ("File loaded successfully: %s", file);
      }
      break;

    case U64CMD_RUN:
      PrintVerbose ("Executing RUN command");
      if (!file)
        {
          PrintError ("FILE argument required for run command");
          return 5;
        }
      {
        UBYTE *data;
        ULONG size;
        STRPTR error_details = NULL;
        STRPTR validation_info = NULL;
        CONST_STRPTR ext;

        PrintVerbose ("Loading file for running: %s", file);
        data = U64_ReadFile (file, &size);
        if (!data)
          {
            PrintError ("Failed to load file: %s", file);
            return 10;
          }

        PrintVerbose ("File loaded successfully: %lu bytes",
                      (unsigned long)size);

        /* Determine file type by extension */
        ext = strrchr (file, '.');
        if (ext && stricmp (ext, ".crt") == 0)
          {
            /* CRT file */
            if (U64_ValidateCRTFile (data, size, &validation_info) == U64_OK)
              {
                if (validation_info)
                  {
                    PrintVerbose ("CRT validation: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
              }
            else
              {
                if (validation_info)
                  {
                    PrintError ("CRT validation failed: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
                else
                  {
                    PrintError ("CRT validation failed");
                  }
                FreeVec (data);
                return 10;
              }

            PrintVerbose ("Running CRT file...");
            result = U64_RunCRT (conn, data, size, &error_details);
          }
        else
          {
            /* Assume PRG file */
            if (U64_ValidatePRGFile (data, size, &validation_info) == U64_OK)
              {
                if (validation_info)
                  {
                    PrintVerbose ("PRG validation: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
              }
            else
              {
                if (validation_info)
                  {
                    PrintError ("PRG validation failed: %s", validation_info);
                    FreeMem (validation_info, strlen (validation_info) + 1);
                  }
                else
                  {
                    PrintError ("PRG validation failed");
                  }
                FreeVec (data);
                return 10;
              }

            PrintVerbose ("Running PRG file with enhanced error reporting...");
            result = U64_RunPRG (conn, data, size, &error_details);
          }

        /* Free file data immediately after use */
        FreeVec (data);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("Run failed with u64ctl errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("Run failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError ("The run endpoint was not found. Check "
                                "Ultimate64 firmware version.");
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid file format or corrupted data.");
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError ("Request timed out. Ultimate64 may be busy.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        if (ext && stricmp (ext, ".crt") == 0)
          {
            PrintInfo ("CRT file started successfully: %s", file);
          }
        else
          {
            PrintInfo ("PRG file running: %s", file);
          }
      }
      break;

    case U64CMD_TYPE:
      PrintVerbose ("Executing TYPE command");
      if (!text)
        {
          PrintError ("TEXT argument required for type command");
          return 5;
        }

      /* Convert escape sequences in the text */
      {
        STRPTR converted_text = U64_ConvertEscapeSequences (text);
        if (!converted_text)
          {
            PrintError ("Failed to convert escape sequences");
            return 10;
          }

        PrintVerbose ("Original text: '%s'", text);
        PrintVerbose ("Converted text: '%s'", converted_text);
        PrintVerbose ("Typing converted text...");

        result = U64_TypeText (conn, (CONST_STRPTR)converted_text);

        /* Free the converted text */
        FreeVec (converted_text);

        if (result != U64_OK)
          {
            PrintError ("Type failed: %s", U64_GetErrorString (result));
            return 10;
          }
        PrintInfo ("Text typed successfully");
      }
      break;

    case U64CMD_PEEK:
      PrintVerbose ("Executing PEEK command");
      if (!address_str)
        {
          PrintError ("ADDRESS argument required for peek command");
          return 5;
        }

      /* Parse address string - handle hex and decimal */
      if (strncmp (address_str, "0x", 2) == 0
          || strncmp (address_str, "0X", 2) == 0)
        {
          address = (UWORD)strtol (address_str, NULL, 16);
        }
      else if (address_str[0] == '$')
        {
          address = (UWORD)strtol (address_str + 1, NULL, 16);
        }
      else
        {
          address = (UWORD)strtol (address_str, NULL, 10);
        }

      PrintVerbose ("Parsed address: $%04X (%s)", address, address_str);

      {
        UBYTE value = U64_Peek (conn, address);
        LONG last_error = U64_GetLastError (conn);
        if (last_error != U64_OK)
          {
            PrintError ("Peek failed: %s", U64_GetErrorString (last_error));
            return 10;
          }
        PrintInfo ("$%04X: $%02X (%d)", address, value, value);
      }
      break;

    case U64CMD_POKE:
      PrintVerbose ("Executing POKE command");
      if (!address_str)
        {
          PrintError ("ADDRESS argument required for poke command");
          return 5;
        }
      if (!text)
        {
          PrintError ("Value required for poke (use TEXT argument)");
          return 5;
        }

      /* Parse address string - handle hex and decimal */
      if (strncmp (address_str, "0x", 2) == 0
          || strncmp (address_str, "0X", 2) == 0)
        {
          address = (UWORD)strtol (address_str, NULL, 16);
        }
      else if (address_str[0] == '$')
        {
          address = (UWORD)strtol (address_str + 1, NULL, 16);
        }
      else
        {
          address = (UWORD)strtol (address_str, NULL, 10);
        }

      PrintVerbose ("Parsed address: $%04X (%s)", address, address_str);

      {
        UBYTE value;
        /* Handle both decimal and hex input for value */
        if (strncmp (text, "0x", 2) == 0 || strncmp (text, "0X", 2) == 0)
          {
            value = (UBYTE)strtol (text, NULL, 16);
          }
        else if (text[0] == '$')
          {
            value = (UBYTE)strtol (text + 1, NULL, 16);
          }
        else
          {
            value = (UBYTE)strtol (text, NULL, 10);
          }

        PrintVerbose ("Parsed value: $%02X (%d) from '%s'", value, value,
                      text);

        result = U64_Poke (conn, address, value);
        if (result != U64_OK)
          {
            PrintError ("Poke failed: %s", U64_GetErrorString (result));
            return 10;
          }
        PrintInfo ("Poked $%02X to $%04X", value, address);
      }
      break;

    case U64CMD_PLAYSID:
      PrintVerbose ("Executing PLAYSID command");
      if (!file)
        {
          PrintError ("FILE argument required for playsid command");
          return 5;
        }
      {
        UBYTE *data;
        ULONG size;
        UBYTE song_num = song ? (UBYTE)*song : 0;
        STRPTR error_details = NULL;
        STRPTR validation_info = NULL;

        PrintVerbose ("Loading SID file: %s", file);
        data = U64_ReadFile (file, &size);
        if (!data)
          {
            PrintError ("Failed to load file: %s", file);
            return 10;
          }

        PrintVerbose ("File loaded successfully: %lu bytes",
                      (unsigned long)size);

        /* Validate SID file format */
        if (U64_ValidateSIDFile (data, size, &validation_info) == U64_OK)
          {
            if (validation_info)
              {
                PrintVerbose ("SID validation: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
          }
        else
          {
            if (validation_info)
              {
                PrintError ("SID validation failed: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
            else
              {
                PrintError ("SID validation failed");
              }
            FreeVec (data);
            return 10;
          }

        PrintVerbose ("Playing SID file: %s (song %d)", file, song_num);
        PrintVerbose ("Using enhanced PlaySID with error reporting...");

        /* Use enhanced PlaySID function with error details */
        result = U64_PlaySID (conn, data, size, song_num, &error_details);

        /* Free file data immediately after use */
        FreeVec (data);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("PlaySID failed with Ultimate64 errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("PlaySID failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                /* Provide more specific error context */
                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError ("The SID player endpoint was not found. Check "
                                "Ultimate64 firmware version.");
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid SID file format or corrupted data.");
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError ("Request timed out. Ultimate64 may be busy.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        PrintInfo ("SID file started successfully: %s", file);
        if (song_num > 0)
          {
            PrintInfo ("Playing song number: %d", song_num);
          }
      }
      break;

    case U64CMD_MOUNT:
      PrintVerbose ("Executing enhanced MOUNT command");
      if (!file)
        {
          PrintError ("FILE argument required for mount command");
          return 5;
        }
      if (!drive)
        {
          PrintError (
              "DRIVE argument required for mount command (a, b, c, or d)");
          return 5;
        }
      {
        U64MountMode mount_mode = U64_MOUNT_RO; /* Default to read-only */
        char *mode_str = (char *)args[ARG_MODE];
        STRPTR error_details = NULL;
        STRPTR validation_info = NULL;
        UBYTE *disk_data;
        ULONG disk_size;
        U64DiskImageType disk_type;

        /* Parse mount mode */
        if (mode_str)
          {
            mount_mode = ParseMountMode (mode_str);
            PrintVerbose ("Mount mode: %s", mode_str);
          }

        /* Validate drive ID */
        if (strlen (drive) != 1 || (drive[0] < 'a' || drive[0] > 'd'))
          {
            PrintError ("Invalid drive '%s'. Must be a, b, c, or d", drive);
            return 5;
          }

        PrintVerbose ("Mounting %s to drive %s (mode: %s)", file, drive,
                      mount_mode == U64_MOUNT_RW   ? "read/write"
                      : mount_mode == U64_MOUNT_RO ? "read-only"
                                                   : "unlinked");

        /* Pre-validate the disk image file */
        disk_data = U64_ReadFile (file, &disk_size);
        if (!disk_data)
          {
            PrintError ("Failed to load disk image: %s", file);
            return 10;
          }

        disk_type = U64_GetDiskTypeFromExt (file);
        PrintVerbose ("Detected disk type: %s",
                      U64_GetDiskTypeString (disk_type));

        /* Validate disk image format */
        if (U64_ValidateDiskImage (disk_data, disk_size, disk_type,
                                   &validation_info)
            == U64_OK)
          {
            if (validation_info)
              {
                PrintVerbose ("Disk validation: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
          }
        else
          {
            if (validation_info)
              {
                PrintError ("Disk validation failed: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
            else
              {
                PrintError ("Disk validation failed");
              }
            FreeVec (disk_data);
            return 10;
          }

        /* Free the disk data - the mount function will reload it */
        FreeVec (disk_data);

        /* Check if drive is already mounted */
        BOOL is_mounted;
        STRPTR current_image = NULL;
        U64MountMode current_mode;

        if (U64_GetDriveStatus (conn, drive, &is_mounted, &current_image,
                                &current_mode)
            == U64_OK)
          {
            if (is_mounted)
              {
                PrintInfo ("Drive %s is currently mounted with: %s", drive,
                           current_image ? (char *)current_image
                                         : "unknown image");
                PrintVerbose ("Current mount mode: %s",
                              current_mode == U64_MOUNT_RW   ? "read/write"
                              : current_mode == U64_MOUNT_RO ? "read-only"
                                                             : "unlinked");

                /* Ask user if they want to unmount first */
                PrintInfo ("Unmounting current image...");
                STRPTR unmount_errors = NULL;
                if (U64_UnmountDisk (conn, drive, &unmount_errors) != U64_OK)
                  {
                    if (unmount_errors)
                      {
                        PrintError ("Failed to unmount current disk: %s",
                                    unmount_errors);
                        FreeMem (unmount_errors, strlen (unmount_errors) + 1);
                      }
                    else
                      {
                        PrintError ("Failed to unmount current disk");
                      }
                    if (current_image)
                      FreeMem (current_image, strlen (current_image) + 1);
                    return 10;
                  }
                PrintVerbose ("Previous image unmounted successfully");
              }
            if (current_image)
              {
                FreeMem (current_image, strlen (current_image) + 1);
              }
          }

        /* Perform the mount with enhanced error reporting */
        result = U64_MountDisk (conn, file, drive, mount_mode, FALSE,
                                &error_details);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("Mount failed with Ultimate64 errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("Mount failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError (
                        "Drive %s not found or mount endpoint not available",
                        drive);
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid disk image or drive parameters");
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError (
                        "Mount operation timed out. Ultimate64 may be busy.");
                    break;
                  case U64_ERR_ACCESS:
                    PrintError (
                        "Access denied. Check password or drive permissions.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred during mount.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        PrintInfo ("Disk image mounted successfully:");
        PrintInfo ("  File: %s", file);
        PrintInfo ("  Drive: %s", drive);
        PrintInfo ("  Mode: %s", mount_mode == U64_MOUNT_RW   ? "read/write"
                                 : mount_mode == U64_MOUNT_RO ? "read-only"
                                                              : "unlinked");

        /* Verify the mount was successful */
        if (U64_GetDriveStatus (conn, drive, &is_mounted, &current_image,
                                &current_mode)
            == U64_OK)
          {
            if (is_mounted)
              {
                PrintVerbose ("Mount verified: %s", current_image
                                                        ? (char *)current_image
                                                        : "mounted");
                if (current_image)
                  {
                    FreeMem (current_image, strlen (current_image) + 1);
                  }
              }
            else
              {
                PrintError ("WARNING: Mount reported success but drive "
                            "appears unmounted");
              }
          }
      }
      break;

    case U64CMD_UNMOUNT:
      PrintVerbose ("Executing enhanced UNMOUNT command");
      if (!drive)
        {
          PrintError (
              "DRIVE argument required for unmount command (a, b, c, or d)");
          return 5;
        }
      {
        STRPTR error_details = NULL;
        BOOL is_mounted;
        STRPTR current_image = NULL;
        U64MountMode current_mode;

        /* Validate drive ID */
        if (strlen (drive) != 1 || (drive[0] < 'a' || drive[0] > 'd'))
          {
            PrintError ("Invalid drive '%s'. Must be a, b, c, or d", drive);
            return 5;
          }

        PrintVerbose ("Unmounting drive %s", drive);

        /* Check current drive status */
        if (U64_GetDriveStatus (conn, drive, &is_mounted, &current_image,
                                &current_mode)
            == U64_OK)
          {
            if (!is_mounted)
              {
                PrintInfo ("Drive %s is not currently mounted", drive);
                if (current_image)
                  {
                    FreeMem (current_image, strlen (current_image) + 1);
                  }
                return 0; /* Not an error */
              }
            else
              {
                PrintInfo ("Unmounting: %s from drive %s",
                           current_image ? (char *)current_image
                                         : "mounted image",
                           drive);
                PrintVerbose ("Current mount mode: %s",
                              current_mode == U64_MOUNT_RW   ? "read/write"
                              : current_mode == U64_MOUNT_RO ? "read-only"
                                                             : "unlinked");
              }
            if (current_image)
              {
                FreeMem (current_image, strlen (current_image) + 1);
              }
          }
        else
          {
            PrintVerbose (
                "Could not check drive status, proceeding with unmount");
          }

        /* Perform the unmount with enhanced error reporting */
        result = U64_UnmountDisk (conn, drive, &error_details);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("Unmount failed with Ultimate64 errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("Unmount failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError (
                        "Drive %s not found or unmount endpoint not available",
                        drive);
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid unmount request for drive %s", drive);
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError ("Unmount operation timed out. Ultimate64 may "
                                "be busy.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred during unmount.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        PrintInfo ("Drive %s unmounted successfully", drive);

        /* Verify the unmount was successful */
        if (U64_GetDriveStatus (conn, drive, &is_mounted, &current_image,
                                &current_mode)
            == U64_OK)
          {
            if (!is_mounted)
              {
                PrintVerbose ("Unmount verified: drive %s is now empty",
                              drive);
              }
            else
              {
                PrintError ("WARNING: Unmount reported success but drive "
                            "still appears mounted");
                if (current_image)
                  {
                    PrintError ("Still shows: %s", current_image);
                  }
              }
            if (current_image)
              {
                FreeMem (current_image, strlen (current_image) + 1);
              }
          }
      }
      break;

    /* Also add a drives status command */
    case U64CMD_DRIVES:
      PrintVerbose ("Executing DRIVES status command");
      {
        /* Ultimate64 exposes two real internal drives: A (bus 8), B (bus 9). */
        const char *drive_letters[] = { "a", "b" };
        int i;

        PrintInfo ("Drive Status:");
        PrintInfo ("=============");

        for (i = 0; i < 2; i++)
          {
            BOOL is_mounted;
            STRPTR image_name = NULL;
            U64MountMode mode;

            if (U64_GetDriveStatus (conn, drive_letters[i], &is_mounted,
                                    &image_name, &mode)
                == U64_OK)
              {
                if (is_mounted)
                  {
                    PrintInfo ("Drive %s: %s (%s)", drive_letters[i],
                               image_name ? (char *)image_name : "mounted",
                               mode == U64_MOUNT_RW   ? "read/write"
                               : mode == U64_MOUNT_RO ? "read-only"
                                                      : "unlinked");
                    if (image_name)
                      {
                        FreeMem (image_name, strlen (image_name) + 1);
                      }
                  }
                else
                  {
                    PrintInfo ("Drive %s: empty", drive_letters[i]);
                  }
              }
            else
              {
                PrintInfo ("Drive %s: status unknown", drive_letters[i]);
              }
          }
      }
      break;
    case U64CMD_LISTCONFIG:
      PrintVerbose("Executing LISTCONFIG command");
      {
        STRPTR *categories;
        ULONG cat_count;
        ULONG i;

        PrintInfo("Getting configuration categories...");
        result = U64_GetConfigCategories(conn, &categories, &cat_count);

        if (result != U64_OK)
        {
          PrintError("Failed to get configuration categories: %s",
                     U64_GetErrorString(result));
          return 10;
        }

        PrintInfo("Ultimate64 Configuration Categories (%lu found):",
                  (unsigned long)cat_count);
        PrintInfo("====================================================");

        for (i = 0; i < cat_count; i++)
        {
          PrintInfo("  %2lu. %s", (unsigned long)(i + 1), categories[i]);
        }

        PrintInfo("");
        PrintInfo("Use 'showconfig' with category name to see items:");
        PrintInfo("  u64ctl showconfig TEXT \"Drive A Settings\"");

        U64_FreeConfigCategories(categories, cat_count);
      }
      break;

    case U64CMD_SHOWCONFIG:
      PrintVerbose("Executing SHOWCONFIG command");
      if (!text)
      {
        PrintError("Category name required (use TEXT argument)");
        PrintInfo("Example: u64ctl showconfig TEXT \"Drive A Settings\"");
        return 5;
      }
      {
        U64ConfigItem *items;
        ULONG item_count;
        ULONG i;

        PrintInfo("Getting configuration items for category: %s", text);
        result = U64_GetConfigCategory(conn, text, &items, &item_count);

        if (result != U64_OK)
        {
          PrintError("Failed to get configuration category '%s': %s",
                     text, U64_GetErrorString(result));
          PrintInfo("Use 'listconfig' to see available categories");
          return 10;
        }

        PrintInfo("Configuration Category: %s", text);
        PrintInfo("=========================================");
        PrintInfo("Items (%lu found):", (unsigned long)item_count);
        PrintInfo("");

        for (i = 0; i < item_count; i++)
        {
          const U64ConfigItem *item = &items[i];

          printf("  %-30s = ", item->name);

          if (item->value.is_numeric)
          {
            printf("%ld", item->value.current_int);
          }
          else
          {
            printf("\"%s\"", item->value.current_str ? item->value.current_str : "NULL");
          }

          printf("\n");
        }

        PrintInfo("");
        PrintInfo("Use 'getconfig' for detailed item information:");
        PrintInfo("  u64ctl getconfig TEXT \"%s/item_name\"", text);

        U64_FreeConfigItems(items, item_count);
      }
      break;

    case U64CMD_GETCONFIG:
      PrintVerbose("Executing GETCONFIG command");
      if (!text)
      {
        PrintError("Configuration path required (use TEXT argument)");
        PrintInfo("Format: \"Category Name/Item Name\"");
        PrintInfo("Example: u64ctl getconfig TEXT \"Drive A Settings/Drive Type\"");
        return 5;
      }
      {
        char *category_copy, *category, *item;
        U64ConfigItem config_item;

        /* Parse category/item path */
        category_copy = AllocMem(strlen(text) + 1, MEMF_PUBLIC);
        if (!category_copy)
        {
          PrintError("Out of memory");
          return 10;
        }
        strcpy(category_copy, text);

        /* Find the separator */
        item = strrchr(category_copy, '/');
        if (!item)
        {
          PrintError("Invalid format. Use: \"Category Name/Item Name\"");
          PrintInfo("Example: \"Drive A Settings/Drive Type\"");
          FreeMem(category_copy, strlen(text) + 1);
          return 5;
        }

        *item = '\0';  /* Terminate category string */
        item++;        /* Point to item name */
        category = category_copy;

        PrintVerbose("Category: '%s', Item: '%s'", category, item);

        result = U64_GetConfigItem(conn, category, item, &config_item);

        if (result != U64_OK)
        {
          PrintError("Failed to get configuration item '%s/%s': %s",
                     category, item, U64_GetErrorString(result));
          FreeMem(category_copy, strlen(text) + 1);
          return 10;
        }

        PrintInfo("Configuration Item Details:");
        PrintInfo("===========================");
        PrintInfo("Category: %s", category);
        PrintInfo("Item:     %s", item);
        PrintInfo("");

        if (config_item.value.is_numeric)
        {
          PrintInfo("Current Value: %ld", config_item.value.current_int);

          if (config_item.value.min_value != 0 || config_item.value.max_value != 0)
          {
            PrintInfo("Valid Range:   %ld - %ld",
                      config_item.value.min_value, config_item.value.max_value);
          }

          if (config_item.value.format)
          {
            PrintInfo("Format:        %s", config_item.value.format);
          }

          if (config_item.value.default_int != config_item.value.current_int)
          {
            PrintInfo("Default Value: %ld", config_item.value.default_int);
          }
        }
        else
        {
          PrintInfo("Current Value: \"%s\"",
                    config_item.value.current_str ? config_item.value.current_str : "NULL");

          if (config_item.value.default_str)
          {
            if (!config_item.value.current_str ||
                strcmp(config_item.value.default_str, config_item.value.current_str) != 0)
            {
              PrintInfo("Default Value: \"%s\"", config_item.value.default_str);
            }
          }
        }

        PrintInfo("");
        PrintInfo("To change this value, use:");
        PrintInfo("  u64ctl setconfig TEXT \"%s/%s\" ADDRESS \"new_value\"",
                  category, item);

        U64_FreeConfigItem(&config_item);
        FreeMem(category_copy, strlen(text) + 1);
      }
      break;

    case U64CMD_SETCONFIG:
      PrintVerbose("Executing SETCONFIG command");
      if (!text || !address_str)
      {
        PrintError("Configuration path and value required");
        PrintInfo("Format: TEXT=\"Category/Item\" ADDRESS=\"value\"");
        PrintInfo("Example: u64ctl setconfig TEXT \"Drive A Settings/Drive Type\" ADDRESS \"1571\"");
        return 5;
      }
      {
        char *category_copy, *category, *item;

        /* Parse category/item path */
        category_copy = AllocMem(strlen(text) + 1, MEMF_PUBLIC);
        if (!category_copy)
        {
          PrintError("Out of memory");
          return 10;
        }
        strcpy(category_copy, text);

        /* Find the separator */
        item = strrchr(category_copy, '/');
        if (!item)
        {
          PrintError("Invalid format. Use: \"Category Name/Item Name\"");
          PrintInfo("Example: \"Drive A Settings/Drive Type\"");
          FreeMem(category_copy, strlen(text) + 1);
          return 5;
        }

        *item = '\0';  /* Terminate category string */
        item++;        /* Point to item name */
        category = category_copy;

        PrintVerbose("Setting %s/%s = %s", category, item, address_str);
        PrintInfo("Setting configuration: %s/%s = \"%s\"", category, item, address_str);

        result = U64_SetConfigItem(conn, category, item, address_str);

        if (result != U64_OK)
        {
          PrintError("Failed to set configuration item: %s",
                     U64_GetErrorString(result));

          switch (result)
          {
            case U64_ERR_NOTFOUND:
              PrintError("Configuration item not found. Check category/item name.");
              break;
            case U64_ERR_INVALID:
              PrintError("Invalid value. Check valid range or format.");
              break;
            case U64_ERR_ACCESS:
              PrintError("Access denied. Check password.");
              break;
            case U64_ERR_NETWORK:
              PrintError("Network error. Check connection.");
              break;
            default:
              PrintError("Unexpected error occurred.");
              break;
          }

          FreeMem(category_copy, strlen(text) + 1);
          return 10;
        }

        PrintInfo("Configuration updated successfully");
        PrintInfo("");
        PrintInfo("Note: Changes are temporary until saved to flash.");
        PrintInfo("Use 'saveconfig' to make changes permanent:");
        PrintInfo("  u64ctl saveconfig");

        FreeMem(category_copy, strlen(text) + 1);
      }
      break;

    case U64CMD_SAVECONFIG:
      PrintVerbose("Executing SAVECONFIG command");
      PrintInfo("Saving current configuration to flash memory...");

      result = U64_SaveConfigToFlash(conn);

      if (result != U64_OK)
      {
        PrintError("Failed to save configuration: %s", U64_GetErrorString(result));

        switch (result)
        {
          case U64_ERR_ACCESS:
            PrintError("Access denied. Check password or permissions.");
            break;
          case U64_ERR_NETWORK:
            PrintError("Network error. Check connection.");
            break;
          case U64_ERR_TIMEOUT:
            PrintError("Operation timed out. Ultimate64 may be busy.");
            break;
          default:
            PrintError("Unexpected error occurred.");
            break;
        }
        return 10;
      }

      PrintInfo("Configuration saved to flash memory successfully");
      PrintInfo("Settings will persist after reboot");
      break;

    case U64CMD_LOADCONFIG:
      PrintVerbose("Executing LOADCONFIG command");
      PrintInfo("Loading configuration from flash memory...");

      result = U64_LoadConfigFromFlash(conn);

      if (result != U64_OK)
      {
        PrintError("Failed to load configuration: %s", U64_GetErrorString(result));

        switch (result)
        {
          case U64_ERR_ACCESS:
            PrintError("Access denied. Check password or permissions.");
            break;
          case U64_ERR_NETWORK:
            PrintError("Network error. Check connection.");
            break;
          case U64_ERR_TIMEOUT:
            PrintError("Operation timed out. Ultimate64 may be busy.");
            break;
          default:
            PrintError("Unexpected error occurred.");
            break;
        }
        return 10;
      }

      PrintInfo("Configuration loaded from flash memory successfully");
      PrintInfo("All settings restored to saved values");
      break;

    case U64CMD_RESETCONFIG:
      PrintVerbose("Executing RESETCONFIG command");
      PrintInfo("Resetting configuration to factory defaults...");
      PrintInfo("WARNING: This will reset ALL settings to default values!");

      result = U64_ResetConfigToDefault(conn);

      if (result != U64_OK)
      {
        PrintError("Failed to reset configuration: %s", U64_GetErrorString(result));

        switch (result)
        {
          case U64_ERR_ACCESS:
            PrintError("Access denied. Check password or permissions.");
            break;
          case U64_ERR_NETWORK:
            PrintError("Network error. Check connection.");
            break;
          case U64_ERR_TIMEOUT:
            PrintError("Operation timed out. Ultimate64 may be busy.");
            break;
          default:
            PrintError("Unexpected error occurred.");
            break;
        }
        return 10;
      }

      PrintInfo("Configuration reset to factory defaults successfully");
      PrintInfo("Note: Changes are temporary until saved to flash.");
      PrintInfo("Use 'saveconfig' to make the reset permanent:");
      PrintInfo("  u64ctl saveconfig");
      break;
    case U64CMD_PLAYMOD:
      PrintVerbose ("Executing PLAYMOD command");
      if (!file)
        {
          PrintError ("FILE argument required for playmod command");
          return 5;
        }
      {
        UBYTE *data;
        ULONG size;
        STRPTR error_details = NULL;
        STRPTR validation_info = NULL;

        PrintVerbose ("Loading MOD file: %s", file);
        data = U64_ReadFile (file, &size);
        if (!data)
          {
            PrintError ("Failed to load file: %s", file);
            return 10;
          }

        PrintVerbose ("File loaded successfully: %lu bytes",
                      (unsigned long)size);

        /* Validate MOD file format */
        if (U64_ValidateMODFile (data, size, &validation_info) == U64_OK)
          {
            if (validation_info)
              {
                PrintVerbose ("MOD validation: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
          }
        else
          {
            if (validation_info)
              {
                PrintError ("MOD validation failed: %s", validation_info);
                FreeMem (validation_info, strlen (validation_info) + 1);
              }
            else
              {
                PrintError ("MOD validation failed");
              }
            FreeVec (data);
            return 10;
          }

        PrintVerbose ("Playing MOD file: %s", file);
        PrintVerbose ("Using enhanced PlayMOD with error reporting...");

        /* Use enhanced PlayMOD function with error details */
        result = U64_PlayMOD (conn, data, size, &error_details);

        /* Free file data immediately after use */
        FreeVec (data);

        if (result != U64_OK)
          {
            if (error_details)
              {
                PrintError ("PlayMOD failed with Ultimate64 errors: %s",
                            error_details);
                FreeMem (error_details, strlen (error_details) + 1);
              }
            else
              {
                PrintError ("PlayMOD failed: %s (HTTP/Network error)",
                            U64_GetErrorString (result));

                /* Provide more specific error context */
                switch (result)
                  {
                  case U64_ERR_NOTFOUND:
                    PrintError ("The MOD player endpoint was not found. Check "
                                "Ultimate64 firmware version.");
                    break;
                  case U64_ERR_INVALID:
                    PrintError ("Invalid MOD file format or corrupted data.");
                    break;
                  case U64_ERR_NETWORK:
                    PrintError (
                        "Network communication failed. Check connection.");
                    break;
                  case U64_ERR_TIMEOUT:
                    PrintError ("Request timed out. Ultimate64 may be busy.");
                    break;
                  default:
                    PrintError ("Unexpected error occurred.");
                    break;
                  }
              }
            return 10;
          }

        /* Clean up error details if any */
        if (error_details)
          {
            FreeMem (error_details, strlen (error_details) + 1);
          }

        PrintInfo ("MOD file started successfully: %s", file);
      }
      break;

    default:
      PrintVerbose ("Executing unknown command: %d", cmd);
      PrintError ("Unknown command");
      return 10;
    }

  return 0;
}
