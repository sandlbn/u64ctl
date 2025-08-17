/* Example MUI application using Ultimate64 library
 * Shows how to integrate with MUI for Amiga OS 3.x
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <libraries/asl.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/asl.h>

#include <stdio.h>
#include <string.h>

#include "ultimate64_amiga.h"

/* MUI Object creation macros - only define if not already defined */
#ifndef ApplicationObject
#define ApplicationObject   MUI_NewObject(MUIC_Application
#endif
#ifndef WindowObject
#define WindowObject        MUI_NewObject(MUIC_Window
#endif
#ifndef VGroup
#define VGroup              MUI_NewObject(MUIC_Group
#endif
#ifndef GroupObject
#define GroupObject         MUI_NewObject(MUIC_Group
#endif
#ifndef StringObject
#define StringObject        MUI_NewObject(MUIC_String
#endif
#ifndef TextObject
#define TextObject          MUI_NewObject(MUIC_Text
#endif
#ifndef CycleObject
#define CycleObject         MUI_NewObject(MUIC_Cycle
#endif

/* Define IPTR if not available */
#ifndef IPTR
#define IPTR ULONG
#endif

/* MAKE_ID macro if not defined */
#ifndef MAKE_ID
#define MAKE_ID(a,b,c,d) \
  ((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif

/* MUI IDs */
enum {
    ID_CONNECT = 1,
    ID_DISCONNECT,
    ID_RESET,
    ID_LOAD,
    ID_RUN,
    ID_TYPE,
    ID_MOUNT,
    ID_QUIT,
    ID_ABOUT
};

/* Application data structure */
struct AppData {
    Object *app;
    Object *window;
    Object *str_host;
    Object *str_password;
    Object *txt_status;
    Object *btn_connect;
    Object *btn_reset;
    Object *btn_load;
    Object *btn_type;
    Object *str_type;
    Object *btn_mount;
    Object *cyc_drive;
    Object *cyc_mode;
    U64Connection *connection;
};

/* Version string */
static const char version[] = "$VER: Ultimate64_MUI 1.0 (2024)";

/* Library bases */
struct Library *MUIMasterBase = NULL;

/* Drive options */
static const char *drive_options[] = { (CONST_STRPTR)"Drive A", (CONST_STRPTR)"Drive B", (CONST_STRPTR)"Drive C", (CONST_STRPTR)"Drive D", NULL };
static const char *drive_ids[] = { "a", "b", "c", "d" };

/* Mount mode options */
static const char *mode_options[] = { (CONST_STRPTR)"Read/Write", (CONST_STRPTR)"Read Only", (CONST_STRPTR)"Unlinked", NULL };
static const U64MountMode mode_values[] = { U64_MOUNT_RW, U64_MOUNT_RO, U64_MOUNT_UL };

/* Update status text */
static void UpdateStatus(struct AppData *data, CONST_STRPTR text)
{
    set(data->txt_status, MUIA_Text_Contents, text);
}

/* Connect to Ultimate device */
static void DoConnect(struct AppData *data)
{
    STRPTR host, password;
    char status[256];
    
    get(data->str_host, MUIA_String_Contents, &host);
    get(data->str_password, MUIA_String_Contents, &password);
    
    if (!host || strlen((char *)host) == 0) {
        UpdateStatus(data, (CONST_STRPTR)"Please enter a host address");
        return;
    }
    
    /* Disconnect if already connected */
    if (data->connection) {
        U64_Disconnect(data->connection);
        data->connection = NULL;
    }
    
    /* Connect */
    data->connection = U64_Connect((CONST_STRPTR)host, (CONST_STRPTR)password);
    if (data->connection) {
        U64DeviceInfo info;
        
        sprintf(status, "Connected to %s", (char *)host);
        UpdateStatus(data, (CONST_STRPTR)status);
        
        /* Try to get device info */
        if (U64_GetDeviceInfo(data->connection, &info) == U64_OK) {
            sprintf(status, "Connected to %s (firmware %s)", 
                    info.product_name ? (char *)info.product_name : "Ultimate",
                    info.firmware_version ? (char *)info.firmware_version : "unknown");
            UpdateStatus(data, (CONST_STRPTR)status);
            U64_FreeDeviceInfo(&info);
        }
        
        /* Enable controls */
        set(data->btn_reset, MUIA_Disabled, FALSE);
        set(data->btn_load, MUIA_Disabled, FALSE);
        set(data->btn_type, MUIA_Disabled, FALSE);
        set(data->btn_mount, MUIA_Disabled, FALSE);
        set(data->btn_connect, MUIA_Text_Contents, (CONST_STRPTR)"Disconnect");
    } else {
        UpdateStatus(data, (CONST_STRPTR)"Connection failed");
    }
}

/* Disconnect from Ultimate device */
static void DoDisconnect(struct AppData *data)
{
    if (data->connection) {
        U64_Disconnect(data->connection);
        data->connection = NULL;
        UpdateStatus(data, (CONST_STRPTR)"Disconnected");
        
        /* Disable controls */
        set(data->btn_reset, MUIA_Disabled, TRUE);
        set(data->btn_load, MUIA_Disabled, TRUE);
        set(data->btn_type, MUIA_Disabled, TRUE);
        set(data->btn_mount, MUIA_Disabled, TRUE);
        set(data->btn_connect, MUIA_Text_Contents, (CONST_STRPTR)"Connect");
    }
}

/* Reset machine */
static void DoReset(struct AppData *data)
{
    if (data->connection) {
        if (U64_Reset(data->connection) == U64_OK) {
            UpdateStatus(data, (CONST_STRPTR)"Machine reset");
        } else {
            UpdateStatus(data, (CONST_STRPTR)"Reset failed");
        }
    }
}

/* Load PRG file */
static void DoLoad(struct AppData *data)
{
    struct FileRequester *req;
    char filename[256];
    
    if (!data->connection) {
        return;
    }
    
    /* Open file requester */
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (CONST_STRPTR)"Select PRG file to load",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (CONST_STRPTR)"#?.prg",
        TAG_DONE);
    
    if (req && AslRequest(req, NULL)) {
        strcpy(filename, req->rf_Dir);
        AddPart((STRPTR)filename, req->rf_File, sizeof(filename));
        
        if (U64_LoadFile(data->connection, (CONST_STRPTR)filename, NULL) == U64_OK) {
            UpdateStatus(data, (CONST_STRPTR)"File loaded");
        } else {
            UpdateStatus(data, (CONST_STRPTR)"Load failed");
        }
    }
    
    if (req) {
        FreeAslRequest(req);
    }
}

/* Type text */
static void DoType(struct AppData *data)
{
    STRPTR text;
    
    if (!data->connection) {
        return;
    }
    
    get(data->str_type, MUIA_String_Contents, &text);
    
    if (text && strlen((char *)text) > 0) {
        if (U64_TypeText(data->connection, (CONST_STRPTR)text) == U64_OK) {
            UpdateStatus(data, (CONST_STRPTR)"Text typed");
            set(data->str_type, MUIA_String_Contents, (CONST_STRPTR)"");
        } else {
            UpdateStatus(data, (CONST_STRPTR)"Type failed");
        }
    }
}

/* Mount disk image */
static void DoMount(struct AppData *data)
{
    struct FileRequester *req;
    char filename[256];
    LONG drive_idx, mode_idx;
    
    if (!data->connection) {
        return;
    }
    
    /* Get selected drive and mode */
    get(data->cyc_drive, MUIA_Cycle_Active, &drive_idx);
    get(data->cyc_mode, MUIA_Cycle_Active, &mode_idx);
    
    /* Open file requester */
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (CONST_STRPTR)"Select disk image",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, (CONST_STRPTR)"#?.(d64|g64|d71|g71|d81)",
        TAG_DONE);
    
    if (req && AslRequest(req, NULL)) {
        strcpy(filename, req->rf_Dir);
        AddPart((STRPTR)filename, req->rf_File, sizeof(filename));
        
        if (U64_MountDisk(data->connection, (CONST_STRPTR)filename, 
                          (CONST_STRPTR)drive_ids[drive_idx], 
                          mode_values[mode_idx], 
                          FALSE) == U64_OK) {
            UpdateStatus(data, (CONST_STRPTR)"Disk mounted");
        } else {
            UpdateStatus(data, (CONST_STRPTR)"Mount failed");
        }
    }
    
    if (req) {
        FreeAslRequest(req);
    }
}

/* Main function */
int main(int argc, char *argv[])
{
    struct AppData data;
    BOOL running = TRUE;
    ULONG signals;
    int retval = 0;
    
    /* Suppress unused parameter warnings */
    (void)argc;
    (void)argv;
    
    /* Clear data structure */
    memset(&data, 0, sizeof(data));
    
    /* Open MUI master library */
    MUIMasterBase = OpenLibrary(MUIMASTER_NAME, 0);
    if (!MUIMasterBase) {
        printf("Failed to open muimaster.library v%d\n", MUIMASTER_VMIN);
        return 20;
    }
    
    /* Initialize Ultimate64 library */
    if (!U64_InitLibrary()) {
        printf("Failed to initialize Ultimate64 library\n");
        CloseLibrary(MUIMasterBase);
        return 20;
    }
    
    /* Create MUI application */
    data.app = ApplicationObject,
        MUIA_Application_Title, (CONST_STRPTR)"Ultimate64 Control",
        MUIA_Application_Version, (CONST_STRPTR)version,
        MUIA_Application_Copyright, (CONST_STRPTR)"© 2024",
        MUIA_Application_Author, (CONST_STRPTR)"Your Name",
        MUIA_Application_Description, (CONST_STRPTR)"Control Ultimate64/II via network",
        MUIA_Application_Base, (CONST_STRPTR)"ULTIMATE64",
        
        SubWindow, data.window = WindowObject,
            MUIA_Window_Title, (CONST_STRPTR)"Ultimate64 Control",
            MUIA_Window_ID, MAKE_ID('U','6','4','W'),
            
            WindowContents, VGroup,
                /* Connection group */
                Child, GroupObject,
                    MUIA_Group_Columns, 2,
                    MUIA_Frame, MUIV_Frame_Group,
                    MUIA_FrameTitle, (CONST_STRPTR)"Connection",
                    
                    Child, Label("Host:"),
                    Child, data.str_host = StringObject,
                        MUIA_String_Contents, (CONST_STRPTR)"192.168.1.64",
                        MUIA_String_MaxLen, 256,
                    End,
                    
                    Child, Label("Password:"),
                    Child, data.str_password = StringObject,
                        MUIA_String_Secret, TRUE,
                        MUIA_String_MaxLen, 256,
                    End,
                End,
                
                /* Connect button */
                Child, data.btn_connect = SimpleButton("Connect"),
                
                /* Control group */
                Child, GroupObject,
                    MUIA_Frame, MUIV_Frame_Group,
                    MUIA_FrameTitle, (CONST_STRPTR)"Controls",
                    
                    Child, HGroup,
                        Child, data.btn_reset = SimpleButton("Reset"),
                        Child, data.btn_load = SimpleButton("Load PRG"),
                    End,
                    
                    Child, HGroup,
                        Child, data.str_type = StringObject,
                            MUIA_String_Contents, (CONST_STRPTR)"",
                            MUIA_String_MaxLen, 256,
                        End,
                        Child, data.btn_type = SimpleButton("Type"),
                    End,
                    
                    Child, HGroup,
                        Child, data.cyc_drive = CycleObject,
                            MUIA_Cycle_Entries, drive_options,
                        End,
                        Child, data.cyc_mode = CycleObject,
                            MUIA_Cycle_Entries, mode_options,
                        End,
                        Child, data.btn_mount = SimpleButton("Mount"),
                    End,
                End,
                
                /* Status */
                Child, data.txt_status = TextObject,
                    MUIA_Frame, MUIV_Frame_Text,
                    MUIA_Text_Contents, (CONST_STRPTR)"Not connected",
                End,
            End,
        End,
    End;
    
    if (!data.app) {
        printf("Failed to create MUI application\n");
        U64_CleanupLibrary();
        CloseLibrary(MUIMasterBase);
        return 20;
    }
    
    /* Initially disable controls */
    set(data.btn_reset, MUIA_Disabled, TRUE);
    set(data.btn_load, MUIA_Disabled, TRUE);
    set(data.btn_type, MUIA_Disabled, TRUE);
    set(data.btn_mount, MUIA_Disabled, TRUE);
    
    /* Setup notifications */
    DoMethod(data.window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             data.app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    
    DoMethod(data.btn_connect, MUIM_Notify, MUIA_Pressed, FALSE,
             data.app, 2, MUIM_Application_ReturnID, ID_CONNECT);
    
    DoMethod(data.btn_reset, MUIM_Notify, MUIA_Pressed, FALSE,
             data.app, 2, MUIM_Application_ReturnID, ID_RESET);
    
    DoMethod(data.btn_load, MUIM_Notify, MUIA_Pressed, FALSE,
             data.app, 2, MUIM_Application_ReturnID, ID_LOAD);
    
    DoMethod(data.btn_type, MUIM_Notify, MUIA_Pressed, FALSE,
             data.app, 2, MUIM_Application_ReturnID, ID_TYPE);
    
    DoMethod(data.btn_mount, MUIM_Notify, MUIA_Pressed, FALSE,
             data.app, 2, MUIM_Application_ReturnID, ID_MOUNT);
    
    /* Open window */
    set(data.window, MUIA_Window_Open, TRUE);
    
    /* Main event loop */
    while (running) {
        ULONG id = DoMethod(data.app, MUIM_Application_Input, &signals);
        
        switch (id) {
            case MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;
                
            case ID_CONNECT:
                if (data.connection) {
                    DoDisconnect(&data);
                } else {
                    DoConnect(&data);
                }
                break;
                
            case ID_RESET:
                DoReset(&data);
                break;
                
            case ID_LOAD:
                DoLoad(&data);
                break;
                
            case ID_TYPE:
                DoType(&data);
                break;
                
            case ID_MOUNT:
                DoMount(&data);
                break;
        }
        
        if (running && signals) {
            Wait(signals);
        }
    }
    
    /* Cleanup */
    if (data.connection) {
        U64_Disconnect(data.connection);
    }
    
    set(data.window, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(data.app);
    
    U64_CleanupLibrary();
    CloseLibrary(MUIMasterBase);
    
    return retval;
}