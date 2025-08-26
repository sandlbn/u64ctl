#ifndef ULTIMATE64_MUI_MAIN_H
#define ULTIMATE64_MUI_MAIN_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include "ultimate64_amiga.h"

#define HOOKFUNC LONG (*)(struct Hook *, APTR, APTR)
#define ENV_PATH "ENVARC:Ultimate64/"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) \
  ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#ifdef DEBUG_BUILD
    #define DEBUG(msg, ...) printf("DEBUG [%s:%d]: " msg "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
    #define DEBUG(msg, ...) ((void)0)
#endif

// MUI helper macros
#define MakeMenuBar() MenuitemObject, MUIA_Menuitem_Title, "", End

#define MakeMenuItem(title, shortcut) \
  MenuitemObject, MUIA_Menuitem_Title, title, MUIA_Menuitem_Shortcut, \
      shortcut, End

// Application defines
#define APP_NAME "Ultimate64 Control"
#define APP_DATE "20.08.2025"
#define APP_VERSION "1.0"
#define APP_VERSTRING "$VER: " APP_NAME " " APP_VERSION " (" APP_DATE ")"
#define APP_AUTHORS "Coding: Marcin Spoczynski"
#define APP_COPYRIGHT "Free to use and distribute"
#define APP_DESCRIPTION "MUI Interface for Ultimate64/II"
#define MAX_STATUS_MSG_LEN 256

// Window IDs
#define APP_ID_WIN_MAIN MAKE_ID('U', '6', '4', '0')
#define APP_ID_WIN_CONFIG MAKE_ID('U', '6', '4', '1')
#define APP_ID_WIN_SETTINGS_MUI MAKE_ID('U', '6', '4', '2')

// Environment variable names
#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_PORT "Ultimate64/Port"

// Configuration defaults
#define CONFIG_HOST_DEFAULT "192.168.1.64"
#define CONFIG_PORT_DEFAULT "80"
#define CONFIG_PASSWORD_DEFAULT ""
#define CONFIG_HOST_MAX_LEN 255
#define CONFIG_PORT_MAX_LEN 7
#define CONFIG_PASSWORD_MAX_LEN 255

// Event IDs
enum EVENT_IDS {
  EVENT_ABOUT = 100,
  EVENT_ABOUT_MUI,
  EVENT_CONNECT,
  EVENT_DISCONNECT,
  EVENT_ICONIFY,
  EVENT_QUIT,
  EVENT_CONFIG,
  EVENT_CONFIG_MUI,
  EVENT_CONFIG_SAVE,
  EVENT_CONFIG_CANCEL,
  EVENT_CONFIG_CLEAR,
  EVENT_RESET,
  EVENT_REBOOT,
  EVENT_POWEROFF,
  EVENT_PAUSE,
  EVENT_RESUME,
  EVENT_MENU,
  EVENT_LOAD_PRG,
  EVENT_RUN_PRG,
  EVENT_RUN_CRT,
  EVENT_TYPE,
  EVENT_MOUNT,
  EVENT_UNMOUNT,
  EVENT_DRIVES_STATUS,
  EVENT_PEEK,
  EVENT_POKE,
  EVENT_PLAY_SID,
  EVENT_PLAY_MOD
};

// Configuration structure
struct U64Config {
    char host[CONFIG_HOST_MAX_LEN + 1];
    char password[CONFIG_PASSWORD_MAX_LEN + 1];
    char port[CONFIG_PORT_MAX_LEN + 1];
};

// Main application object structure
struct ObjApp {
  APTR App;

  // Windows
  APTR WIN_About;
  APTR WIN_Main;
  APTR WIN_Config;

  // Menu objects
  APTR MN_Main;
  APTR MN_Project_Connect;
  APTR MN_Project_About;
  APTR MN_Project_About_MUI;
  APTR MN_Project_Config;
  APTR MN_Project_Config_MUI;
  APTR MN_Project_Iconify;
  APTR MN_Project_Quit;
  APTR MN_Machine_Reset;
  APTR MN_Machine_Reboot;
  APTR MN_Machine_PowerOff;
  APTR MN_Machine_Pause;
  APTR MN_Machine_Resume;
  APTR MN_Machine_Menu;
  APTR MN_File_Load_PRG;
  APTR MN_File_Run_PRG;
  APTR MN_File_Run_CRT;

  // Main window controls
  APTR BTN_Connect;
  APTR CHK_Verbose;
  APTR TXT_Host_Display;
  APTR TXT_Status;
  APTR REG_Tabs;

  // Disks & Carts tab
  APTR CYC_Drive;
  APTR CYC_Mode;
  APTR BTN_Mount;
  APTR BTN_Unmount;
  APTR BTN_Drives_Status;
  APTR BTN_Load_PRG;
  APTR BTN_Run_PRG;
  APTR BTN_Run_CRT;
  APTR TXT_Disk_Status;

  // Memory tab
  APTR STR_Peek_Addr;
  APTR BTN_Peek;
  APTR STR_Poke_Addr;
  APTR STR_Poke_Value;
  APTR BTN_Poke;
  APTR TXT_Memory_Result;

  // Machine tab
  APTR BTN_Reset;
  APTR BTN_Reboot;
  APTR BTN_PowerOff;
  APTR BTN_Pause;
  APTR BTN_Resume;
  APTR BTN_Menu;

  // Music tab
  APTR STR_Song_Num;
  APTR BTN_Play_SID;
  APTR BTN_Play_MOD;

  // Always visible controls
  APTR STR_Type;
  APTR BTN_Type;
  APTR TXT_Output;

  // Configuration window controls
  APTR STR_Config_Host;
  APTR STR_Config_Password;
  APTR STR_Config_Port;
  APTR BTN_Config_Save;
  APTR BTN_Config_Cancel;

  // Application state
  struct U64Config config;
  U64Connection *connection;
  BOOL verbose_mode;
};

// Function prototypes
extern struct ObjApp *CreateApp(void);
extern void DisposeApp(struct ObjApp *);
extern BOOL LoadConfig(struct U64Config *config);
extern BOOL SaveConfig(const struct U64Config *config);
extern void ClearConfig(struct U64Config *config);

// Application function prototypes
extern BOOL APP_Init(void);
extern BOOL APP_About(void);
extern BOOL APP_About_MUI(void);
extern BOOL APP_Iconify(void);
extern BOOL APP_Config(void);
extern BOOL APP_Config_Save(void);
extern BOOL APP_Config_Cancel(void);
extern BOOL APP_Connect(void);
extern BOOL APP_Reset(void);
extern BOOL APP_Reboot(void);
extern BOOL APP_PowerOff(void);
extern BOOL APP_Pause(void);
extern BOOL APP_Resume(void);
extern BOOL APP_Menu(void);
extern BOOL APP_Load_PRG(void);
extern BOOL APP_Run_PRG(void);
extern BOOL APP_Run_CRT(void);
extern BOOL APP_Type(void);
extern BOOL APP_Mount(void);
extern BOOL APP_Unmount(void);
extern BOOL APP_Drives_Status(void);
extern BOOL APP_Peek(void);
extern BOOL APP_Poke(void);
extern BOOL APP_Play_SID(void);
extern BOOL APP_Play_MOD(void);

#endif  // ULTIMATE64_MUI_MAIN_H