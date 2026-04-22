/* Ultimate64 Control - Command Line Interface
 * Print helpers and usage text.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include "cli.h"

#include <stdarg.h>
#include <stdio.h>

/* Print error message */
void
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

/* Print info message */
void
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
void
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

/* Print usage information */
void
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
