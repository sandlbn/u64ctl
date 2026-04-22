/* Ultimate64 Control - entry point and module-scope globals
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <dos/dos.h>
#include <dos/var.h>
#include <exec/memory.h>
#include <exec/tasks.h>
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

#include "mui_app.h"

/* Guaranteed stack size (bytes). MUI + our handler stack footprint is around
 * 8 KB; we request 32 KB for headroom. If the caller's stack is already at
 * least this big we run directly, otherwise we StackSwap onto a private
 * buffer. */
#define REQUIRED_STACK 32768

/* StackSwapStruct kept as a module global so the compiler doesn't try to
 * reach it via stack-relative addressing across a StackSwap. */
static struct StackSwapStruct g_sss;
static UBYTE *g_new_stack = NULL;

static int AppMain (int argc, char *argv[]);

/* Version string */
static const char version[] = "$VER: Ultimate64_MUI 0.3.1 (2025)";

/* Library bases */
struct Library *MUIMasterBase = NULL;

/* Tab titles */
static const char *tab_titles[]
    = { (CONST_STRPTR) "Disks & Carts", (CONST_STRPTR) "Memory",
        (CONST_STRPTR) "Machine", (CONST_STRPTR) "Music", NULL };

/* Drive options — the Ultimate64 only exposes two physical internal drives
 * (A at bus 8, B at bus 9). C/D were never real units on the hardware. */
static const char *drive_options[]
    = { (CONST_STRPTR) "Drive A", (CONST_STRPTR) "Drive B", NULL };
const char *drive_ids[] = { "a", "b" };

/* Mount mode options */
static const char *mode_options[]
    = { (CONST_STRPTR) "Read/Write", (CONST_STRPTR) "Read Only",
        (CONST_STRPTR) "Unlinked", NULL };
const U64MountMode mode_values[]
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

/* Main function */
int
main (int argc, char *argv[])
{
  struct Process *proc = (struct Process *)FindTask (NULL);
  ULONG current_stack
      = (ULONG)proc->pr_Task.tc_SPUpper - (ULONG)proc->pr_Task.tc_SPLower;
  int retval;

  /* If caller already gave us enough stack, run in-place. */
  if (current_stack >= REQUIRED_STACK)
    return AppMain (argc, argv);

  /* Otherwise allocate a bigger stack and swap to it for the duration of the
   * program. */
  g_new_stack = AllocMem (REQUIRED_STACK, MEMF_PUBLIC);
  if (!g_new_stack)
    {
      printf ("Out of memory for stack\n");
      return 20;
    }

  g_sss.stk_Lower = g_new_stack;
  g_sss.stk_Upper = (ULONG)(g_new_stack + REQUIRED_STACK);
  g_sss.stk_Pointer = (APTR)(g_new_stack + REQUIRED_STACK);

  StackSwap (&g_sss);
  retval = AppMain (argc, argv);
  StackSwap (&g_sss);

  FreeMem (g_new_stack, REQUIRED_STACK);
  g_new_stack = NULL;
  return retval;
}

static int
AppMain (int argc, char *argv[])
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
                                    TRUE, MUIA_Frame, MUIV_Frame_ImageButton,
                                    MUIA_Background, MUII_ButtonBack,
                                    MUIA_ShowSelState, TRUE,
                                    MUIA_Selected, FALSE, TAG_DONE),
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
      data.connection = NULL;
    }

  Delay (5); /* 100ms — let network settle before tearing down libs */

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
