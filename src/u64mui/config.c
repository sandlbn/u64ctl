/* Ultimate64 Control - configuration management (env + config window)
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <intuition/intuition.h>
#include <libraries/mui.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/muimaster.h>

#include <stdlib.h>
#include <string.h>

#include "mui_app.h"
#include "env_utils.h"

/* Load configuration from environment */
void
LoadConfig (struct AppData *data)
{
  STRPTR env_host, env_password, env_port;

  env_host = U64_ReadEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_HOST);
  if (env_host)
    {
      strcpy (data->host, env_host);
      FreeVec (env_host);
    }
  else
    {
      strcpy (data->host, "192.168.1.64");
    }

  env_password = U64_ReadEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_PASSWORD);
  if (env_password)
    {
      strcpy (data->password, env_password);
      FreeVec (env_password);
    }
  else
    {
      data->password[0] = '\0';
    }

  env_port = U64_ReadEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_PORT);
  if (env_port)
    {
      strcpy (data->port, env_port);
      FreeVec (env_port);
    }
  else
    {
      strcpy (data->port, "80");
    }
}

/* Save configuration to environment */
void
SaveConfig (struct AppData *data)
{
  U64_WriteEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_HOST, (CONST_STRPTR)data->host,
                   TRUE);
  if (strlen (data->password) > 0)
    {
      U64_WriteEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_PASSWORD,
                       (CONST_STRPTR)data->password, TRUE);
    }
  else
    {
      DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);
    }
  U64_WriteEnvVar ((CONST_STRPTR)ENV_ULTIMATE64_PORT, (CONST_STRPTR)data->port,
                   TRUE);

  UpdateStatus (data, (CONST_STRPTR) "Configuration saved", TRUE);
}

/* Clear all configuration */
void
ClearConfig (struct AppData *data)
{
  Object *host_display;

  /* Delete environment variables */
  DeleteVar (ENV_ULTIMATE64_HOST, GVF_GLOBAL_ONLY);
  DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
  DeleteVar (ENV_ULTIMATE64_PORT, GVF_GLOBAL_ONLY);

  /* Delete persistent files */
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_HOST);
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_PORT);

  /* Reset to defaults */
  strcpy (data->host, "192.168.1.64");
  data->password[0] = '\0';
  strcpy (data->port, "80");

  /* Update the host display in the main window */
  host_display = (Object *)DoMethod (data->window, MUIM_FindUData,
                                     MAKE_ID ('H', 'O', 'S', 'T'));
  if (host_display)
    {
      set (host_display, MUIA_Text_Contents, (CONST_STRPTR)data->host);
    }

  UpdateStatus (data, (CONST_STRPTR) "Configuration cleared", TRUE);
}

/* Open configuration window */
void
OpenConfigWindow (struct AppData *data)
{
  if (!data->config_window)
    {
      CreateConfigWindow (data);
    }

  if (data->config_window)
    {
      /* Update string gadgets with current values */
      set (data->config_host_string, MUIA_String_Contents,
           (CONST_STRPTR)data->host);
      set (data->config_password_string, MUIA_String_Contents,
           (CONST_STRPTR)data->password);
      set (data->config_port_string, MUIA_String_Contents,
           (CONST_STRPTR)data->port);

      /* Open the window */
      set (data->config_window, MUIA_Window_Open, TRUE);
    }
}

/* Close configuration window */
void
CloseConfigWindow (struct AppData *data)
{
  if (data->config_window)
    {
      set (data->config_window, MUIA_Window_Open, FALSE);
    }
}

/* Apply configuration changes */
void
ApplyConfigChanges (struct AppData *data)
{
  STRPTR host_str, password_str, port_str;
  Object *host_display;

  if (!data->config_window)
    {
      return;
    }

  /* Get values from string gadgets */
  get (data->config_host_string, MUIA_String_Contents, &host_str);
  get (data->config_password_string, MUIA_String_Contents, &password_str);
  get (data->config_port_string, MUIA_String_Contents, &port_str);

  /* Validate and copy values */
  if (host_str && strlen (host_str) > 0)
    {
      strncpy (data->host, host_str, sizeof (data->host) - 1);
      data->host[sizeof (data->host) - 1] = '\0';
    }

  if (password_str)
    {
      strncpy (data->password, password_str, sizeof (data->password) - 1);
      data->password[sizeof (data->password) - 1] = '\0';
    }

  if (port_str && strlen (port_str) > 0)
    {
      int port_num = atoi (port_str);
      if (port_num > 0 && port_num <= 65535)
        {
          strncpy (data->port, port_str, sizeof (data->port) - 1);
          data->port[sizeof (data->port) - 1] = '\0';
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Invalid port number (1-65535)",
                        TRUE);
          return;
        }
    }

  /* Save configuration */
  SaveConfig (data);

  /* Update the host display in the main window */
  host_display = (Object *)DoMethod (data->window, MUIM_FindUData,
                                     MAKE_ID ('H', 'O', 'S', 'T'));
  if (host_display)
    {
      set (host_display, MUIA_Text_Contents, (CONST_STRPTR)data->host);
    }

  /* Close window */
  CloseConfigWindow (data);
}
