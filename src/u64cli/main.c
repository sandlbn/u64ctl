/* Ultimate64 Control - Command Line Interface
 * Entry point.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>

#include "cli.h"

/* Version string */
static const char version[] = "$VER: u64ctl 0.3.1 (2025)";

/* Module globals */
BOOL verbose = FALSE;
BOOL quiet = FALSE;

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
    FreeVec (env_host);
  if (env_password)
    FreeVec (env_password);

  PrintVerbose ("Freeing arguments...");
  FreeArgs (rdargs);

  PrintVerbose ("Exiting with code %d", retval);
  return retval;
}
