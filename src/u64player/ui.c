/* Ultimate64 SID Player - MUI window/menu construction
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>

#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "player.h"

static const char *search_mode_entries[] = {
    "Search",
    "Filter",
    NULL
};

/* Menu creation */
void CreateMenu(struct ObjApp *obj)
{
    obj->MN_Project_About = MenuitemObject,
        MUIA_Menuitem_Title, "About...",
        MUIA_Menuitem_Shortcut, "?",
    End;

    obj->MN_Project_Config = MenuitemObject,
        MUIA_Menuitem_Title, "Configure...",
        MUIA_Menuitem_Shortcut, "C",
    End;

    obj->MN_Project_Quit = MenuitemObject,
        MUIA_Menuitem_Title, "Quit",
        MUIA_Menuitem_Shortcut, "Q",
    End;

    /* NEW: Playlist menu items */
    obj->MN_Playlist_Load = MenuitemObject,
        MUIA_Menuitem_Title, "Load Playlist...",
        MUIA_Menuitem_Shortcut, "L",
    End;

    obj->MN_Playlist_Save = MenuitemObject,
        MUIA_Menuitem_Title, "Save Playlist",
        MUIA_Menuitem_Shortcut, "S",
    End;

    obj->MN_Playlist_SaveAs = MenuitemObject,
        MUIA_Menuitem_Title, "Save Playlist As...",
        MUIA_Menuitem_Shortcut, "A",
    End;

    Object *menu1 = MenuitemObject,
        MUIA_Menuitem_Title, "Project",
        MUIA_Family_Child, obj->MN_Project_About,
        MUIA_Family_Child, MenuitemObject, MUIA_Menuitem_Title, "", End,
        MUIA_Family_Child, obj->MN_Project_Config,
        MUIA_Family_Child, MenuitemObject, MUIA_Menuitem_Title, "", End,
        MUIA_Family_Child, obj->MN_Project_Quit,
    End;

    /* NEW: Playlist menu */
    Object *menu2 = MenuitemObject,
        MUIA_Menuitem_Title, "Playlist",
        MUIA_Family_Child, obj->MN_Playlist_Load,
        MUIA_Family_Child, MenuitemObject, MUIA_Menuitem_Title, "", End,
        MUIA_Family_Child, obj->MN_Playlist_Save,
        MUIA_Family_Child, obj->MN_Playlist_SaveAs,
    End;

    obj->MN_Main = MenustripObject,
        MUIA_Family_Child, menu1,
        MUIA_Family_Child, menu2,  /* NEW: Add playlist menu */
    End;
}

/* Window creation */
void CreateMenuEvents(struct ObjApp *obj)
{
    DoMethod(obj->MN_Project_About, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_ABOUT);

    DoMethod(obj->MN_Project_Config, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_OPEN);

    DoMethod(obj->MN_Project_Quit, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Playlist menu events */
    DoMethod(obj->MN_Playlist_Load, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_LOAD);

    DoMethod(obj->MN_Playlist_Save, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_SAVE);

    DoMethod(obj->MN_Playlist_SaveAs, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_SAVE_AS);
}

void CreateWindowMain(struct ObjApp *obj)
{
    Object *group1, *group2, *group3, *group4, *group0;
    Object *playlist_buttons, *search_row;

    /* Create connection controls */
    obj->BTN_Connect = U64SimpleButton("Connect");
    obj->BTN_LoadSongLengths = U64SimpleButton("Download Songlengths");
    obj->TXT_Status = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Text,
        MUIA_Text_Contents, "Ready",
        TAG_DONE);
    obj->TXT_ConnectionStatus = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Text,
        MUIA_Text_Contents, "Disconnected",
        TAG_DONE);

    /* Create SID info displays */
    obj->TXT_SID1_Info = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "SID #1: Not connected",
        MUIA_Text_PreParse, "\33c",
        TAG_DONE);

    obj->TXT_SID2_Info = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "SID #2: Not connected",
        MUIA_Text_PreParse, "\33c",
        TAG_DONE);

    /* Create playlist list */
    obj->LSV_PlaylistList = MUI_NewObject(MUIC_Listview,
        MUIA_Listview_List, MUI_NewObject(MUIC_List,
            MUIA_Frame, MUIV_Frame_InputList,
            MUIA_List_Active, MUIV_List_Active_Top,
            MUIA_List_Format, "",
            MUIA_List_Title, FALSE,
            TAG_DONE),
        MUIA_Listview_Input, TRUE,
        MUIA_Listview_DoubleClick, TRUE,
        MUIA_Listview_MultiSelect, MUIV_Listview_MultiSelect_None,
        TAG_DONE);

    /* Create playlist management buttons with equal widths */
    obj->BTN_AddFile = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Add Files",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 100,  /* Equal weight */
        TAG_DONE);

    obj->BTN_RemoveFile = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Remove",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 100,  /* Equal weight */
        TAG_DONE);

    obj->BTN_ClearPlaylist = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Clear All",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 100,  /* Equal weight */
        TAG_DONE);

    obj->BTN_LoadPlaylist = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Load",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 100,  /* Equal weight */
        TAG_DONE);

    obj->BTN_SavePlaylist = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Save",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 100,  /* Equal weight */
        TAG_DONE);

    /* Create search controls */
    obj->STR_SearchText = MUI_NewObject(MUIC_String,
        MUIA_Frame, MUIV_Frame_String,
        MUIA_String_MaxLen, 255,
        MUIA_CycleChain, TRUE,
        MUIA_String_Contents, "",
        MUIA_Weight, 50,  /* Limit search field width */
        TAG_DONE);

    obj->CYC_SearchMode = MUI_NewObject(MUIC_Cycle,
        MUIA_Cycle_Entries, search_mode_entries,
        MUIA_Cycle_Active, 0,
        MUIA_CycleChain, TRUE,
        MUIA_Weight, 0,
        TAG_DONE);



    obj->BTN_SearchPrev = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Prev",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 0,
        MUIA_FixWidth, 35,
        TAG_DONE);

    obj->BTN_SearchNext = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Next",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 0,
        MUIA_FixWidth, 35,
        TAG_DONE);

    obj->BTN_SearchClear = MUI_NewObject(MUIC_Text,
        MUIA_Frame, MUIV_Frame_Button,
        MUIA_Font, MUIV_Font_Button,
        MUIA_Text_Contents, "Clear",
        MUIA_Text_PreParse, "\33c",
        MUIA_Background, MUII_ButtonBack,
        MUIA_InputMode, MUIV_InputMode_RelVerify,
        MUIA_Weight, 0,
        MUIA_FixWidth, 40,
        TAG_DONE);

    /* Create player controls */
    obj->BTN_Play = U64SimpleButton("Play");
    obj->BTN_Stop = U64SimpleButton("Stop");
    obj->BTN_Next = U64SimpleButton(">>");
    obj->BTN_Prev = U64SimpleButton("<<");

    obj->CHK_Shuffle = U64CheckMark(FALSE);
    obj->CHK_Repeat = U64CheckMark(FALSE);

    /* Create current song info displays */
    obj->TXT_CurrentSong = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "No song loaded",
        MUIA_Text_PreParse, "\33c\33b",
        MUIA_Font, MUIV_Font_Big,
        TAG_DONE);

    obj->TXT_CurrentTime = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "0:00",
        TAG_DONE);

    obj->TXT_TotalTime = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "0:00",
        TAG_DONE);

    obj->GAU_Progress = MUI_NewObject(MUIC_Gauge,
        MUIA_Gauge_Horiz, TRUE,
        MUIA_Gauge_Max, 100,
        MUIA_Gauge_Current, 0,
        TAG_DONE);

    obj->TXT_SubsongInfo = MUI_NewObject(MUIC_Text,
        MUIA_Text_Contents, "",
        MUIA_Text_PreParse, "\33c",
        TAG_DONE);

    /* Create groups */

    /* Group 1: Connection & SID Status */
    group1 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Connection & SID Status",
        Child, MUI_NewObject(MUIC_Group,
            MUIA_Group_Horiz, TRUE,
            Child, obj->BTN_Connect,
            Child, obj->BTN_LoadSongLengths,
            Child, MUI_NewObject(MUIC_Rectangle, TAG_DONE), /* spacer */
            Child, U64Label("Status:"),
            Child, obj->TXT_ConnectionStatus,
            TAG_DONE),
        /* SID Configuration display */
        Child, MUI_NewObject(MUIC_Group,
            MUIA_Group_Horiz, TRUE,
            MUIA_Group_Columns, 2,
            Child, obj->TXT_SID1_Info,
            Child, obj->TXT_SID2_Info,
            TAG_DONE),
        TAG_DONE);

    /* Group 2: Now Playing */
    group2 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Now Playing",
        Child, obj->TXT_CurrentSong,
        Child, obj->TXT_SubsongInfo,
        Child, MUI_NewObject(MUIC_Group,
            MUIA_Group_Horiz, TRUE,
            Child, obj->TXT_CurrentTime,
            Child, obj->GAU_Progress,
            Child, obj->TXT_TotalTime,
            TAG_DONE),
        TAG_DONE);

    /* Group 3: Controls */
    group3 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Controls",
        MUIA_Group_Horiz, TRUE,
        Child, obj->BTN_Prev,
        Child, obj->BTN_Play,
        Child, obj->BTN_Stop,
        Child, obj->BTN_Next,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 10, TAG_DONE), /* spacer */
        Child, obj->CHK_Shuffle,
        Child, U64Label("Shuffle"),
        Child, obj->CHK_Repeat,
        Child, U64Label("Repeat"),
        TAG_DONE);

    /* Playlist management buttons row with logical grouping */
    playlist_buttons = MUI_NewObject(MUIC_Group,
        MUIA_Group_Horiz, TRUE,
        /* File operations group */
        Child, obj->BTN_AddFile,
        Child, obj->BTN_RemoveFile,
        Child, obj->BTN_ClearPlaylist,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 20, TAG_DONE), /* separator */
        /* Playlist save/load group */
        Child, obj->BTN_LoadPlaylist,
        Child, obj->BTN_SavePlaylist,
        TAG_DONE);

    /* Better balanced search row with proper spacing */
    search_row = MUI_NewObject(MUIC_Group,
        MUIA_Group_Horiz, TRUE,
        MUIA_Group_Spacing, 4,
        Child, MUI_NewObject(MUIC_Text,
            MUIA_Text_Contents, "Search:",
            MUIA_Text_PreParse, "\33r",
            MUIA_Weight, 0,
            MUIA_FixWidth, 45,
            TAG_DONE),
        Child, obj->STR_SearchText,
        Child, obj->CYC_SearchMode,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 10, TAG_DONE), /* spacer */
        Child, obj->BTN_SearchPrev,
        Child, obj->BTN_SearchNext,
        Child, obj->BTN_SearchClear,
        TAG_DONE);

    /* Group 4: Playlist with search */
    group4 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Playlist",
        Child, playlist_buttons,
        Child, search_row,
        Child, obj->LSV_PlaylistList,
        TAG_DONE);

    /* Main group containing all sections */
    group0 = MUI_NewObject(MUIC_Group,
        MUIA_Group_Columns, 1,
        Child, group1,
        Child, group2,
        Child, group3,
        Child, group4,
        Child, obj->TXT_Status,
        TAG_DONE);

    /* Create main window */
    obj->WIN_Main = MUI_NewObject(MUIC_Window,
        MUIA_Window_Title, "Ultimate64 SID Player",
        MUIA_Window_ID, APP_ID_WIN_MAIN,
        MUIA_Window_SizeGadget, TRUE,
        WindowContents, group0,
        TAG_DONE);
}

void CreateWindowMainEvents(struct ObjApp *obj)
{
    /* Window events */
    DoMethod(obj->WIN_Main, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             obj->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Button events */
    DoMethod(obj->BTN_Connect, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CONNECT);

    DoMethod(obj->BTN_LoadSongLengths, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_DOWNLOAD_SONGLENGTHS);


    DoMethod(obj->BTN_AddFile, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_ADD_FILE);

    DoMethod(obj->BTN_RemoveFile, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_REMOVE_FILE);

    DoMethod(obj->BTN_ClearPlaylist, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CLEAR_PLAYLIST);

    DoMethod(obj->BTN_Play, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAY);

    DoMethod(obj->BTN_Stop, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_STOP);

    DoMethod(obj->BTN_Next, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_NEXT);

    DoMethod(obj->BTN_Prev, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_PREV);

    DoMethod(obj->CHK_Shuffle, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SHUFFLE);

    DoMethod(obj->CHK_Repeat, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_REPEAT);

    /* Playlist events */
    DoMethod(obj->LSV_PlaylistList, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_ACTIVE);

    DoMethod(obj->LSV_PlaylistList, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_DCLICK);

    DoMethod(obj->STR_SearchText, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH_TEXT);

    DoMethod(obj->CYC_SearchMode, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH_MODE_CHANGED);

    DoMethod(obj->BTN_SearchClear, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH_CLEAR);

    DoMethod(obj->BTN_SearchNext, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH_NEXT);

    DoMethod(obj->BTN_SearchPrev, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH_PREV);

    DoMethod(obj->BTN_LoadPlaylist, MUIM_Notify, MUIA_Pressed, FALSE,
            obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_LOAD);

    DoMethod(obj->BTN_SavePlaylist, MUIM_Notify, MUIA_Pressed, FALSE,
            obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_SAVE);


}

void CreateWindowConfig(struct ObjApp *obj)
{
    Object *group0, *group1, *group2;

    obj->STR_ConfigHost = MUI_NewObject(MUIC_String,
        MUIA_Frame, MUIV_Frame_String,
        MUIA_String_MaxLen, 255,
        TAG_DONE);

    obj->STR_ConfigPassword = MUI_NewObject(MUIC_String,
        MUIA_Frame, MUIV_Frame_String,
        MUIA_String_MaxLen, 255,
        MUIA_String_Secret, TRUE,
        TAG_DONE);

    obj->BTN_ConfigOK = U64SimpleButton("OK");
    obj->BTN_ConfigCancel = U64SimpleButton("Cancel");

    group1 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Connection Settings",
        MUIA_Group_Columns, 2,
        Child, U64Label("Host:"),
        Child, obj->STR_ConfigHost,
        Child, U64Label("Password:"),
        Child, obj->STR_ConfigPassword,
        TAG_DONE);

    group2 = MUI_NewObject(MUIC_Group,
        MUIA_Group_Horiz, TRUE,
        Child, MUI_NewObject(MUIC_Rectangle, TAG_DONE), /* spacer */
        Child, obj->BTN_ConfigOK,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 10, TAG_DONE), /* spacer */
        Child, obj->BTN_ConfigCancel,
        Child, MUI_NewObject(MUIC_Rectangle, TAG_DONE), /* spacer */
        TAG_DONE);

    group0 = MUI_NewObject(MUIC_Group,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 2, TAG_DONE), /* spacer */
        Child, group1,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 2, TAG_DONE), /* spacer */
        Child, group2,
        Child, MUI_NewObject(MUIC_Rectangle, MUIA_Weight, 2, TAG_DONE), /* spacer */
        TAG_DONE);

    obj->WIN_Config = MUI_NewObject(MUIC_Window,
        MUIA_Window_Title, "Ultimate64 Configuration",
        MUIA_Window_ID, APP_ID_WIN_CONFIG,
        MUIA_Window_Width, 400,
        MUIA_Window_Height, 150,
        MUIA_Window_CloseGadget, FALSE,
        WindowContents, group0,
        TAG_DONE);
}

void CreateWindowConfigEvents(struct ObjApp *obj)
{
    DoMethod(obj->BTN_ConfigOK, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_OK);

    DoMethod(obj->BTN_ConfigCancel, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_CANCEL);
}
