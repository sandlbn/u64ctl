/* Ultimate64 Control
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <workbench/workbench.h>

#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"

/* MUI Object creation macros */
#define ApplicationObject   MUI_NewObject(MUIC_Application
#define WindowObject        MUI_NewObject(MUIC_Window
#define VGroup              MUI_NewObject(MUIC_Group
#define HGroup              MUI_NewObject(MUIC_Group, MUIA_Group_Horiz, TRUE
#define GroupObject         MUI_NewObject(MUIC_Group
#define StringObject        MUI_NewObject(MUIC_String
#define TextObject          MUI_NewObject(MUIC_Text
#define CycleObject         MUI_NewObject(MUIC_Cycle
#define RegisterObject      MUI_NewObject(MUIC_Register
#define SimpleButton(text)                                                    \
  MUI_NewObject (                                                             \
      MUIC_Text, MUIA_Frame, MUIV_Frame_Button, MUIA_Font, MUIV_Font_Button,  \
      MUIA_Text_Contents, text, MUIA_Text_PreParse, "\33c", MUIA_Background,  \
      MUII_ButtonBack, MUIA_InputMode, MUIV_InputMode_RelVerify, TAG_DONE)
#define Label(text)                                                           \
  MUI_NewObject (MUIC_Text, MUIA_Text_Contents, text, MUIA_Text_PreParse,     \
                 "\33r", TAG_DONE)

/* Define IPTR if not available */
#ifndef IPTR
#define IPTR ULONG
#endif

/* MAKE_ID macro if not defined */
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d)                                                   \
  ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

/* Environment variable names */
#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_PORT "Ultimate64/Port"

/* Menu IDs */
#define MENU_CONFIG_HOST 1000
#define MENU_CONFIG_PASSWORD 1001
#define MENU_CONFIG_PORT 1002
#define MENU_CONFIG_SAVE 1003
#define MENU_CONFIG_LOAD 1004
#define MENU_CONFIG_CLEAR 1005

/* MUI IDs */
enum
{
  ID_CONNECT = 1,
  ID_DISCONNECT,
  ID_RESET,
  ID_REBOOT,
  ID_POWEROFF,
  ID_PAUSE,
  ID_RESUME,
  ID_MENU,
  ID_LOAD_PRG,
  ID_RUN_PRG,
  ID_RUN_CRT,
  ID_TYPE,
  ID_MOUNT,
  ID_UNMOUNT,
  ID_PEEK,
  ID_POKE,
  ID_PLAY_SID,
  ID_PLAY_MOD,
  ID_DRIVES_STATUS,
  ID_QUIT,
  ID_ABOUT,

  /* Configuration dialog IDs */
  ID_CONFIG_OK,
  ID_CONFIG_CANCEL,
  ID_CONFIG_OPEN,
  ID_CONFIG_CLEAR
};

/* Application data structure */
struct AppData
{
  Object *app;
  Object *window;
  Object *register_tabs;

  /* Menu */
  Object *menu_strip;

  /* Configuration window */
  Object *config_window;
  Object *config_host_string;
  Object *config_password_string;
  Object *config_port_string;
  Object *config_ok_button;
  Object *config_cancel_button;

  /* Connection */
  Object *btn_connect;
  Object *txt_status;
  Object *chk_verbose;

  /* Disks & Carts tab */
  Object *cyc_drive;
  Object *cyc_mode;
  Object *btn_mount;
  Object *btn_unmount;
  Object *btn_drives_status;
  Object *btn_load_prg;
  Object *btn_run_prg;
  Object *btn_run_crt;
  Object *txt_disk_status;

  /* Memory tab */
  Object *str_peek_addr;
  Object *btn_peek;
  Object *str_poke_addr;
  Object *str_poke_value;
  Object *btn_poke;
  Object *txt_memory_result;

  /* Machine tab */
  Object *btn_reset;
  Object *btn_reboot;
  Object *btn_poweroff;
  Object *btn_pause;
  Object *btn_resume;
  Object *btn_menu;

  /* Music tab */
  Object *str_song_num;
  Object *btn_play_sid;
  Object *btn_play_mod;

  /* Always visible - Text input */
  Object *str_type;
  Object *btn_type;

  /* Output area */
  Object *txt_output;

  /* Configuration strings */
  char host[256];
  char password[256];
  char port[8];

  U64Connection *connection;
  BOOL verbose_mode;
};

/* Version string */
static const char version[] = "$VER: Ultimate64_MUI 1.0 (2025)";

/* Library bases */
struct Library *MUIMasterBase = NULL;

/* Tab titles */
static const char *tab_titles[]
    = { (CONST_STRPTR) "Disks & Carts", (CONST_STRPTR) "Memory",
        (CONST_STRPTR) "Machine", (CONST_STRPTR) "Music", NULL };

/* Drive options */
static const char *drive_options[]
    = { (CONST_STRPTR) "Drive A", (CONST_STRPTR) "Drive B",
        (CONST_STRPTR) "Drive C", (CONST_STRPTR) "Drive D", NULL };
static const char *drive_ids[] = { "a", "b", "c", "d" };

/* Mount mode options */
static const char *mode_options[]
    = { (CONST_STRPTR) "Read/Write", (CONST_STRPTR) "Read Only",
        (CONST_STRPTR) "Unlinked", NULL };
static const U64MountMode mode_values[]
    = { U64_MOUNT_RW, U64_MOUNT_RO, U64_MOUNT_UL };

/* Menu structure */
struct NewMenu MainMenu[]
    = { { NM_TITLE, (STRPTR) "Project", NULL, 0, 0, NULL },
        { NM_ITEM, (STRPTR) "About...", (STRPTR) "?", 0, 0, (APTR)ID_ABOUT },
        { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
        { NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)ID_QUIT },

        { NM_TITLE, (STRPTR) "Settings", NULL, 0, 0, NULL },
        { NM_ITEM, (STRPTR) "Configure...", (STRPTR) "C", 0, 0,
          (APTR)ID_CONFIG_OPEN },
        { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
        { NM_ITEM, (STRPTR) "Clear Settings", NULL, 0, 0,
          (APTR)ID_CONFIG_CLEAR },

        { NM_END, NULL, NULL, 0, 0, NULL } };

/* Environment variable functions */
static STRPTR
ReadEnvVar (CONST_STRPTR var_name)
{
  LONG result;
  STRPTR buffer;
  ULONG buffer_size = 256;

  buffer = AllocMem (buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
  if (!buffer)
    {
      return NULL;
    }

  result = GetVar ((STRPTR)var_name, buffer, buffer_size, GVF_GLOBAL_ONLY);

  if (result > 0)
    {
      STRPTR final_buffer = AllocMem (result + 1, MEMF_PUBLIC);
      if (final_buffer)
        {
          strcpy (final_buffer, buffer);
          FreeMem (buffer, buffer_size);
          return final_buffer;
        }
    }

  FreeMem (buffer, buffer_size);
  return NULL;
}

static BOOL
WriteEnvVar (CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent)
{
  LONG result;
  ULONG flags = GVF_GLOBAL_ONLY;

  if (!var_name || !value)
    {
      return FALSE;
    }

  result = SetVar ((STRPTR)var_name, (STRPTR)value, strlen (value), flags);

  if (result && persistent)
    {
      BPTR file;
      STRPTR envarc_path;
      ULONG path_len = strlen ("ENVARC:") + strlen (var_name) + 1;

      envarc_path = AllocMem (path_len, MEMF_PUBLIC);
      if (envarc_path)
        {
          strcpy (envarc_path, "ENVARC:");
          strcat (envarc_path, var_name);

          STRPTR dir_end = strrchr (envarc_path, '/');
          if (dir_end)
            {
              *dir_end = '\0';
              CreateDir (envarc_path);
              *dir_end = '/';
            }

          file = Open (envarc_path, MODE_NEWFILE);
          if (file)
            {
              Write (file, (STRPTR)value, strlen (value));
              Close (file);
            }

          FreeMem (envarc_path, path_len);
        }
    }

  return result ? TRUE : FALSE;
}

/* Update status text and optionally add to output */
static void
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

/* Load configuration from environment */
static void
LoadConfig (struct AppData *data)
{
  STRPTR env_host, env_password, env_port;

  env_host = ReadEnvVar (ENV_ULTIMATE64_HOST);
  if (env_host)
    {
      strcpy (data->host, env_host);
      FreeMem (env_host, strlen (env_host) + 1);
    }
  else
    {
      strcpy (data->host, "192.168.1.64");
    }

  env_password = ReadEnvVar (ENV_ULTIMATE64_PASSWORD);
  if (env_password)
    {
      strcpy (data->password, env_password);
      FreeMem (env_password, strlen (env_password) + 1);
    }
  else
    {
      data->password[0] = '\0';
    }

  env_port = ReadEnvVar (ENV_ULTIMATE64_PORT);
  if (env_port)
    {
      strcpy (data->port, env_port);
      FreeMem (env_port, strlen (env_port) + 1);
    }
  else
    {
      strcpy (data->port, "80");
    }
}

/* Save configuration to environment */
static void
SaveConfig (struct AppData *data)
{
  WriteEnvVar (ENV_ULTIMATE64_HOST, (CONST_STRPTR)data->host, TRUE);
  if (strlen (data->password) > 0)
    {
      WriteEnvVar (ENV_ULTIMATE64_PASSWORD, (CONST_STRPTR)data->password,
                   TRUE);
    }
  else
    {
      DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
      DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);
    }
  WriteEnvVar (ENV_ULTIMATE64_PORT, (CONST_STRPTR)data->port, TRUE);

  UpdateStatus (data, (CONST_STRPTR) "Configuration saved", TRUE);
}

/* Clear all configuration */
static void
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

/* Create configuration window */
static Object *
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

/* Open configuration window */
static void
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
static void
CloseConfigWindow (struct AppData *data)
{
  if (data->config_window)
    {
      set (data->config_window, MUIA_Window_Open, FALSE);
    }
}

/* Apply configuration changes */
static void
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

/* Create menu bar */
static Object *
CreateMenuBar (struct AppData *data)
{
  Object *menu;

  menu = MUI_MakeObject (MUIO_MenustripNM, MainMenu, 0);

  if (menu)
    {
      data->menu_strip = menu;
    }

  return menu;
}

/* Show about dialog */
static void
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

/* Load file into memory */
static UBYTE *
LoadFile (CONST_STRPTR filename, ULONG *size)
{
  BPTR file;
  LONG file_size;
  UBYTE *buffer;

  file = Open (filename, MODE_OLDFILE);
  if (!file)
    {
      return NULL;
    }

  Seek (file, 0, OFFSET_END);
  file_size = Seek (file, 0, OFFSET_BEGINNING);

  if (file_size <= 0)
    {
      Close (file);
      return NULL;
    }

  buffer = AllocMem (file_size, MEMF_PUBLIC);
  if (!buffer)
    {
      Close (file);
      return NULL;
    }

  if (Read (file, buffer, file_size) != file_size)
    {
      FreeMem (buffer, file_size);
      Close (file);
      return NULL;
    }

  Close (file);

  if (size)
    *size = file_size;
  return buffer;
}

/* Parse address string (hex or decimal) */
static UWORD
ParseAddress (CONST_STRPTR addr_str)
{
  if (!addr_str)
    return 0;

  if (strncmp (addr_str, "0x", 2) == 0 || strncmp (addr_str, "0X", 2) == 0)
    {
      return (UWORD)strtol (addr_str, NULL, 16);
    }
  else if (addr_str[0] == '$')
    {
      return (UWORD)strtol (addr_str + 1, NULL, 16);
    }
  else
    {
      return (UWORD)strtol (addr_str, NULL, 10);
    }
}

/* Parse value string (hex or decimal) */
static UBYTE
ParseValue (CONST_STRPTR val_str)
{
  if (!val_str)
    return 0;

  if (strncmp (val_str, "0x", 2) == 0 || strncmp (val_str, "0X", 2) == 0)
    {
      return (UBYTE)strtol (val_str, NULL, 16);
    }
  else if (val_str[0] == '$')
    {
      return (UBYTE)strtol (val_str + 1, NULL, 16);
    }
  else
    {
      return (UBYTE)strtol (val_str, NULL, 10);
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

  /* Allocate output buffer */
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

/* Update disk status display */
static void
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

/* Connect to Ultimate device */
static void
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

  /* Set verbose mode */
  get (data->chk_verbose, MUIA_Selected, &data->verbose_mode);
  U64_SetVerboseMode (data->verbose_mode);

  /* Connect */
  data->connection = U64_Connect (
      (CONST_STRPTR)data->host,
      (strlen (data->password) > 0) ? (CONST_STRPTR)data->password : NULL);
  if (data->connection)
    {
      U64DeviceInfo info;

      sprintf (status, "Connected to %s", data->host);
      UpdateStatus (data, (CONST_STRPTR)status, TRUE);

      /* Try to get device info */
      if (U64_GetDeviceInfo (data->connection, &info) == U64_OK)
        {
          sprintf (status, "Device: %s (firmware %s)",
                   info.product_name ? (char *)info.product_name : "Ultimate",
                   info.firmware_version ? (char *)info.firmware_version
                                         : "unknown");
          UpdateStatus (data, (CONST_STRPTR)status, TRUE);
          U64_FreeDeviceInfo (&info);
        }

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
static void
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
static void
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

static void
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

static void
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

static void
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

static void
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

static void
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
static void
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

      file_data = LoadFile ((CONST_STRPTR)filename, &file_size);
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
          FreeMem (file_data, file_size);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

static void
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

      file_data = LoadFile ((CONST_STRPTR)filename, &file_size);
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
          FreeMem (file_data, file_size);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

static void
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

      file_data = LoadFile ((CONST_STRPTR)filename, &file_size);
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
          FreeMem (file_data, file_size);
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
static void
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
static void
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

static void
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
static void
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
static void
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
static void
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
static void
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

      file_data = LoadFile ((CONST_STRPTR)filename, &file_size);
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
          FreeMem (file_data, file_size);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read SID file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

static void
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

      file_data = LoadFile ((CONST_STRPTR)filename, &file_size);
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
          FreeMem (file_data, file_size);
        }
      else
        {
          UpdateStatus (data, (CONST_STRPTR) "Failed to read MOD file", TRUE);
        }
    }

  if (req)
    FreeAslRequest (req);
}

/* Main function */
int
main (int argc, char *argv[])
{
  struct AppData data;
  BOOL running = TRUE;
  ULONG signals;
  int retval = 0;

  /* Suppress unused parameter warnings */
  (void)argc;
  (void)argv;

  /* Clear data structure */
  memset (&data, 0, sizeof (data));

  /* Open MUI master library */
  MUIMasterBase = OpenLibrary (MUIMASTER_NAME, 0);
  if (!MUIMasterBase)
    {
      printf ("Failed to open muimaster.library v%d\n", MUIMASTER_VMIN);
      return 20;
    }

  /* Initialize Ultimate64 library */
  if (!U64_InitLibrary ())
    {
      printf ("Failed to initialize Ultimate64 library\n");
      CloseLibrary (MUIMasterBase);
      return 20;
    }

  /* Load configuration */
  LoadConfig (&data);

  /* Create MUI application */
  data.app = ApplicationObject, MUIA_Application_Title,
  (CONST_STRPTR) "Ultimate64 Control", MUIA_Application_Version,
  (CONST_STRPTR)version, MUIA_Application_Copyright, (CONST_STRPTR) "2025",
  MUIA_Application_Author, (CONST_STRPTR) "Marcin Spoczynski",
  MUIA_Application_Description, (CONST_STRPTR) "Ultimate64/II control",
  MUIA_Application_Base, (CONST_STRPTR) "ULTIMATE64",
  MUIA_Application_Menustrip, CreateMenuBar (&data),

  SubWindow, data.window = WindowObject, MUIA_Window_Title,
  (CONST_STRPTR) "Ultimate64 Control", MUIA_Window_ID,
  MAKE_ID ('U', '6', '4', 'W'), MUIA_Window_Width, 500, MUIA_Window_Height,
  200,

  WindowContents, VGroup,
  /* Connection bar */
      Child, HGroup, MUIA_Frame, MUIV_Frame_Group, Child,
  data.btn_connect = SimpleButton ("Connect"), Child, HGroup, Child,
  Label ("Verbose:"), Child,
  data.chk_verbose = MUI_NewObject (MUIC_Image, MUIA_Image_Spec,
                                    MUII_CheckMark, MUIA_InputMode,
                                    MUIV_InputMode_Toggle, MUIA_Image_FreeVert,
                                    TRUE, MUIA_Selected, FALSE, TAG_DONE),
  End, Child, HGroup, Child, Label ("Host:"), Child, TextObject,
  MUIA_Text_Contents, (CONST_STRPTR)data.host, MUIA_Frame, MUIV_Frame_Text,
  MUIA_UserData, MAKE_ID ('H', 'O', 'S', 'T'), End, End, End,

  /* Tabbed interface */
      Child, data.register_tabs = RegisterObject, MUIA_Register_Titles,
  tab_titles,

  /* Disks & Carts tab */
      Child, VGroup, Child, GroupObject, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, (CONST_STRPTR) "Disk Operations", Child, HGroup, Child,
  data.cyc_drive = CycleObject, MUIA_Cycle_Entries, drive_options, End, Child,
  data.cyc_mode = CycleObject, MUIA_Cycle_Entries, mode_options, End, End,
  Child, HGroup, Child, data.btn_mount = SimpleButton ("Mount"), Child,
  data.btn_unmount = SimpleButton ("Unmount"), Child,
  data.btn_drives_status = SimpleButton ("Status"), End, End,

  Child, GroupObject, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle,
  (CONST_STRPTR) "Programs & Cartridges", Child, HGroup, Child,
  data.btn_load_prg = SimpleButton ("Load PRG"), Child,
  data.btn_run_prg = SimpleButton ("Run PRG"), Child,
  data.btn_run_crt = SimpleButton ("Run CRT"), End, End,

  /* Disk status display area */
      Child, GroupObject, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle,
  (CONST_STRPTR) "Drive Status", MUIA_Weight, 100, Child,
  data.txt_disk_status = TextObject, MUIA_Frame, MUIV_Frame_Text,
  MUIA_Text_Contents, (CONST_STRPTR) "Not connected", MUIA_Text_SetVMax,
  FALSE,                           // Don't force minimum size
      MUIA_FixHeightTxt, "\n\n\n", // Limit to ~5 lines height
      MUIA_Background, MUII_TextBack, End, End,

  Child, VSpace (0), End,

  /* Memory tab */
      Child, VGroup, Child, GroupObject, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, (CONST_STRPTR) "Memory Operations",

  Child, HGroup, Child, Label ("Peek Addr:"), Child,
  data.str_peek_addr = StringObject, MUIA_String_MaxLen, 16, End, Child,
  data.btn_peek = SimpleButton ("Peek"), End,

  Child, HGroup, Child, Label ("Poke Addr:"), Child,
  data.str_poke_addr = StringObject, MUIA_String_MaxLen, 16, End, Child,
  Label ("Value:"), Child, data.str_poke_value = StringObject,
  MUIA_String_MaxLen, 8, End, Child, data.btn_poke = SimpleButton ("Poke"),
  End,

  Child, data.txt_memory_result = TextObject, MUIA_Frame, MUIV_Frame_Text,
  MUIA_Text_Contents, (CONST_STRPTR) "Memory result", End, End,

  Child, VSpace (0), End,

  /* Machine tab */
      Child, VGroup, Child, GroupObject, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, (CONST_STRPTR) "Machine Control",

  Child, HGroup, Child, data.btn_reset = SimpleButton ("Reset"), Child,
  data.btn_reboot = SimpleButton ("Reboot"), Child,
  data.btn_poweroff = SimpleButton ("Power Off"), End,

  Child, HGroup, Child, data.btn_pause = SimpleButton ("Pause"), Child,
  data.btn_resume = SimpleButton ("Resume"), Child,
  data.btn_menu = SimpleButton ("Menu"), End, End,

  Child, VSpace (0), End,

  /* Music tab */
      Child, VGroup, Child, GroupObject, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, (CONST_STRPTR) "Music Playback",

  Child, HGroup, Child, Label ("Song:"), Child,
  data.str_song_num = StringObject, MUIA_String_Contents, (CONST_STRPTR) "0",
  MUIA_String_MaxLen, 4, MUIA_String_Accept, (CONST_STRPTR) "0123456789", End,
  Child, HSpace (0), End,

  Child, HGroup, Child, data.btn_play_sid = SimpleButton ("Play SID"), Child,
  data.btn_play_mod = SimpleButton ("Play MOD"), End, End,

  Child, VSpace (0), End, End,

  /* Always visible - Text input */
      Child, GroupObject, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle,
  (CONST_STRPTR) "Keyboard Input", Child, HGroup, Child,
  data.str_type = StringObject, MUIA_String_MaxLen, 256, End, Child,
  data.btn_type = SimpleButton ("Type"), End, End,

  /* Status */
      Child, data.txt_status = TextObject, MUIA_Frame, MUIV_Frame_Text,
  MUIA_Text_Contents, (CONST_STRPTR) "Not connected", End,

  /* Output area */
      End, End, End;

  if (!data.app)
    {
      printf ("Failed to create MUI application\n");
      U64_CleanupLibrary ();
      CloseLibrary (MUIMasterBase);
      return 20;
    }

  /* Initially disable controls */
  set (data.btn_reset, MUIA_Disabled, TRUE);
  set (data.btn_reboot, MUIA_Disabled, TRUE);
  set (data.btn_poweroff, MUIA_Disabled, TRUE);
  set (data.btn_pause, MUIA_Disabled, TRUE);
  set (data.btn_resume, MUIA_Disabled, TRUE);
  set (data.btn_menu, MUIA_Disabled, TRUE);
  set (data.btn_load_prg, MUIA_Disabled, TRUE);
  set (data.btn_run_prg, MUIA_Disabled, TRUE);
  set (data.btn_run_crt, MUIA_Disabled, TRUE);
  set (data.btn_type, MUIA_Disabled, TRUE);
  set (data.btn_mount, MUIA_Disabled, TRUE);
  set (data.btn_unmount, MUIA_Disabled, TRUE);
  set (data.btn_peek, MUIA_Disabled, TRUE);
  set (data.btn_poke, MUIA_Disabled, TRUE);
  set (data.btn_play_sid, MUIA_Disabled, TRUE);
  set (data.btn_play_mod, MUIA_Disabled, TRUE);
  set (data.btn_drives_status, MUIA_Disabled, TRUE);

  /* Setup notifications */
  DoMethod (data.window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, data.app,
            2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

  DoMethod (data.btn_connect, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_CONNECT);

  /* Machine control notifications */
  DoMethod (data.btn_reset, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_RESET);
  DoMethod (data.btn_reboot, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_REBOOT);
  DoMethod (data.btn_poweroff, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_POWEROFF);
  DoMethod (data.btn_pause, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PAUSE);
  DoMethod (data.btn_resume, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_RESUME);
  DoMethod (data.btn_menu, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_MENU);

  /* File operation notifications */
  DoMethod (data.btn_load_prg, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_LOAD_PRG);
  DoMethod (data.btn_run_prg, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_RUN_PRG);
  DoMethod (data.btn_run_crt, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_RUN_CRT);

  /* Text and memory notifications */
  DoMethod (data.btn_type, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_TYPE);
  DoMethod (data.btn_peek, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PEEK);
  DoMethod (data.btn_poke, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_POKE);

  /* Drive operation notifications */
  DoMethod (data.btn_mount, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_MOUNT);
  DoMethod (data.btn_unmount, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_UNMOUNT);
  DoMethod (data.btn_drives_status, MUIM_Notify, MUIA_Pressed, FALSE, data.app,
            2, MUIM_Application_ReturnID, ID_DRIVES_STATUS);

  /* Music operation notifications */
  DoMethod (data.btn_play_sid, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PLAY_SID);
  DoMethod (data.btn_play_mod, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PLAY_MOD);

  /* Open window */
  set (data.window, MUIA_Window_Open, TRUE);

  /* Main event loop */
  while (running)
    {
      ULONG id = DoMethod (data.app, MUIM_Application_Input, &signals);

      switch (id)
        {
        case MUIV_Application_ReturnID_Quit:
        case ID_QUIT:
          running = FALSE;
          break;

        case ID_ABOUT:
          ShowAboutDialog (&data);
          break;

        case ID_CONFIG_OPEN:
          OpenConfigWindow (&data);
          break;

        case ID_CONFIG_OK:
          ApplyConfigChanges (&data);
          break;

        case ID_CONFIG_CANCEL:
          CloseConfigWindow (&data);
          break;

        case ID_CONFIG_CLEAR:
          ClearConfig (&data);
          break;

        case ID_CONNECT:
          if (data.connection)
            {
              DoDisconnect (&data);
            }
          else
            {
              DoConnect (&data);
            }
          break;

        /* Machine control */
        case ID_RESET:
          DoReset (&data);
          break;
        case ID_REBOOT:
          DoReboot (&data);
          break;
        case ID_POWEROFF:
          DoPowerOff (&data);
          break;
        case ID_PAUSE:
          DoPause (&data);
          break;
        case ID_RESUME:
          DoResume (&data);
          break;
        case ID_MENU:
          DoMenu (&data);
          break;

        /* File operations */
        case ID_LOAD_PRG:
          DoLoadPRG (&data);
          break;
        case ID_RUN_PRG:
          DoRunPRG (&data);
          break;
        case ID_RUN_CRT:
          DoRunCRT (&data);
          break;

        /* Text and memory */
        case ID_TYPE:
          DoType (&data);
          break;
        case ID_PEEK:
          DoPeek (&data);
          break;
        case ID_POKE:
          DoPoke (&data);
          break;

        /* Drive operations */
        case ID_MOUNT:
          DoMount (&data);
          break;
        case ID_UNMOUNT:
          DoUnmount (&data);
          break;
        case ID_DRIVES_STATUS:
          DoDrivesStatus (&data);
          break;

        /* Music operations */
        case ID_PLAY_SID:
          DoPlaySID (&data);
          break;
        case ID_PLAY_MOD:
          DoPlayMOD (&data);
          break;
        }

      if (running && signals)
        {
          Wait (signals);
        }
    }

  /* Cleanup */
  if (data.connection)
    {
      U64_Disconnect (data.connection);
    }

  /* Close configuration window if open */
  if (data.config_window)
    {
      set (data.config_window, MUIA_Window_Open, FALSE);
    }

  set (data.window, MUIA_Window_Open, FALSE);
  MUI_DisposeObject (data.app);

  U64_CleanupLibrary ();
  CloseLibrary (MUIMasterBase);

  return retval;
}