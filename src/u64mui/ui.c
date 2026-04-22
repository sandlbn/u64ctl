/* Ultimate64 Control - UI construction and status display
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <string.h>

#include "mui_app.h"

/* Update status text and optionally add to output */
void
UpdateStatus (struct AppData *data, CONST_STRPTR text, BOOL add_to_output)
{
  /* Always update the status line */
  set (data->txt_status, MUIA_Text_Contents, text);

  /* If add_to_output is TRUE, also show it in the output area */
  if (add_to_output && data->txt_output)
    {
      /* Just set the single line - no accumulation */
      set (data->txt_output, MUIA_Text_Contents, text);
    }
}

/* Create configuration window */
Object *
CreateConfigWindow (struct AppData *data)
{
  Object *window, *ok_button, *cancel_button;
  Object *host_string, *password_string, *port_string;

  window = WindowObject, MUIA_Window_Title,
  (CONST_STRPTR) "Ultimate64 Configuration", MUIA_Window_ID,
  MAKE_ID ('C', 'F', 'G', 'W'), MUIA_Window_Width, 400, MUIA_Window_Height,
  200, MUIA_Window_CloseGadget, FALSE,

  WindowContents, VGroup, Child, VGroup, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, (CONST_STRPTR) "Connection Settings",

  Child, HGroup, Child, Label ("Host:"), Child, host_string = StringObject,
  MUIA_String_Contents, (CONST_STRPTR)data->host, MUIA_String_MaxLen, 255, End,
  End,

  Child, HGroup, Child, Label ("Port:"), Child, port_string = StringObject,
  MUIA_String_Contents, (CONST_STRPTR)data->port, MUIA_String_MaxLen, 7,
  MUIA_String_Accept, (CONST_STRPTR) "0123456789", End, Child, HSpace (0), End,

  Child, HGroup, Child, Label ("Password:"), Child,
  password_string = StringObject, MUIA_String_Contents,
  (CONST_STRPTR)data->password, MUIA_String_MaxLen, 255, MUIA_String_Secret,
  TRUE, End, End,

  Child, VSpace (10),

  Child, TextObject, MUIA_Text_Contents,
  (CONST_STRPTR) "Leave password empty if not required.\nSettings will be "
                 "saved to ENV: and ENVARC:",
  MUIA_Text_PreParse, (CONST_STRPTR) "\33c", MUIA_Font, MUIV_Font_Tiny, End,
  End,

  Child, VSpace (10),

  Child, HGroup, Child, HSpace (0), Child, ok_button = SimpleButton ("OK"),
  Child, HSpace (10), Child, cancel_button = SimpleButton ("Cancel"), Child,
  HSpace (0), End, End, End;

  if (window)
    {
      /* Store object pointers */
      data->config_window = window;
      data->config_host_string = host_string;
      data->config_password_string = password_string;
      data->config_port_string = port_string;
      data->config_ok_button = ok_button;
      data->config_cancel_button = cancel_button;

      /* Set up notifications */
      DoMethod (ok_button, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
                MUIM_Application_ReturnID, ID_CONFIG_OK);

      DoMethod (cancel_button, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
                MUIM_Application_ReturnID, ID_CONFIG_CANCEL);

      /* Add window to application */
      DoMethod (data->app, OM_ADDMEMBER, window);
    }

  return window;
}

/* Create menu bar */
Object *
CreateMenuBar (struct AppData *data)
{
  extern struct NewMenu MainMenu[];
  Object *menu;

  menu = MUI_MakeObject (MUIO_MenustripNM, MainMenu, 0);

  if (menu)
    {
      data->menu_strip = menu;
    }

  return menu;
}

/* Show about dialog */
void
ShowAboutDialog (struct AppData *data)
{
  Object *about_window, *ok_button;
  BOOL running = TRUE;
  ULONG signals;
  ULONG about_id = 999; /* Unique ID for about dialog */

  about_window = WindowObject, MUIA_Window_Title,
  (CONST_STRPTR) "About Ultimate64 Control", MUIA_Window_Width, 300,
  MUIA_Window_Height, 150, MUIA_Window_CloseGadget, FALSE,
  MUIA_Window_DepthGadget, FALSE, MUIA_Window_SizeGadget, FALSE,
  MUIA_Window_DragBar, TRUE,

  WindowContents, VGroup, Child, VSpace (10), Child, TextObject,
  MUIA_Text_Contents, (CONST_STRPTR) "Ultimate64 Control v1.0",
  MUIA_Text_PreParse, (CONST_STRPTR) "\33c\33b", MUIA_Font, MUIV_Font_Big, End,
  Child, VSpace (5), Child, TextObject, MUIA_Text_Contents,
  (CONST_STRPTR) "MUI Interface for u64ctl",
  MUIA_Text_PreParse, (CONST_STRPTR) "\33c", End, Child, VSpace (5), Child,
  TextObject, MUIA_Text_Contents, (CONST_STRPTR) "2025 Marcin Spoczynski",
  MUIA_Text_PreParse, (CONST_STRPTR) "\33c", MUIA_Font, MUIV_Font_Tiny, End,
  Child, VSpace (10), Child, HGroup, Child, HSpace (0), Child,
  ok_button = SimpleButton ("OK"), Child, HSpace (0), End, Child, VSpace (10),
  End, End;

  if (about_window)
    {
      DoMethod (data->app, OM_ADDMEMBER, about_window);

      /* Set up notification for OK button */
      DoMethod (ok_button, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
                MUIM_Application_ReturnID, about_id);

      set (about_window, MUIA_Window_Open, TRUE);

      /* Simple event loop for about dialog */
      while (running)
        {
          ULONG id = DoMethod (data->app, MUIM_Application_Input, &signals);

          if (id == about_id)
            {
              running = FALSE;
            }

          if (running && signals)
            {
              Wait (signals);
            }
        }

      set (about_window, MUIA_Window_Open, FALSE);
      DoMethod (data->app, OM_REMMEMBER, about_window);
      MUI_DisposeObject (about_window);
    }
}

/* Update disk status display */
void
UpdateDiskDisplay (struct AppData *data)
{
  const char *drive_letters[] = { "a", "b", "c", "d" };
  int i;
  char status_text[1024];

  if (!data->connection)
    {
      set (data->txt_disk_status, MUIA_Text_Contents,
           (CONST_STRPTR) "Not connected");
      return;
    }

  strcpy (status_text, "");

  for (i = 0; i < 4; i++)
    {
      BOOL is_mounted;
      STRPTR image_name = NULL;
      U64MountMode mode;

      if (U64_GetDriveStatus (data->connection, drive_letters[i], &is_mounted,
                              &image_name, &mode)
          == U64_OK)
        {
          char drive_info[256];
          if (is_mounted)
            {
              sprintf (drive_info, "Drive %s: %s (%s)\n", drive_letters[i],
                       image_name ? (char *)image_name : "mounted",
                       mode == U64_MOUNT_RW   ? "R/W"
                       : mode == U64_MOUNT_RO ? "R/O"
                                              : "Unlinked");
              if (image_name)
                FreeMem (image_name, strlen ((char *)image_name) + 1);
            }
          else
            {
              sprintf (drive_info, "Drive %s: empty\n", drive_letters[i]);
            }
          strcat (status_text, drive_info);
        }
      else
        {
          char drive_info[64];
          sprintf (drive_info, "Drive %s: unknown\n", drive_letters[i]);
          strcat (status_text, drive_info);
        }
    }

  set (data->txt_disk_status, MUIA_Text_Contents, (CONST_STRPTR)status_text);
}
