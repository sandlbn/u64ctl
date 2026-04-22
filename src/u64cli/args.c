/* Ultimate64 Control - Command Line Interface
 * Command table and argument parsers.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include "cli.h"

#include <ctype.h>
#include <string.h>

/* Command table */
const Command commands[] = {
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

/* Parse command string to command type */
U64CommandType
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
U64MountMode
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
