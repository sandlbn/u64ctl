/* Ultimate64 SID Player - shared header
 * For Amiga OS 3.x by Marcin Spoczynski
 */

#ifndef U64_PLAYER_H
#define U64_PLAYER_H

#include <stddef.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <devices/timer.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Constants */
#define DEFAULT_SONG_LENGTH 300 /* 5 minutes in seconds */
#define MAX_PLAYLIST_ENTRIES 1000
#define MD5_HASH_SIZE 16
#define MD5_STRING_SIZE 33 /* 32 hex chars + null terminator */

/* Environment variable names */
#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_SID_DIR "Ultimate64/SidDir"

/* Window IDs */
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) \
  ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#define APP_ID_WIN_MAIN MAKE_ID('S', 'I', 'D', '0')
#define APP_ID_WIN_CONFIG MAKE_ID('S', 'I', 'D', '1')

/* Custom button creation function to avoid macro conflicts */
#define U64SimpleButton(text) \
  MUI_NewObject(MUIC_Text, \
    MUIA_Frame, MUIV_Frame_Button, \
    MUIA_Font, MUIV_Font_Button, \
    MUIA_Text_Contents, text, \
    MUIA_Text_PreParse, "\33c", \
    MUIA_Background, MUII_ButtonBack, \
    MUIA_InputMode, MUIV_InputMode_RelVerify, \
    TAG_DONE)

#define U64Label(text) \
  MUI_NewObject(MUIC_Text, \
    MUIA_Text_Contents, text, \
    MUIA_Text_PreParse, "\33r", \
    TAG_DONE)

#define U64CheckMark(selected) \
  MUI_NewObject(MUIC_Image, \
    MUIA_Image_Spec, MUII_CheckMark, \
    MUIA_InputMode, MUIV_InputMode_Toggle, \
    MUIA_Image_FreeVert, TRUE, \
    MUIA_Selected, selected, \
    TAG_DONE)

/* Event IDs */
enum EVENT_IDS {
  EVENT_CONNECT = 100,
  EVENT_ADD_FILE,
  EVENT_REMOVE_FILE,
  EVENT_CLEAR_PLAYLIST,
  EVENT_PLAY,
  EVENT_STOP,
  EVENT_NEXT,
  EVENT_PREV,
  EVENT_SHUFFLE,
  EVENT_REPEAT,
  EVENT_DOWNLOAD_SONGLENGTHS,
  EVENT_QUIT,
  EVENT_ABOUT,
  EVENT_PLAYLIST_DCLICK,
  EVENT_PLAYLIST_ACTIVE,
  EVENT_CONFIG_OPEN,
  EVENT_CONFIG_OK,
  EVENT_CONFIG_CANCEL,
  EVENT_CONFIG_CLEAR,
  EVENT_TIMER_UPDATE,
  EVENT_PLAYLIST_SAVE = 200,
  EVENT_PLAYLIST_LOAD,
  EVENT_PLAYLIST_SAVE_AS,
  EVENT_SEARCH_TEXT,
  EVENT_SEARCH_MODE_CHANGED,
  EVENT_SEARCH_CLEAR,
  EVENT_SEARCH_NEXT,
  EVENT_SEARCH_PREV
};

/* Playlist entry structure */
typedef struct PlaylistEntry
{
  STRPTR filename;
  STRPTR title;
  UBYTE md5[MD5_HASH_SIZE];
  ULONG duration; /* in seconds, 0 = unknown */
  UWORD subsongs;
  UWORD current_subsong;
  struct PlaylistEntry *next;
} PlaylistEntry;

/* Song length database entry */
typedef struct SongLengthEntry
{
  UBYTE md5[MD5_HASH_SIZE];
  ULONG *lengths; /* Array of lengths for each subsong */
  UWORD num_subsongs;
  struct SongLengthEntry *next;
} SongLengthEntry;

/* Player state */
typedef enum
{
  PLAYER_STOPPED,
  PLAYER_PLAYING,
  PLAYER_PAUSED
} PlayerState;

/* MD5 context */
typedef struct {
  ULONG state[4];
  ULONG count[2];
  UBYTE buffer[64];
} MD5_CTX;

/* Application data structure */
struct ObjApp
{
  /* MUI Application and Windows */
  Object *App;
  Object *WIN_Main;
  Object *WIN_Config;

  /* Connection controls */
  Object *BTN_Connect;
  Object *BTN_LoadSongLengths;
  Object *TXT_Status;
  Object *TXT_ConnectionStatus;

  /* Playlist controls */
  Object *LSV_PlaylistList;
  Object *BTN_AddFile;
  Object *BTN_RemoveFile;
  Object *BTN_ClearPlaylist;

  /* Player controls */
  Object *BTN_Play;
  Object *BTN_Stop;
  Object *BTN_Next;
  Object *BTN_Prev;
  Object *CHK_Shuffle;
  Object *CHK_Repeat;

  /* Current song info */
  Object *TXT_CurrentSong;
  Object *TXT_CurrentTime;
  Object *TXT_TotalTime;
  Object *GAU_Progress;
  Object *TXT_SubsongInfo;

  /* Configuration window */
  Object *STR_ConfigHost;
  Object *STR_ConfigPassword;
  Object *BTN_ConfigOK;
  Object *BTN_ConfigCancel;

  /* Menu */
  Object *MN_Main;
  Object *MN_Project_About;
  Object *MN_Project_Config;
  Object *MN_Project_Quit;

  /* Data */
  U64Connection *connection;
  PlaylistEntry *playlist_head;
  PlaylistEntry *current_entry;
  SongLengthEntry *songlength_db;
  ULONG playlist_count;
  ULONG current_index;

  Object *MN_Playlist_Load;
  Object *MN_Playlist_Save;
  Object *MN_Playlist_SaveAs;
  Object *BTN_LoadPlaylist;
  Object *BTN_SavePlaylist;

  /* Player state */
  PlayerState state;
  ULONG current_time;
  ULONG total_time;
  BOOL shuffle_mode;
  BOOL repeat_mode;
  ULONG timer_counter;

  Object *TXT_SID1_Info;
  Object *TXT_SID2_Info;

    /* Search controls */
  Object *STR_SearchText;
  Object *CYC_SearchMode;
  Object *BTN_SearchClear;
  Object *BTN_SearchNext;
  Object *BTN_SearchPrev;
  Object *GRP_SearchControls;

  /* Search data */
  char search_text[256];
  BOOL search_mode_filter;  /* TRUE = filter, FALSE = search/navigate */
  BOOL *playlist_visible;   /* Array tracking visibility in filter mode */
  ULONG search_current_match; /* Current match index in search mode */
  ULONG search_total_matches; /* Total matches found */


  /* Configuration */
  char host[256];
  char password[256];
  char last_sid_dir[256];
  char current_playlist_file[512];

};

/* Globals */
extern struct Library *MUIMasterBase;
extern struct Library *AslBase;
extern struct ObjApp *objApp;

/* Timer globals (shared between timer.c and main.c) */
extern struct MsgPort      *TimerPort;
extern struct timerequest  *TimerReq;
extern ULONG                TimerSig;
extern BOOL                 TimerRunning;

/* main.c */
BOOL InitLibs(void);
void CleanupLibs(void);
struct ObjApp *CreateApp(void);
void DisposeApp(struct ObjApp *obj);

/* config.c */
BOOL LoadConfig(struct ObjApp *obj);
BOOL SaveConfig(struct ObjApp *obj);

/* timer.c */
BOOL StartTimerDevice(void);
void StopTimerDevice(void);
void StartPeriodicTimer(void);
void StopPeriodicTimer(void);
BOOL CheckTimerSignal(ULONG sigs);

/* md5.c */
void MD5Init(MD5_CTX *ctx);
void MD5Update(MD5_CTX *ctx, const UBYTE *input, ULONG inputLen);
void MD5Final(UBYTE digest[MD5_HASH_SIZE], MD5_CTX *ctx);
void CalculateMD5(const UBYTE *data, ULONG size, UBYTE digest[MD5_HASH_SIZE]);
void MD5ToHexString(const UBYTE hash[MD5_HASH_SIZE], char hex_string[MD5_STRING_SIZE]);
BOOL HexStringToMD5(const char *hex_string, UBYTE hash[MD5_HASH_SIZE]);
BOOL MD5Compare(const UBYTE hash1[MD5_HASH_SIZE], const UBYTE hash2[MD5_HASH_SIZE]);

/* sid.c */
UWORD ParseSIDSubsongs(const UBYTE *data, ULONG size);
STRPTR ExtractSIDTitle(const UBYTE *data, ULONG size);
ULONG ParseTimeString(const char *time_str);

/* songdb.c */
BOOL LoadSongLengthsWithProgress(struct ObjApp *obj, CONST_STRPTR filename);
BOOL CheckSongLengthsFile(char *filepath, ULONG filepath_size);
void AutoLoadSongLengths(struct ObjApp *obj);
BOOL APP_DownloadSongLengths(void);
ULONG FindSongLength(struct ObjApp *obj, const UBYTE md5[MD5_HASH_SIZE], UWORD subsong);
void FreeSongLengthDB(struct ObjApp *obj);

/* playlist.c */
BOOL AddPlaylistEntry(struct ObjApp *obj, CONST_STRPTR filename);
BOOL SavePlaylistToFile(struct ObjApp *obj, CONST_STRPTR filename);
BOOL LoadPlaylistFromFile(struct ObjApp *obj, CONST_STRPTR filename);
BOOL APP_PlaylistSave(void);
BOOL APP_PlaylistSaveAs(void);
BOOL APP_PlaylistLoad(void);
BOOL APP_ClearPlaylist(void);
BOOL APP_PlaylistDoubleClick(void);
BOOL APP_PlaylistActive(void);
void FreePlaylists(struct ObjApp *obj);
void APP_UpdatePlaylistDisplay(void);

/* playback.c */
BOOL APP_Play(void);
BOOL APP_Stop(void);
BOOL APP_Next(void);
BOOL APP_Prev(void);
BOOL PlayCurrentSong(struct ObjApp *obj);
void APP_TimerUpdate(void);
void APP_UpdateCurrentSongDisplay(void);
void APP_UpdateCurrentSongCache(void);

/* ui.c */
void CreateWindowMain(struct ObjApp *obj);
void CreateWindowMainEvents(struct ObjApp *obj);
void CreateWindowConfig(struct ObjApp *obj);
void CreateWindowConfigEvents(struct ObjApp *obj);
void CreateMenu(struct ObjApp *obj);
void CreateMenuEvents(struct ObjApp *obj);

/* search.c */
BOOL APP_SearchTextChanged(void);
BOOL APP_SearchModeChanged(void);
BOOL APP_SearchClear(void);
BOOL APP_SearchNext(void);
BOOL APP_SearchPrev(void);
BOOL MatchesSearchTerm(PlaylistEntry *entry, const char *search_term);
void UpdateSearchMatches(void);
void UpdateFilteredPlaylistDisplay(void);

/* handlers.c */
BOOL APP_Connect(void);
BOOL APP_AddFiles(void);
BOOL APP_RemoveFile(void);
BOOL APP_About(void);
BOOL APP_ConfigOpen(void);
BOOL APP_ConfigOK(void);
BOOL APP_ConfigCancel(void);
BOOL APP_Init(void);
void APP_UpdateStatus(CONST_STRPTR text);
BOOL APP_GetSIDConfig(char *sid1_info, char *sid2_info, size_t buffer_size);
void APP_UpdateSIDConfigDisplay(void);

#endif /* U64_PLAYER_H */
