/* Ultimate64 Control - Command Line Interface
 * Environment-backed settings load/save.
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include "cli.h"

#include "env_utils.h"

#include <proto/dos.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

/* Load settings from environment */
void
LoadSettings (char **host, char **password, UWORD *port)
{
  STRPTR env_host, env_password, env_port;

  PrintVerbose ("Loading settings from ENV:...");

  /* Load host */
  env_host = U64_ReadEnvVar (ENV_ULTIMATE64_HOST);
  if (env_host)
    {
      *host = env_host;
    }
  else
    {
      /* Use default and save it */
      ULONG default_len = strlen (DEFAULT_HOST) + 1;
      *host = AllocVec (default_len, MEMF_PUBLIC);
      if (*host)
        {
          strcpy (*host, DEFAULT_HOST);
          U64_WriteEnvVar (ENV_ULTIMATE64_HOST, DEFAULT_HOST, TRUE);
          PrintInfo ("Set default host to %s", DEFAULT_HOST);
        }
    }

  /* Load password (optional) */
  env_password = U64_ReadEnvVar (ENV_ULTIMATE64_PASSWORD);
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
  env_port = U64_ReadEnvVar (ENV_ULTIMATE64_PORT);
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
          U64_WriteEnvVar (ENV_ULTIMATE64_PORT, DEFAULT_PORT, TRUE);
        }
      FreeVec (env_port);
    }
  else
    {
      *port = 80;
      U64_WriteEnvVar (ENV_ULTIMATE64_PORT, DEFAULT_PORT, TRUE);
      PrintVerbose ("Set default port to 80");
    }

  PrintInfo ("Loaded settings: host=%s, port=%d, password=%s",
             *host ? *host : "none", *port, *password ? "***" : "none");
}

/* Save settings to environment */
BOOL
SaveSettings (CONST_STRPTR host, CONST_STRPTR password, UWORD port)
{
  char port_str[16];
  BOOL success = TRUE;

  PrintInfo ("Saving settings to ENV:...");

  if (host)
    {
      success &= U64_WriteEnvVar (ENV_ULTIMATE64_HOST, host, TRUE);
    }

  if (password)
    {
      success &= U64_WriteEnvVar (ENV_ULTIMATE64_PASSWORD, password, TRUE);
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
  success &= U64_WriteEnvVar (ENV_ULTIMATE64_PORT, port_str, TRUE);

  return success;
}
