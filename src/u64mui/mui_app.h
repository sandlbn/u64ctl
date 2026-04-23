/* Ultimate64 Control - MUI app shared header
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#ifndef U64_MUI_APP_H
#define U64_MUI_APP_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>  /* DoMethod, MakeLibrary, etc. */

#include "ultimate64_amiga.h"

/* MUI Object creation macros. libraries/mui.h already supplies HGroup,
 * SimpleButton and Label with different semantics; we override them with the
 * customised forms this app relies on. #undef first to avoid redefine
 * warnings.
 */
#undef HGroup
#undef SimpleButton
#undef Label

#define ApplicationObject   MUI_NewObject(MUIC_Application
#define WindowObject        MUI_NewObject(MUIC_Window
#define VGroup              MUI_NewObject(MUIC_Group
#define HGroup              MUI_NewObject(MUIC_Group, MUIA_Group_Horiz, TRUE
#define GroupObject         MUI_NewObject(MUIC_Group
/* String gadgets default to a visible recessed frame + StringBack fill so
 * they stand out from surrounding text widgets. Callers append their own
 * MUIA_String_* tags and close with End. */
#define StringObject        MUI_NewObject(MUIC_String, \
                                MUIA_Frame, MUIV_Frame_String, \
                                MUIA_Background, MUII_StringBack, \
                                MUIA_CycleChain, TRUE
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
  ID_CONFIG_CLEAR,

  /* Assembly64 tab IDs */
  ID_ASM_SEARCH = 200,
  ID_ASM_PREV,
  ID_ASM_NEXT,
  ID_ASM_SHOW_FILES,
  ID_ASM_RUN,
  ID_ASM_MOUNT,
  ID_ASM_DOWNLOAD
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

  /* Assembly64 tab */
  Object *str_asm_query;
  Object *cyc_asm_category;
  Object *cyc_asm_latest;
  Object *cyc_asm_source;     /* CSDB / HVSC / OneLoad64 / … */
  Object *cyc_asm_rank;       /* Any / ≥7 / ≥8 / ≥9 / Top rated first */
  Object *cyc_asm_drive;      /* A / B target for .d64 mounts */
  Object *btn_asm_search;
  Object *btn_asm_prev;
  Object *btn_asm_next;
  Object *lst_asm_results;
  Object *lst_asm_files;
  Object *btn_asm_show_files;
  Object *btn_asm_run;
  Object *btn_asm_mount;      /* mount-without-run, for picking disk 2/3/… of a multi-disker */
  Object *btn_asm_download;   /* save to local path instead of sending to U64 */
  Object *dtp_asm_preview;    /* MUIC_Dtpic — shows CSDB screenshot if any */
  Object *txt_asm_info;

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

/* Shared globals defined in main.c */
extern struct Library *MUIMasterBase;
extern const char *drive_ids[];
extern const U64MountMode mode_values[];

/* config.c */
void LoadConfig (struct AppData *data);
void SaveConfig (struct AppData *data);
void ClearConfig (struct AppData *data);
void OpenConfigWindow (struct AppData *data);
void CloseConfigWindow (struct AppData *data);
void ApplyConfigChanges (struct AppData *data);

/* ui.c */
void UpdateStatus (struct AppData *data, CONST_STRPTR text, BOOL add_to_output);
Object *CreateConfigWindow (struct AppData *data);
Object *CreateMenuBar (struct AppData *data);
void ShowAboutDialog (struct AppData *data);
void UpdateDiskDisplay (struct AppData *data);

/* utils.c */
UWORD ParseAddress (CONST_STRPTR addr_str);
UBYTE ParseValue (CONST_STRPTR val_str);

/* handlers.c */
void DoConnect (struct AppData *data);
void DoDisconnect (struct AppData *data);
void DoReset (struct AppData *data);
void DoReboot (struct AppData *data);
void DoPowerOff (struct AppData *data);
void DoPause (struct AppData *data);
void DoResume (struct AppData *data);
void DoMenu (struct AppData *data);
void DoLoadPRG (struct AppData *data);
void DoRunPRG (struct AppData *data);
void DoRunCRT (struct AppData *data);
void DoType (struct AppData *data);
void DoMount (struct AppData *data);
void DoUnmount (struct AppData *data);
void DoDrivesStatus (struct AppData *data);
void DoPeek (struct AppData *data);
void DoPoke (struct AppData *data);
void DoPlaySID (struct AppData *data);
void DoPlayMOD (struct AppData *data);

/* Assembly64 tab (assembly_tab.c) */
Object *CreateAssemblyTab     (struct AppData *data);
void    ConnectAssemblyEvents (struct AppData *data);
BOOL    AsmDispatch           (struct AppData *data, ULONG id);
void    AsmKickstart          (struct AppData *data);  /* prefetch latest:1week */
void    DisposeAssemblyState  (void);

#endif /* U64_MUI_APP_H */
