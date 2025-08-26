/* Ultimate64 Control - Command Line Interface
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"

/* Version string */
static const char version[] = "$VER: u64ctl 0.3.0 (2025)";

/* Template for ReadArgs */
#define TEMPLATE                                                              \
  "HOST/K,COMMAND/A,FILE/K,ADDRESS/K,TEXT/K,DRIVE/K,MODE/K,"                  \
  "PASSWORD/K,SONG/K/N,VERBOSE/S,QUIET/S"

#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_PORT "Ultimate64/Port"

/* Default values */
#define DEFAULT_HOST "192.168.1.64"
#define DEFAULT_PORT "80"

/* Argument indices */
enum
{
  ARG_HOST = 0,
  ARG_COMMAND,
  ARG_FILE,
  ARG_ADDRESS,
  ARG_TEXT,
  ARG_DRIVE,
  ARG_MODE,
  ARG_PASSWORD,
  ARG_SONG,
  ARG_VERBOSE,
  ARG_QUIET,
  ARG_COUNT
};

/* Command types */
typedef enum
{
  U64CMD_UNKNOWN = 0,
  U64CMD_INFO,
  U64CMD_RESET,
  U64CMD_REBOOT,
  U64CMD_POWEROFF,
  U64CMD_PAUSE,
  U64CMD_RESUME,
  U64CMD_MENU,
  U64CMD_LOAD,
  U64CMD_RUN,
  U64CMD_TYPE,
  U64CMD_MOUNT,
  U64CMD_UNMOUNT,
  U64CMD_DRIVES,
  U64CMD_PEEK,
  U64CMD_POKE,
  U64CMD_PLAYSID,
  U64CMD_PLAYMOD,
  U64CMD_CONFIG,
  U64CMD_SETHOST,
  U64CMD_SETPASSWORD,
  U64CMD_SETPORT,
  U64CMD_CLEARCONFIG,
  U64CMD_LISTCONFIG,     /* List all configuration categories */
  U64CMD_SHOWCONFIG,     /* Show items in a specific category */
  U64CMD_GETCONFIG,      /* Get value of specific configuration item */
  U64CMD_SETCONFIG,      /* Set value of specific configuration item */
  U64CMD_SAVECONFIG,     /* Save current config to flash */
  U64CMD_LOADCONFIG,     /* Load config from flash */
  U64CMD_RESETCONFIG,    /* Reset config to defaults */
} U64CommandType;

/* Command table */
typedef struct
{
  const char *name;
  U64CommandType type;
  const char *description;
  BOOL implemented;
} Command;

static const Command commands[] = {
  { "info", U64CMD_INFO, "Show device information", TRUE },
  { "reset", U64CMD_RESET, "Reset the C64", TRUE },
  { "reboot", U64CMD_REBOOT, "Reboot the Ultimate device", TRUE },
  { "poweroff", U64CMD_POWEROFF, "Power off the C64", TRUE },
  { "pause", U64CMD_PAUSE, "Pause the C64", TRUE },
  { "resume", U64CMD_RESUME, "Resume the C64", TRUE },
  { "menu", U64CMD_MENU, "Press menu button", TRUE },
  { "load", U64CMD_LOAD, "Load PRG file (FILE required)", TRUE },
  { "run", U64CMD_RUN, "Run PRG/CRT file (FILE required)", TRUE },
  { "type", U64CMD_TYPE, "Type text (TEXT required)", TRUE },
  { "mount", U64CMD_MOUNT, "Mount disk image (FILE required)", TRUE },
  { "unmount", U64CMD_UNMOUNT, "Unmount disk (DRIVE required)", TRUE },
  { "drives", U64CMD_DRIVES, "List drives", TRUE },
  { "peek", U64CMD_PEEK, "Read memory (ADDRESS required)", TRUE },
  { "poke", U64CMD_POKE, "Write memory (ADDRESS and value required)", TRUE },
  { "playsid", U64CMD_PLAYSID, "Play SID file (FILE required)", TRUE },
  { "playmod", U64CMD_PLAYMOD, "Play MOD file (FILE required)", TRUE },
  { "config", U64CMD_CONFIG, "Show current configuration", TRUE },
  { "listconfig", U64CMD_LISTCONFIG, "List all configuration categories", TRUE },
  { "showconfig", U64CMD_SHOWCONFIG, "Show configuration category (TEXT=category)", TRUE },
  { "getconfig", U64CMD_GETCONFIG, "Get config item (TEXT=category/item)", TRUE },
  { "setconfig", U64CMD_SETCONFIG, "Set config item (TEXT=category/item, ADDRESS=value)", TRUE },
  { "saveconfig", U64CMD_SAVECONFIG, "Save current configuration to flash", TRUE },
  { "loadconfig", U64CMD_LOADCONFIG, "Load configuration from flash", TRUE },
  { "resetconfig", U64CMD_RESETCONFIG, "Reset configuration to defaults", TRUE },
  { "sethost", U64CMD_SETHOST, "Set default host (HOST required)", TRUE },
  { "setpassword", U64CMD_SETPASSWORD,
    "Set default password (PASSWORD required)", TRUE },
  { "setport", U64CMD_SETPORT, "Set default port (TEXT=port required)", TRUE },
  { "clearconfig", U64CMD_CLEARCONFIG, "Clear all saved configuration", TRUE },

  { NULL, U64CMD_UNKNOWN, NULL, FALSE }
};

/* Global variables */
static BOOL verbose = FALSE;
static BOOL quiet = FALSE;

/* Function prototypes */
static STRPTR ReadEnvVar (CONST_STRPTR var_name);
static BOOL WriteEnvVar (CONST_STRPTR var_name, CONST_STRPTR value,
                         BOOL persistent);
static void LoadSettings (char **host, char **password, UWORD *port);
static BOOL SaveSettings (CONST_STRPTR host, CONST_STRPTR password,
                          UWORD port);

/* Print error message */
static void
PrintError (const char *format, ...)
{
  if (!quiet)
    {
      va_list args;
      va_start (args, format);
      vfprintf (stderr, format, args);
      va_end (args);
      fprintf (stderr, "\n");
    }
}

/* Convert escape sequences in a string */
static STRPTR
ConvertEscapeSequences (CONST_STRPTR input)
{
  STRPTR output;
  ULONG input_len;
  ULONG i, j;

  if (!input)
    {
      return NULL;
    }

  input_len = strlen (input);

  /* Allocate output buffer (same size is enough since escapes make strings
   * shorter) */
  output = AllocMem (input_len + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!output)
    {
      return NULL;
    }

  /* Convert escape sequences */
  for (i = 0, j = 0; i < input_len; i++)
    {
      if (input[i] == '\\' && i + 1 < input_len)
        {
          /* Handle escape sequence */
          switch (input[i + 1])
            {
            case 'n':
              output[j++] = '\n';
              i++; /* Skip the next character */
              break;
            case 'r':
              output[j++] = '\r';
              i++; /* Skip the next character */
              break;
            case 't':
              output[j++] = '\t';
              i++; /* Skip the next character */
              break;
            case '\\':
              output[j++] = '\\';
              i++; /* Skip the next character */
              break;
            case '"':
              output[j++] = '"';
              i++; /* Skip the next character */
              break;
            case '0':
              output[j++] = '\0';
              i++; /* Skip the next character */
              break;
            default:
              /* Unknown escape sequence, keep the backslash */
              output[j++] = input[i];
              break;
            }
        }
      else
        {
          /* Regular character */
          output[j++] = input[i];
        }
    }

  output[j] = '\0';

  return output;
}

/* Print info message */
static void
PrintInfo (const char *format, ...)
{
  if (!quiet)
    {
      va_list args;
      va_start (args, format);
      vprintf (format, args);
      va_end (args);
      printf ("\n");
    }
}

/* Print verbose message */
static void
PrintVerbose (const char *format, ...)
{
  if (verbose && !quiet)
    {
      va_list args;
      va_start (args, format);
      printf ("[DEBUG] ");
      vprintf (format, args);
      va_end (args);
      printf ("\n");
    }
}

/* Parse command string to command type */
static U64CommandType
ParseCommand (const char *cmd)
{
  int i;
  char lower[32];
  int len;

  if (!cmd)
    return U64CMD_UNKNOWN;

  /* Convert to lowercase for comparison */
  len = strlen (cmd);
  if (len >= (int)sizeof (lower))
    len = sizeof (lower) - 1;

  for (i = 0; i < len; i++)
    {
      lower[i] = tolower (cmd[i]);
    }
  lower[len] = '\0';

  PrintVerbose ("Looking for command: '%s'", lower);

  /* Find command in table */
  for (i = 0; commands[i].name != NULL; i++)
    {
      PrintVerbose ("Checking against: '%s' (type %d)", commands[i].name,
                    commands[i].type);
      if (strcmp (lower, commands[i].name) == 0)
        {
          PrintVerbose ("Found command: %s -> %d", commands[i].name,
                        commands[i].type);
          return commands[i].type;
        }
    }

  PrintVerbose ("Command not found: %s", lower);
  return U64CMD_UNKNOWN;
}

/* Parse mount mode string */
static U64MountMode
ParseMountMode (const char *mode)
{
  if (!mode)
    return U64_MOUNT_RO;

  if (stricmp (mode, "rw") == 0 || stricmp (mode, "readwrite") == 0)
    {
      return U64_MOUNT_RW;
    }
  else if (stricmp (mode, "ro") == 0 || stricmp (mode, "readonly") == 0)
    {
      return U64_MOUNT_RO;
    }
  else if (stricmp (mode, "ul") == 0 || stricmp (mode, "unlinked") == 0)
    {
      return U64_MOUNT_UL;
    }

  return U64_MOUNT_RO; /* Default */
}

/* Load file into memory */
static UBYTE *
LoadFile (const char *filename, ULONG *size)
{
  BPTR file;
  LONG file_size;
  UBYTE *buffer;

  file = Open ((CONST_STRPTR)filename, MODE_OLDFILE);
  if (!file)
    {
      PrintError ("Cannot open file: %s", filename);
      return NULL;
    }

  /* Get file size */
  Seek (file, 0, OFFSET_END);
  file_size = Seek (file, 0, OFFSET_BEGINNING);

  if (file_size <= 0)
    {
      Close (file);
      PrintError ("Invalid file size: %s", filename);
      return NULL;
    }

  /* Allocate buffer */
  buffer = AllocMem (file_size, MEMF_PUBLIC);
  if (!buffer)
    {
      Close (file);
      PrintError ("Out of memory (%ld bytes)", file_size);
      return NULL;
    }

  /* Read file */
  if (Read (file, buffer, file_size) != file_size)
    {
      FreeMem (buffer, file_size);
      Close (file);
      PrintError ("Error reading file: %s", filename);
      return NULL;
    }

  Close (file);

  if (size)
    *size = file_size;

  PrintVerbose ("Loaded %ld bytes from %s", file_size, filename);

  return buffer;
}

/* Read environment variable */
static STRPTR
ReadEnvVar (CONST_STRPTR var_name)
{
  LONG result;
  STRPTR buffer;
  ULONG buffer_size = 256;

  /* Allocate buffer */
  buffer = AllocMem (buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!buffer)
    {
      return NULL;
    }

  /* Try to read the variable */
  result = GetVar ((STRPTR)var_name, buffer, buffer_size, GVF_GLOBAL_ONLY);

  if (result > 0)
    {
      /* Variable found - resize buffer to actual size */
      STRPTR final_buffer = AllocMem (result + 1, MEMF_PUBLIC);
      if (final_buffer)
        {
          strcpy (final_buffer, buffer);
          FreeMem (buffer, buffer_size);
          PrintVerbose ("Read ENV:%s = '%s'", var_name, final_buffer);
          return final_buffer;
        }
    }

  /* Variable not found or error */
  FreeMem (buffer, buffer_size);
  PrintVerbose ("ENV:%s not found", var_name);
  return NULL;
}

/* Write environment variable */
static BOOL
WriteEnvVar (CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent)
{
  LONG result;
  ULONG flags = GVF_GLOBAL_ONLY;

  if (!var_name || !value)
    {
      return FALSE;
    }

  PrintVerbose ("Writing ENV:%s = '%s' (persistent: %s)", var_name, value,
                persistent ? "yes" : "no");

  /* Set the variable */
  result = SetVar ((STRPTR)var_name, (STRPTR)value, strlen (value), flags);

  if (result && persistent)
    {
      /* Also save to ENVARC: for persistence across reboots */
      BPTR file;
      STRPTR envarc_path;
      ULONG path_len = strlen ("ENVARC:") + strlen (var_name) + 1;

      envarc_path = AllocMem (path_len, MEMF_PUBLIC);
      if (envarc_path)
        {
          strcpy (envarc_path, "ENVARC:");
          strcat (envarc_path, var_name);

          /* Create directory structure if needed */
          STRPTR dir_end = strrchr (envarc_path, '/');
          if (dir_end)
            {
              *dir_end = '\0';
              CreateDir (envarc_path);
              *dir_end = '/';
            }

          /* Write to ENVARC: */
          file = Open (envarc_path, MODE_NEWFILE);
          if (file)
            {
              Write (file, (STRPTR)value, strlen (value));
              Close (file);
              PrintVerbose ("Saved to %s", envarc_path);
            }
          else
            {
              PrintVerbose ("Failed to save to %s", envarc_path);
            }

          FreeMem (envarc_path, path_len);
        }
    }

  return result ? TRUE : FALSE;
}

/* Load settings from environment */
static void
LoadSettings (char **host, char **password, UWORD *port)
{
  STRPTR env_host, env_password, env_port;

  PrintVerbose ("Loading settings from ENV:...");

  /* Load host */
  env_host = ReadEnvVar (ENV_ULTIMATE64_HOST);
  if (env_host)
    {
      *host = env_host;
    }
  else
    {
      /* Use default and save it */
      *host = AllocMem (strlen (DEFAULT_HOST) + 1, MEMF_PUBLIC);
      if (*host)
        {
          strcpy (*host, DEFAULT_HOST);
          WriteEnvVar (ENV_ULTIMATE64_HOST, DEFAULT_HOST, TRUE);
          PrintInfo ("Set default host to %s", DEFAULT_HOST);
        }
    }

  /* Load password (optional) */
  env_password = ReadEnvVar (ENV_ULTIMATE64_PASSWORD);
  if (env_password)
    {
      *password = env_password;
    }
  else
    {
      *password = NULL;
      PrintVerbose ("No password set in environment");
    }

  /* Load port */
  env_port = ReadEnvVar (ENV_ULTIMATE64_PORT);
  if (env_port)
    {
      UWORD parsed_port = (UWORD)atoi (env_port);
      if (parsed_port > 0 && parsed_port <= 65535)
        {
          *port = parsed_port;
        }
      else
        {
          PrintVerbose ("Invalid port in ENV: %s, using default 80", env_port);
          *port = 80;
          WriteEnvVar (ENV_ULTIMATE64_PORT, DEFAULT_PORT, TRUE);
        }
      FreeMem (env_port, strlen (env_port) + 1);
    }
  else
    {
      *port = 80;
      WriteEnvVar (ENV_ULTIMATE64_PORT, DEFAULT_PORT, TRUE);
      PrintVerbose ("Set default port to 80");
    }

  PrintInfo ("Loaded settings: host=%s, port=%d, password=%s",
             *host ? *host : "none", *port, *password ? "***" : "none");
}

/* Save settings to environment */
static BOOL
SaveSettings (CONST_STRPTR host, CONST_STRPTR password, UWORD port)
{
  char port_str[16];
  BOOL success = TRUE;

  PrintInfo ("Saving settings to ENV:...");

  if (host)
    {
      success &= WriteEnvVar (ENV_ULTIMATE64_HOST, host, TRUE);
    }

  if (password)
    {
      success &= WriteEnvVar (ENV_ULTIMATE64_PASSWORD, password, TRUE);
    }
  else
    {
      /* Clear password if NULL */
      DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
      /* Also remove from ENVARC: */
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);
    }

  if (port == 0)
    {
      port = 80; /* Default to 80 if somehow 0 was passed */
      PrintVerbose ("Port was 0, defaulting to 80");
    }

  sprintf (port_str, "%d", port);
  success &= WriteEnvVar (ENV_ULTIMATE64_PORT, port_str, TRUE);

  return success;
}

/* Execute command */
static int
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
        data = LoadFile (file, &size);
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
            FreeMem (data, size);
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
                FreeMem (data, size);
                return 10;
              }

            PrintVerbose ("Loading PRG file ...");
            result = U64_LoadPRG (conn, data, size, &error_details);
          }

        /* Free file data immediately after use */
        FreeMem (data, size);

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
        data = LoadFile (file, &size);
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
                FreeMem (data, size);
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
                FreeMem (data, size);
                return 10;
              }

            PrintVerbose ("Running PRG file with enhanced error reporting...");
            result = U64_RunPRG (conn, data, size, &error_details);
          }

        /* Free file data immediately after use */
        FreeMem (data, size);

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
        STRPTR converted_text = ConvertEscapeSequences (text);
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
        FreeMem (converted_text, strlen (converted_text) + 1);

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
        data = LoadFile (file, &size);
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
            FreeMem (data, size);
            return 10;
          }

        PrintVerbose ("Playing SID file: %s (song %d)", file, song_num);
        PrintVerbose ("Using enhanced PlaySID with error reporting...");

        /* Use enhanced PlaySID function with error details */
        result = U64_PlaySID (conn, data, size, song_num, &error_details);

        /* Free file data immediately after use */
        FreeMem (data, size);

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
        disk_data = LoadFile (file, &disk_size);
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
            FreeMem (disk_data, disk_size);
            return 10;
          }

        /* Free the disk data - the mount function will reload it */
        FreeMem (disk_data, disk_size);

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
        const char *drive_letters[] = { "a", "b", "c", "d" };
        int i;

        PrintInfo ("Drive Status:");
        PrintInfo ("=============");

        for (i = 0; i < 4; i++)
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
        data = LoadFile (file, &size);
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
            FreeMem (data, size);
            return 10;
          }

        PrintVerbose ("Playing MOD file: %s", file);
        PrintVerbose ("Using enhanced PlayMOD with error reporting...");

        /* Use enhanced PlayMOD function with error details */
        result = U64_PlayMOD (conn, data, size, &error_details);

        /* Free file data immediately after use */
        FreeMem (data, size);

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

/* Print usage information */
static void
PrintUsage (void)
{
  int i;

  printf ("u64ctl CLI v1.0 by sandlbn\n");
  printf ("Usage: u64ctl HOST COMMAND [options]\n\n");
  printf ("Commands:\n");

  for (i = 0; commands[i].name != NULL; i++)
    {
      printf ("  %-10s - %s%s\n", commands[i].name, commands[i].description,
              commands[i].implemented ? "" : " (not yet implemented)");
    }

  printf ("\nOptions:\n");
  printf ("  FILE       - File to load/run/mount/play\n");
  printf ("  ADDRESS    - Memory address (decimal or hex with 0x prefix)\n");
  printf ("  TEXT       - Text to type or value for poke\n");
  printf ("  DRIVE      - Drive letter (a-d)\n");
  printf ("  MODE       - Mount mode (rw/ro/ul)\n");
  printf ("  PASSWORD   - Device password\n");
  printf ("  SONG       - Song number for SID files\n");
  printf ("  VERBOSE    - Verbose output\n");
  printf ("  QUIET      - Suppress output\n");

  printf ("\nConfiguration Examples:\n");
  printf ("  u64ctl sethost HOST 192.168.1.64      - Set default host\n");
  printf ("  u64ctl setpassword PASSWORD secret123  - Set password\n");
  printf ("  u64ctl setport TEXT 8080               - Set custom port\n");
  printf ("  u64ctl config                          - Show current config\n");
  printf ("  u64ctl clearconfig                     - Clear all settings\n");

  printf ("\nBasic Control Examples:\n");
  printf ("  u64ctl info                            - Device information\n");
  printf ("  u64ctl 192.168.1.64 info              - Info with host "
          "override\n");
  printf ("  u64ctl reset                           - Reset the C64\n");
  printf ("  u64ctl reboot                          - Reboot Ultimate "
          "device\n");
  printf ("  u64ctl poweroff                        - Power off C64\n");
  printf ("  u64ctl pause                           - Pause C64 execution\n");
  printf ("  u64ctl resume                          - Resume C64 execution\n");
  printf ("  u64ctl menu                            - Press menu button\n");

  printf ("\nFile Operations Examples:\n");
  printf ("  u64ctl load FILE games/elite.prg      - Load PRG file\n");
  printf ("  u64ctl run FILE games/elite.prg       - Run PRG file\n");
  printf ("  u64ctl run FILE carts/simons.crt      - Run cartridge file\n");
  printf ("  u64ctl load FILE \"work/my game.prg\"   - Load file with "
          "spaces\n");

  printf ("\nDisk Operations Examples:\n");
  printf ("  u64ctl mount FILE disk1.d64 DRIVE a   - Mount disk to drive A\n");
  printf ("  u64ctl mount FILE disk1.d64 DRIVE a MODE rw - Mount "
          "read/write\n");
  printf ("  u64ctl mount FILE disk1.d71 DRIVE b MODE ro - Mount read-only\n");
  printf ("  u64ctl mount FILE disk1.d81 DRIVE c MODE ul - Mount unlinked\n");
  printf ("  u64ctl unmount DRIVE a                 - Unmount drive A\n");
  printf ("  u64ctl unmount DRIVE b                 - Unmount drive B\n");
  printf ("  u64ctl drives                          - Show all drive "
          "status\n");

  printf ("\nText Input Examples:\n");
  printf ("  u64ctl type TEXT \"hello world\"        - Type simple text\n");
  printf ("  u64ctl type TEXT \"load\\\"*\\\",8,1\\n\"   - Type LOAD "
          "command\n");
  printf ("  u64ctl type TEXT \"run\\n\"             - Type RUN + Enter\n");
  printf ("  u64ctl type TEXT \"list\\n\"            - Type LIST + Enter\n");
  printf ("  u64ctl type TEXT \"10 print\\\"hello\\\"\\n\" - Type BASIC "
          "line\n");

  printf ("\nMemory Operations Examples:\n");
  printf ("  u64ctl peek ADDRESS 0xd020            - Read border color\n");
  printf ("  u64ctl peek ADDRESS 53280             - Same as above "
          "(decimal)\n");
  printf ("  u64ctl peek ADDRESS $d020             - Same with $ prefix\n");
  printf ("  u64ctl poke ADDRESS 0xd020 TEXT 1     - Set border to white\n");
  printf ("  u64ctl poke ADDRESS 53280 TEXT 0      - Set border to black\n");
  printf ("  u64ctl poke ADDRESS $d021 TEXT 6      - Set background to "
          "blue\n");

  printf ("\nMusic Examples:\n");
  printf ("  u64ctl playsid FILE music.sid         - Play SID file (song "
          "0)\n");
  printf ("  u64ctl playsid FILE music.sid SONG 2  - Play specific song\n");
  printf ("  u64ctl playsid FILE hvsc/rob_hubbard.sid SONG 1 VERBOSE\n");
  printf ("  u64ctl playmod FILE music.mod         - Play MOD file\n");
  printf ("  u64ctl playmod FILE \"ambient/track1.mod\" VERBOSE\n");
  printf("\nConfiguration Management Examples:\n");
  printf("  u64ctl listconfig                      - List all configuration categories\n");
  printf("  u64ctl showconfig TEXT \"Drive A Settings\" - Show items in category\n");
  printf("  u64ctl getconfig TEXT \"Drive A Settings/Drive Type\" - Get specific item\n");
  printf("  u64ctl setconfig TEXT \"Drive A Settings/Drive Type\" ADDRESS \"1571\"\n");
  printf("  u64ctl setconfig TEXT \"Drive A Settings/Drive Bus ID\" ADDRESS \"9\"\n");
  printf("  u64ctl saveconfig                      - Save changes to flash\n");
  printf("  u64ctl loadconfig                      - Load saved config\n");
  printf("  u64ctl resetconfig                     - Reset to factory defaults\n");

  printf("\nConfiguration Workflow:\n");
  printf("  1. u64ctl listconfig                   - See available categories\n");
  printf("  2. u64ctl showconfig TEXT \"category\"   - See items in category\n");
  printf("  3. u64ctl getconfig TEXT \"cat/item\"    - Get current value\n");
  printf("  4. u64ctl setconfig TEXT \"cat/item\" ADDRESS \"value\" - Change value\n");
  printf("  5. u64ctl saveconfig                   - Make changes permanent\n");

}

/* Main entry point */
int
main (int argc, char **argv)
{
  struct RDArgs *rdargs;
  LONG args[ARG_COUNT];
  U64Connection *conn = NULL;
  U64CommandType cmd;
  int retval = 0;
  char *command;
  char *host_arg;
  char *password_arg;

  /* Settings loaded from ENV: */
  char *env_host = NULL;
  char *env_password = NULL;
  UWORD env_port = 80;

  /* Final settings to use */
  char *final_host = NULL;
  char *final_password = NULL;

  /* Initialize arguments array */
  memset (args, 0, sizeof (args));

  /* Check for help request */
  if (argc > 1
      && (strcmp (argv[1], "?") == 0 || stricmp (argv[1], "help") == 0
          || strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0))
    {
      PrintUsage ();
      return 0;
    }

  /* Parse arguments */
  rdargs = ReadArgs ((CONST_STRPTR)TEMPLATE, args, NULL);
  if (!rdargs)
    {
      PrintError ("Invalid arguments. Use 'u64ctl ?' for help");
      return 5;
    }

  /* Get command line arguments */
  host_arg = (char *)args[ARG_HOST];
  command = (char *)args[ARG_COMMAND];
  password_arg = (char *)args[ARG_PASSWORD];

  /* Set flags */
  verbose = args[ARG_VERBOSE] ? TRUE : FALSE;
  quiet = args[ARG_QUIET] ? TRUE : FALSE;

  U64_SetVerboseMode (verbose);

  /* Load settings from ENV: */
  LoadSettings (&env_host, &env_password, &env_port);

  /* Command line overrides ENV: settings */
  final_host = host_arg ? host_arg : env_host;
  final_password = password_arg ? password_arg : env_password;

  if (!final_host)
    {
      PrintError ("No host specified. Use HOST argument or run 'ultimate64 "
                  "sethost HOST <ip>'");
      retval = 5;
      goto cleanup;
    }

  PrintVerbose ("Using host: %s", final_host);
  PrintVerbose ("Using password: %s", final_password ? "***" : "none");

  /* Parse command */
  cmd = ParseCommand (command);
  if (cmd == U64CMD_UNKNOWN)
    {
      PrintError ("Unknown command: %s", command);
      PrintInfo ("Use 'ultimate64 ?' for help");
      retval = 5;
      goto cleanup;
    }

  /* Initialize library */
  if (!U64_InitLibrary ())
    {
      PrintError ("Failed to initialize Ultimate64 library");
      retval = 20;
      goto cleanup;
    }

  /* Connect to device */
  PrintVerbose ("Connecting to %s...", final_host);
  conn = U64_Connect ((CONST_STRPTR)final_host, (CONST_STRPTR)final_password);
  if (!conn)
    {
      PrintError ("Failed to connect to %s", final_host);
      retval = 10;
      goto cleanup;
    }
  PrintVerbose ("Connected to %s", final_host);

  /* Execute command */
  retval = ExecuteCommand (conn, cmd, args, env_host, env_password, env_port,
                           host_arg, password_arg);

cleanup:
  /* Disconnect first - this is the most dangerous part */
  if (conn)
    {
      PrintVerbose ("Disconnecting...");
      U64_Disconnect (conn);
      conn = NULL; /* Prevent accidental reuse */
      PrintVerbose ("Disconnected successfully");
    }

  /* Add delay before final cleanup */
  Delay (5); /* 100ms */

  /* Minimal cleanup - let system handle most of it */
  PrintVerbose ("Performing minimal cleanup...");
  U64_CleanupLibrary ();

  /* Free ENV: allocated strings */
  if (env_host)
    FreeMem (env_host, strlen (env_host) + 1);
  if (env_password)
    FreeMem (env_password, strlen (env_password) + 1);

  PrintVerbose ("Freeing arguments...");
  FreeArgs (rdargs);

  PrintVerbose ("Exiting with code %d", retval);
  return retval;
}