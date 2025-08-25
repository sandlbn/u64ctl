/* Ultimate64 SID Player
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
#include <utility/tagitem.h>
#include <workbench/workbench.h>
#include <devices/timer.h>

#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Version string */
static const char version[] = "$VER: u64sidplayer 0.3.0 (2025)";

/* Constants */
#define DEFAULT_SONG_LENGTH 300 /* 5 minutes in seconds */
#define MAX_PLAYLIST_ENTRIES 1000
#define MD5_HASH_SIZE 16
#define MD5_STRING_SIZE 33 /* 32 hex chars + null terminator */

/* Environment variable names */
#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_SID_DIR "Ultimate64/SidDir"

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
  EVENT_PLAYLIST_SAVE_AS
};

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

  /* Player state */
  PlayerState state;
  ULONG current_time;
  ULONG total_time;
  BOOL shuffle_mode;
  BOOL repeat_mode;
  ULONG timer_counter;

  /* Configuration */
  char host[256];
  char password[256];
  char last_sid_dir[256];
  char current_playlist_file[512];

};

/* Global variables */
struct Library *MUIMasterBase = NULL;
struct Library *AslBase = NULL;
struct ObjApp *objApp = NULL;

/* Simple cache for current song info to prevent corruption */
static struct {
  UWORD cached_subsongs;
  UWORD cached_current;
  ULONG cached_duration;
  char cached_title[256];
  char cached_filename[256];
  BOOL cache_valid;
} current_song_cache = { 0, 0, 0, "", "", FALSE };

static struct MsgPort      *TimerPort  = NULL;
static struct timerequest  *TimerReq   = NULL;
static ULONG               TimerSig    = 0;
static BOOL                TimerRunning = FALSE;

/* Forward declarations */
static BOOL InitLibs(void);
static void CleanupLibs(void);
static struct ObjApp *CreateApp(void);
static void DisposeApp(struct ObjApp *obj);
static void CreateWindowMain(struct ObjApp *obj);
static void CreateWindowMainEvents(struct ObjApp *obj);
static void CreateWindowConfig(struct ObjApp *obj);
static void CreateWindowConfigEvents(struct ObjApp *obj);
static void CreateMenu(struct ObjApp *obj);
static void CreateMenuEvents(struct ObjApp *obj);

/* Application functions */
static BOOL APP_Init(void);
static void APP_UpdateStatus(CONST_STRPTR text);
static void APP_UpdatePlaylistDisplay(void);
static void APP_UpdateCurrentSongDisplay(void);
static void APP_UpdateCurrentSongCache(void);
static BOOL APP_Connect(void);
static BOOL APP_AddFiles(void);
static BOOL APP_RemoveFile(void);
static BOOL APP_ClearPlaylist(void);
static BOOL APP_Play(void);
static BOOL APP_Stop(void);
static BOOL APP_Next(void);
static BOOL APP_Prev(void);
static BOOL APP_LoadSongLengths(void);
static BOOL APP_About(void);
static BOOL APP_ConfigOpen(void);
static BOOL APP_ConfigOK(void);
static BOOL APP_ConfigCancel(void);
static BOOL APP_PlaylistDoubleClick(void);
static BOOL APP_PlaylistActive(void);
static void APP_TimerUpdate(void);

/* Memory management functions */
static STRPTR SafeStrDup(CONST_STRPTR str);
static void SafeStrFree(STRPTR str);
static void FreePlaylists(struct ObjApp *obj);
static void FreeSongLengthDB(struct ObjApp *obj);

static BOOL StartTimerDevice(void);
static void StopTimerDevice(void);
static void StartPeriodicTimer(void);
static void StopPeriodicTimer(void);
static BOOL CheckTimerSignal(ULONG sigs);

/* Configuration functions */
static BOOL LoadConfig(struct ObjApp *obj);
static BOOL SaveConfig(struct ObjApp *obj);
static STRPTR ReadEnvVar(CONST_STRPTR var_name);
static BOOL WriteEnvVar(CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent);

/* Playlist functions */
static BOOL AddPlaylistEntry(struct ObjApp *obj, CONST_STRPTR filename);
static BOOL PlayCurrentSong(struct ObjApp *obj);
static ULONG FindSongLength(struct ObjApp *obj, const UBYTE md5[MD5_HASH_SIZE], UWORD subsong);

static BOOL APP_PlaylistSave(void);
static BOOL APP_PlaylistSaveAs(void);
static BOOL APP_PlaylistLoad(void);
static BOOL SavePlaylistToFile(struct ObjApp *obj, CONST_STRPTR filename);
static BOOL LoadPlaylistFromFile(struct ObjApp *obj, CONST_STRPTR filename);
static STRPTR EscapeString(CONST_STRPTR str);
static STRPTR UnescapeString(CONST_STRPTR str);

/* MD5 and file functions */
static BOOL LoadFile(CONST_STRPTR filename, UBYTE **data, ULONG *size);
static void CalculateMD5(const UBYTE *data, ULONG size, UBYTE digest[MD5_HASH_SIZE]);
static STRPTR ExtractSIDTitle(const UBYTE *data, ULONG size);
static UWORD ParseSIDSubsongs(const UBYTE *data, ULONG size);
static ULONG ParseTimeString(const char *time_str);

/* Song length database functions */
static BOOL LoadSongLengthsWithProgress(struct ObjApp *obj, CONST_STRPTR filename);
static BOOL HexStringToMD5(const char *hex_string, UBYTE hash[MD5_HASH_SIZE]);
static void MD5ToHexString(const UBYTE hash[MD5_HASH_SIZE], char hex_string[MD5_STRING_SIZE]);
static BOOL CheckSongLengthsFile(char *filepath, ULONG filepath_size);
static void AutoLoadSongLengths(struct ObjApp *obj);

static void DownloadProgressCallback(ULONG bytes_downloaded, APTR userdata);
static BOOL APP_DownloadSongLengths(void);
static BOOL MD5Compare(const UBYTE hash1[MD5_HASH_SIZE], const UBYTE hash2[MD5_HASH_SIZE]);

/* MD5 implementation */
typedef struct {
  ULONG state[4];
  ULONG count[2];
  UBYTE buffer[64];
} MD5_CTX;

static void MD5Init(MD5_CTX *ctx);
static void MD5Update(MD5_CTX *ctx, const UBYTE *input, ULONG inputLen);
static void MD5Final(UBYTE digest[MD5_HASH_SIZE], MD5_CTX *ctx);
static void MD5Transform(ULONG state[4], const UBYTE block[64]);

/* MD5 constants and functions */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) { \
  (a) += F ((b), (c), (d)) + (x) + (ULONG)(ac); \
  (a) = ROTATE_LEFT ((a), (s)); \
  (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
  (a) += G ((b), (c), (d)) + (x) + (ULONG)(ac); \
  (a) = ROTATE_LEFT ((a), (s)); \
  (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
  (a) += H ((b), (c), (d)) + (x) + (ULONG)(ac); \
  (a) = ROTATE_LEFT ((a), (s)); \
  (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
  (a) += I ((b), (c), (d)) + (x) + (ULONG)(ac); \
  (a) = ROTATE_LEFT ((a), (s)); \
  (a) += (b); \
}

static UBYTE PADDING[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Library initialization */
static BOOL InitLibs(void)
{
    if (!(MUIMasterBase = OpenLibrary(MUIMASTER_NAME, 19))) {
        return FALSE;
    }

    if (!(AslBase = OpenLibrary("asl.library", 37))) {
        CloseLibrary(MUIMasterBase);
        return FALSE;
    }

    return TRUE;
}

static void CleanupLibs(void)
{
    if (AslBase) CloseLibrary(AslBase);
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
}

/* Safe string duplication */
static STRPTR SafeStrDup(CONST_STRPTR str)
{
    STRPTR result;
    ULONG len;
    
    if (!str) return NULL;
    
    len = strlen(str) + 1;
    result = AllocVec(len, MEMF_PUBLIC | MEMF_CLEAR);
    if (result) {
        strcpy(result, str);
    }
    return result;
}

static void SafeStrFree(STRPTR str)
{
    if (str) {
        FreeVec(str);
    }
}

/* Environment variable functions */
static STRPTR ReadEnvVar(CONST_STRPTR var_name)
{
    LONG result;
    STRPTR buffer;
    ULONG buffer_size = 256;

    buffer = AllocVec(buffer_size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!buffer) {
        return NULL;
    }

    result = GetVar((STRPTR)var_name, buffer, buffer_size, GVF_GLOBAL_ONLY);

    if (result > 0) {
        STRPTR final_buffer = AllocVec(result + 1, MEMF_PUBLIC);
        if (final_buffer) {
            strcpy(final_buffer, buffer);
            FreeVec(buffer);
            return final_buffer;
        }
    }

    FreeVec(buffer);
    return NULL;
}

static BOOL WriteEnvVar(CONST_STRPTR var_name, CONST_STRPTR value, BOOL persistent)
{
    LONG result;
    ULONG flags = GVF_GLOBAL_ONLY;

    if (!var_name || !value) {
        return FALSE;
    }

    result = SetVar((STRPTR)var_name, (STRPTR)value, strlen(value), flags);

    if (result && persistent) {
        BPTR file;
        STRPTR envarc_path;
        ULONG path_len = strlen("ENVARC:") + strlen(var_name) + 1;

        envarc_path = AllocVec(path_len, MEMF_PUBLIC);
        if (envarc_path) {
            strcpy(envarc_path, "ENVARC:");
            strcat(envarc_path, var_name);

            STRPTR dir_end = strrchr(envarc_path, '/');
            if (dir_end) {
                *dir_end = '\0';
                CreateDir(envarc_path);
                *dir_end = '/';
            }

            file = Open(envarc_path, MODE_NEWFILE);
            if (file) {
                Write(file, (STRPTR)value, strlen(value));
                Close(file);
            }

            FreeVec(envarc_path);
        }
    }

    return result ? TRUE : FALSE;
}

/* Configuration functions */
static BOOL LoadConfig(struct ObjApp *obj)
{
    STRPTR env_host, env_password, env_dir;

    env_host = ReadEnvVar(ENV_ULTIMATE64_HOST);
    if (env_host) {
        strcpy(obj->host, env_host);
        FreeVec(env_host);
    } else {
        obj->host[0] = '\0';
    }

    env_password = ReadEnvVar(ENV_ULTIMATE64_PASSWORD);
    if (env_password) {
        strcpy(obj->password, env_password);
        FreeVec(env_password);
    } else {
        obj->password[0] = '\0';
    }

    env_dir = ReadEnvVar(ENV_ULTIMATE64_SID_DIR);
    if (env_dir) {
        strncpy(obj->last_sid_dir, env_dir, sizeof(obj->last_sid_dir) - 1);
        obj->last_sid_dir[sizeof(obj->last_sid_dir) - 1] = '\0';
        FreeVec(env_dir);
    } else {
        strcpy(obj->last_sid_dir, "");
    }

    return TRUE;
}

static BOOL SaveConfig(struct ObjApp *obj)
{
    WriteEnvVar(ENV_ULTIMATE64_HOST, obj->host, TRUE);
    if (strlen(obj->password) > 0) {
        WriteEnvVar(ENV_ULTIMATE64_PASSWORD, obj->password, TRUE);
    } else {
        DeleteVar(ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
        DeleteFile("ENVARC:" ENV_ULTIMATE64_PASSWORD);
    }
    
    if (strlen(obj->last_sid_dir) > 0) {
        WriteEnvVar(ENV_ULTIMATE64_SID_DIR, obj->last_sid_dir, TRUE);
    }
    
    return TRUE;
}

/* MD5 implementation */
static void MD5Init(MD5_CTX *ctx)
{
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void MD5Update(MD5_CTX *ctx, const UBYTE *input, ULONG inputLen)
{
    ULONG i, index, partLen;

    index = (ULONG)((ctx->count[0] >> 3) & 0x3F);

    if ((ctx->count[0] += ((ULONG)inputLen << 3)) < ((ULONG)inputLen << 3))
        ctx->count[1]++;
    ctx->count[1] += ((ULONG)inputLen >> 29);

    partLen = 64 - index;

    if (inputLen >= partLen) {
        CopyMem((APTR)input, (APTR)&ctx->buffer[index], partLen);
        MD5Transform(ctx->state, ctx->buffer);

        for (i = partLen; i + 63 < inputLen; i += 64)
            MD5Transform(ctx->state, &input[i]);

        index = 0;
    } else
        i = 0;

    CopyMem((APTR)&input[i], (APTR)&ctx->buffer[index], inputLen - i);
}

static void MD5Final(UBYTE digest[MD5_HASH_SIZE], MD5_CTX *ctx)
{
    UBYTE bits[8];
    ULONG index, padLen;

    /* Save number of bits */
    bits[0] = (UBYTE)(ctx->count[0] & 0xFF);
    bits[1] = (UBYTE)((ctx->count[0] >> 8) & 0xFF);
    bits[2] = (UBYTE)((ctx->count[0] >> 16) & 0xFF);
    bits[3] = (UBYTE)((ctx->count[0] >> 24) & 0xFF);
    bits[4] = (UBYTE)(ctx->count[1] & 0xFF);
    bits[5] = (UBYTE)((ctx->count[1] >> 8) & 0xFF);
    bits[6] = (UBYTE)((ctx->count[1] >> 16) & 0xFF);
    bits[7] = (UBYTE)((ctx->count[1] >> 24) & 0xFF);

    /* Pad out to 56 mod 64. */
    index = (ULONG)((ctx->count[0] >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    MD5Update(ctx, PADDING, padLen);

    /* Append length (before padding) */
    MD5Update(ctx, bits, 8);

    /* Store state in digest */
    for (index = 0; index < 16; index++) {
        digest[index] = (UBYTE)((ctx->state[index >> 2] >> ((index & 3) << 3)) & 0xFF);
    }

    /* Zeroize sensitive information. */
    memset((APTR)ctx, 0, sizeof(*ctx));
}

static void MD5Transform(ULONG state[4], const UBYTE block[64])
{
    ULONG a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    ULONG i;

    for (i = 0; i < 16; i++) {
        x[i] = (ULONG)block[i * 4] | ((ULONG)block[i * 4 + 1] << 8)
             | ((ULONG)block[i * 4 + 2] << 16)
             | ((ULONG)block[i * 4 + 3] << 24);
    }

    /* Round 1 */
    FF(a, b, c, d, x[0], S11, 0xd76aa478);
    FF(d, a, b, c, x[1], S12, 0xe8c7b756);
    FF(c, d, a, b, x[2], S13, 0x242070db);
    FF(b, c, d, a, x[3], S14, 0xc1bdceee);
    FF(a, b, c, d, x[4], S11, 0xf57c0faf);
    FF(d, a, b, c, x[5], S12, 0x4787c62a);
    FF(c, d, a, b, x[6], S13, 0xa8304613);
    FF(b, c, d, a, x[7], S14, 0xfd469501);
    FF(a, b, c, d, x[8], S11, 0x698098d8);
    FF(d, a, b, c, x[9], S12, 0x8b44f7af);
    FF(c, d, a, b, x[10], S13, 0xffff5bb1);
    FF(b, c, d, a, x[11], S14, 0x895cd7be);
    FF(a, b, c, d, x[12], S11, 0x6b901122);
    FF(d, a, b, c, x[13], S12, 0xfd987193);
    FF(c, d, a, b, x[14], S13, 0xa679438e);
    FF(b, c, d, a, x[15], S14, 0x49b40821);

    /* Round 2 */
    GG(a, b, c, d, x[1], S21, 0xf61e2562);
    GG(d, a, b, c, x[6], S22, 0xc040b340);
    GG(c, d, a, b, x[11], S23, 0x265e5a51);
    GG(b, c, d, a, x[0], S24, 0xe9b6c7aa);
    GG(a, b, c, d, x[5], S21, 0xd62f105d);
    GG(d, a, b, c, x[10], S22, 0x2441453);
    GG(c, d, a, b, x[15], S23, 0xd8a1e681);
    GG(b, c, d, a, x[4], S24, 0xe7d3fbc8);
    GG(a, b, c, d, x[9], S21, 0x21e1cde6);
    GG(d, a, b, c, x[14], S22, 0xc33707d6);
    GG(c, d, a, b, x[3], S23, 0xf4d50d87);
    GG(b, c, d, a, x[8], S24, 0x455a14ed);
    GG(a, b, c, d, x[13], S21, 0xa9e3e905);
    GG(d, a, b, c, x[2], S22, 0xfcefa3f8);
    GG(c, d, a, b, x[7], S23, 0x676f02d9);
    GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

    /* Round 3 */
    HH(a, b, c, d, x[5], S31, 0xfffa3942);
    HH(d, a, b, c, x[8], S32, 0x8771f681);
    HH(c, d, a, b, x[11], S33, 0x6d9d6122);
    HH(b, c, d, a, x[14], S34, 0xfde5380c);
    HH(a, b, c, d, x[1], S31, 0xa4beea44);
    HH(d, a, b, c, x[4], S32, 0x4bdecfa9);
    HH(c, d, a, b, x[7], S33, 0xf6bb4b60);
    HH(b, c, d, a, x[10], S34, 0xbebfbc70);
    HH(a, b, c, d, x[13], S31, 0x289b7ec6);
    HH(d, a, b, c, x[0], S32, 0xeaa127fa);
    HH(c, d, a, b, x[3], S33, 0xd4ef3085);
    HH(b, c, d, a, x[6], S34, 0x4881d05);
    HH(a, b, c, d, x[9], S31, 0xd9d4d039);
    HH(d, a, b, c, x[12], S32, 0xe6db99e5);
    HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
    HH(b, c, d, a, x[2], S34, 0xc4ac5665);

    /* Round 4 */
    II(a, b, c, d, x[0], S41, 0xf4292244);
    II(d, a, b, c, x[7], S42, 0x432aff97);
    II(c, d, a, b, x[14], S43, 0xab9423a7);
    II(b, c, d, a, x[5], S44, 0xfc93a039);
    II(a, b, c, d, x[12], S41, 0x655b59c3);
    II(d, a, b, c, x[3], S42, 0x8f0ccc92);
    II(c, d, a, b, x[10], S43, 0xffeff47d);
    II(b, c, d, a, x[1], S44, 0x85845dd1);
    II(a, b, c, d, x[8], S41, 0x6fa87e4f);
    II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    II(c, d, a, b, x[6], S43, 0xa3014314);
    II(b, c, d, a, x[13], S44, 0x4e0811a1);
    II(a, b, c, d, x[4], S41, 0xf7537e82);
    II(d, a, b, c, x[11], S42, 0xbd3af235);
    II(c, d, a, b, x[2], S43, 0x2ad7d2bb);
    II(b, c, d, a, x[9], S44, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;

    /* Zeroize sensitive information. */
    memset((APTR)x, 0, sizeof(x));
}

static void CalculateMD5(const UBYTE *data, ULONG size, UBYTE digest[MD5_HASH_SIZE])
{
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, data, size);
    MD5Final(digest, &ctx);
}

/* Convert MD5 hash to hex string */
static void MD5ToHexString(const UBYTE hash[MD5_HASH_SIZE], char hex_string[MD5_STRING_SIZE])
{
    static const char hex_chars[] = "0123456789abcdef";
    int i;

    for (i = 0; i < MD5_HASH_SIZE; i++) {
        hex_string[i * 2] = hex_chars[(hash[i] >> 4) & 0x0F];
        hex_string[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    hex_string[MD5_STRING_SIZE - 1] = '\0';
}

static BOOL HexStringToMD5(const char *hex_string, UBYTE hash[MD5_HASH_SIZE])
{
    int i;

    if (strlen(hex_string) != 32) {
        return FALSE;
    }

    for (i = 0; i < MD5_HASH_SIZE; i++) {
        char high = hex_string[i * 2];
        char low = hex_string[i * 2 + 1];

        if (!isxdigit(high) || !isxdigit(low)) {
            return FALSE;
        }

        high = tolower(high);
        low = tolower(low);

        UBYTE high_val = (high >= 'a') ? (high - 'a' + 10) : (high - '0');
        UBYTE low_val = (low >= 'a') ? (low - 'a' + 10) : (low - '0');

        hash[i] = (high_val << 4) | low_val;
    }

    return TRUE;
}
static void
ULONGToString(ULONG value, char *buffer)
{
    int pos = 0;
    
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    char digits[16];
    int digit_pos = 0;
    
    while (value > 0) {
        digits[digit_pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    /* Reverse digits */
    for (int i = digit_pos - 1; i >= 0; i--) {
        buffer[pos++] = digits[i];
    }
    buffer[pos] = '\0';
}
/* File loading function */
static BOOL LoadFile(CONST_STRPTR filename, UBYTE **data, ULONG *size)
{
    BPTR file;
    LONG file_size;
    UBYTE *buffer;

    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        return FALSE;
    }

    Seek(file, 0, OFFSET_END);
    file_size = Seek(file, 0, OFFSET_BEGINNING);

    if (file_size <= 0) {
        Close(file);
        return FALSE;
    }

    buffer = AllocVec(file_size, MEMF_PUBLIC);
    if (!buffer) {
        Close(file);
        return FALSE;
    }

    if (Read(file, buffer, file_size) != file_size) {
        FreeVec(buffer);
        Close(file);
        return FALSE;
    }

    Close(file);

    *data = buffer;
    *size = file_size;
    return TRUE;
}

/* SID file parsing functions */
static UWORD ParseSIDSubsongs(const UBYTE *data, ULONG size)
{
    if (size < 0x7E || memcmp(data, "PSID", 4) != 0) {
        return 1; // Default to 1 subsong
    }

    UWORD songs = (data[14] << 8) | data[15];
    return (songs > 0) ? songs : 1;
}

static void APP_UpdateStatus(CONST_STRPTR text)
{
    if (objApp && objApp->TXT_Status) {
        set(objApp->TXT_Status, MUIA_Text_Contents, text);
    }
}

static STRPTR ExtractSIDTitle(const UBYTE *data, ULONG size)
{
    if (size < 0x7E || memcmp(data, "PSID", 4) != 0) {
        return NULL;
    }

    // Title is at offset 0x16, 32 bytes
    char title[33];
    CopyMem((APTR)(data + 0x16), title, 32);
    title[32] = '\0';

    // Remove trailing spaces and nulls
    int i;
    for (i = 31; i >= 0 && (title[i] == ' ' || title[i] == '\0'); i--) {
        title[i] = '\0';
    }

    if (strlen(title) == 0) {
        return NULL;
    }

    return SafeStrDup(title);
}

static BOOL StartTimerDevice(void)
{
    U64_DEBUG("Starting timer device...");
    
    if (!(TimerPort = CreateMsgPort())) {
        U64_DEBUG("Failed to create timer port");
        return FALSE;
    }
    
    if (!(TimerReq = (struct timerequest*)CreateIORequest(TimerPort, sizeof(*TimerReq)))) {
        U64_DEBUG("Failed to create timer request");
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
        return FALSE;
    }
    
    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest*)TimerReq, 0)) {
        U64_DEBUG("Failed to open timer device");
        DeleteIORequest((struct IORequest*)TimerReq);
        DeleteMsgPort(TimerPort);
        TimerReq = NULL;
        TimerPort = NULL;
        return FALSE;
    }
    
    TimerSig = 1UL << TimerPort->mp_SigBit;
    TimerRunning = FALSE;
    
    U64_DEBUG("Timer device started successfully, signal mask: 0x%08x", (unsigned int)TimerSig);
    return TRUE;
}

static void StopTimerDevice(void)
{
    U64_DEBUG("Stopping timer device...");
    StopPeriodicTimer();
    
    if (TimerReq) {
        CloseDevice((struct IORequest*)TimerReq);
        DeleteIORequest((struct IORequest*)TimerReq);
        TimerReq = NULL;
    }
    
    if (TimerPort) {
        DeleteMsgPort(TimerPort);
        TimerPort = NULL;
    }
    
    TimerSig = 0;
    TimerRunning = FALSE;
    U64_DEBUG("Timer device stopped");
}

static void StartPeriodicTimer(void)
{
    if (!TimerReq || TimerRunning) return;
    
    U64_DEBUG("Starting periodic timer");
    
    TimerReq->tr_node.io_Command = TR_ADDREQUEST;
    TimerReq->tr_time.tv_secs    = 1;
    TimerReq->tr_time.tv_micro   = 0;
    
    SendIO((struct IORequest*)TimerReq);
    TimerRunning = TRUE;
}

static void StopPeriodicTimer(void)
{
    if (!TimerReq || !TimerRunning) return;
    
    U64_DEBUG("Stopping periodic timer");
    
    if (!CheckIO((struct IORequest*)TimerReq)) {
        AbortIO((struct IORequest*)TimerReq);
    }
    WaitIO((struct IORequest*)TimerReq);
    
    TimerRunning = FALSE;
}

static BOOL CheckTimerSignal(ULONG sigs)
{
    struct Message *msg;
    BOOL timer_fired = FALSE;
    
    if (!TimerPort || !TimerRunning || !(sigs & TimerSig)) {
        return FALSE;
    }
    
    while ((msg = GetMsg(TimerPort))) {
        timer_fired = TRUE;
        TimerRunning = FALSE;
        
        if (objApp && objApp->state == PLAYER_PLAYING) {
            objApp->current_time++;
            APP_UpdateCurrentSongDisplay();
            if (objApp->current_time >= objApp->total_time) {
                APP_Next();
            }
            
            if (objApp->state == PLAYER_PLAYING) {
                StartPeriodicTimer();
            }
        }
    }
    
    return timer_fired;
}

static ULONG ParseTimeString(const char *time_str)
{
    char *colon_pos;
    ULONG minutes = 0;
    ULONG seconds = 0;
    char temp_str[32];

    if (!time_str || strlen(time_str) == 0) {
        return 0;
    }

    /* Make a copy to work with */
    strncpy(temp_str, time_str, sizeof(temp_str) - 1);
    temp_str[sizeof(temp_str) - 1] = '\0';

    /* Find colon separator */
    colon_pos = strchr(temp_str, ':');
    if (!colon_pos) {
        /* No colon, treat as seconds only */
        return (ULONG)atoi(temp_str);
    }

    /* Split at colon */
    *colon_pos = '\0';
    minutes = (ULONG)atoi(temp_str);

    /* Parse seconds part */
    char *seconds_str = colon_pos + 1;
    char *dot_pos = strchr(seconds_str, '.');
    if (dot_pos) {
        *dot_pos = '\0'; /* Ignore milliseconds */
    }

    seconds = (ULONG)atoi(seconds_str);

    /* Convert to total seconds */
    return (minutes * 60) + seconds;
}

static STRPTR EscapeString(CONST_STRPTR str)
{
    ULONG len, escaped_len;
    STRPTR escaped;
    ULONG i, j;

    if (!str) return NULL;

    len = strlen(str);
    escaped_len = len * 2 + 1;
    
    escaped = AllocVec(escaped_len, MEMF_PUBLIC | MEMF_CLEAR);
    if (!escaped) return NULL;

    for (i = 0, j = 0; i < len && j < escaped_len - 2; i++) {
        if (str[i] == '"' || str[i] == '\\' || str[i] == '\n' || str[i] == '\r') {
            escaped[j++] = '\\';
            if (str[i] == '\n') {
                escaped[j++] = 'n';
            } else if (str[i] == '\r') {
                escaped[j++] = 'r';
            } else {
                escaped[j++] = str[i];
            }
        } else {
            escaped[j++] = str[i];
        }
    }
    escaped[j] = '\0';

    return escaped;
}

static STRPTR UnescapeString(CONST_STRPTR str)
{
    ULONG len;
    STRPTR unescaped;
    ULONG i, j;

    if (!str) return NULL;

    len = strlen(str);
    unescaped = AllocVec(len + 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!unescaped) return NULL;

    for (i = 0, j = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            i++;
            if (str[i] == 'n') {
                unescaped[j++] = '\n';
            } else if (str[i] == 'r') {
                unescaped[j++] = '\r';
            } else {
                unescaped[j++] = str[i];
            }
        } else {
            unescaped[j++] = str[i];
        }
    }
    unescaped[j] = '\0';

    return unescaped;
}

static BOOL SavePlaylistToFile(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    PlaylistEntry *entry;
    char line[1024];
    char md5_str[MD5_STRING_SIZE];
    STRPTR escaped_filename, escaped_title;
    ULONG count = 0;

    file = Open(filename, MODE_NEWFILE);
    if (!file) {
        APP_UpdateStatus("Failed to create playlist file");
        return FALSE;
    }

    Write(file, "# Ultimate64 SID Player Playlist\n", 33);
    Write(file, "# Format: \"filename\" \"title\" md5hash subsongs current_subsong\n", 61);
    Write(file, "\n", 1);

    entry = obj->playlist_head;
    while (entry) {
        count++;

        if ((count % 10) == 0) {
            static char progress_msg[256];
            strcpy(progress_msg, "Saving playlist... ");
            
            char count_str[32];
            ULONGToString(count, count_str);
            strcat(progress_msg, count_str);
            
            APP_UpdateStatus(progress_msg);

            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        escaped_filename = EscapeString(entry->filename);
        escaped_title = EscapeString(entry->title);

        MD5ToHexString(entry->md5, md5_str);

        strcpy(line, "\"");
        if (escaped_filename) {
            strcat(line, escaped_filename);
        }
        strcat(line, "\" \"");
        if (escaped_title) {
            strcat(line, escaped_title);
        }
        strcat(line, "\" ");
        strcat(line, md5_str);
        strcat(line, " ");

        char subsongs_str[16];
        ULONGToString(entry->subsongs, subsongs_str);
        strcat(line, subsongs_str);
        strcat(line, " ");

        char current_str[16];
        ULONGToString(entry->current_subsong, current_str);
        strcat(line, current_str);
        strcat(line, "\n");

        Write(file, line, strlen(line));

        if (escaped_filename) FreeVec(escaped_filename);
        if (escaped_title) FreeVec(escaped_title);

        entry = entry->next;
    }

    Close(file);

    static char status_msg[256];
    strcpy(status_msg, "Saved ");
    
    char count_str[32];
    ULONGToString(count, count_str);
    strcat(status_msg, count_str);
    strcat(status_msg, " entries to playlist");
    
    APP_UpdateStatus(status_msg);

    return TRUE;
}

/* Progress callback for MUI updates */
static void DownloadProgressCallback(ULONG bytes_downloaded, APTR userdata)
{
    (void)userdata;
    static char progress_msg[256];
    
    /* Add debug output */
    U64_DEBUG("Progress callback: %lu bytes", (unsigned long)bytes_downloaded);
    
    /* Convert bytes to KB for display */
    ULONG kb_downloaded = bytes_downloaded / 1024;
    
    sprintf(progress_msg, "Downloaded %lu KB from HVSC server",
        (unsigned long)kb_downloaded);  /* Use %lu instead of %u */

    APP_UpdateStatus(progress_msg);
    
    /* Process MUI events to keep interface responsive */
    if (objApp && objApp->App) {
        ULONG signals;
        DoMethod(objApp->App, MUIM_Application_Input, &signals);
    }
}

static BOOL APP_DownloadSongLengths(void)
{
    char local_filename[256];
    char progdir[256];
    BPTR lock;
    LONG result;
    
    if (!objApp) return FALSE;
    
    APP_UpdateStatus("Connecting to HVSC server...");
    
    /* Disable button during download */
    set(objApp->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
    set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Downloading...");
    
    /* Determine save location */
    lock = GetProgramDir();
    if (lock && NameFromLock(lock, progdir, sizeof(progdir))) {
        strcpy(local_filename, progdir);
        AddPart(local_filename, "Songlengths.md5", sizeof(local_filename));
    } else {
        strcpy(local_filename, "Songlengths.md5");
    }
    
    U64_DEBUG("Download target: %s", local_filename);
    
    const char *urls[] = {
        "http://hvsc.brona.dk/HVSC/C64Music/DOCUMENTS/Songlengths.md5",
        NULL
    };
    
    result = U64_ERR_GENERAL;
    for (int i = 0; urls[i] && result != U64_OK; i++) {
        char status_msg[256];
        sprintf(status_msg, "Trying download from URL %d...", i + 1);
        APP_UpdateStatus(status_msg);
        
        U64_DEBUG("Attempting download from: %s", urls[i]);
        result = U64_DownloadToFile(urls[i], local_filename, DownloadProgressCallback, NULL);
        
        if (result != U64_OK) {
            U64_DEBUG("Download attempt %d failed: %s", i + 1, U64_GetErrorString(result));
        }
    }
    
    if (result != U64_OK) {
        char error_msg[256];
        
        /* Provide specific error messages */
        switch (result) {
            case U64_ERR_NETWORK:
                sprintf(error_msg, "Network error - check internet connection");
                break;
            case U64_ERR_ACCESS:
                sprintf(error_msg, "File access error - check disk space and permissions");
                break;
            case U64_ERR_NOTFOUND:
                sprintf(error_msg, "File not found on HVSC server");
                break;
            case U64_ERR_TIMEOUT:
                sprintf(error_msg, "Download timeout - server may be busy");
                break;
            default:
                sprintf(error_msg, "Download failed: %s", U64_GetErrorString(result));
                break;
        }
        
        APP_UpdateStatus(error_msg);
        U64_DEBUG("All download attempts failed");
        
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }
    
    APP_UpdateStatus("Download complete, verifying file...");
    
    /* Verify downloaded file */
    BPTR test_file = Open(local_filename, MODE_OLDFILE);
    if (!test_file) {
        APP_UpdateStatus("Error: Downloaded file not accessible");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }
    
    /* Check file size */
    Seek(test_file, 0, OFFSET_END);
    LONG file_size = Seek(test_file, 0, OFFSET_BEGINNING);
    Close(test_file);
    
    if (file_size < 10000) { /* Expect at least 10KB */
        char error_msg[256];
        sprintf(error_msg, "Downloaded file too small (%ld bytes) - may be error page", file_size);
        APP_UpdateStatus(error_msg);
        DeleteFile(local_filename);
        
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }
    
    char verify_msg[256];
    sprintf(verify_msg, "File verified OK (%ld bytes), loading database...", file_size);
    APP_UpdateStatus(verify_msg);
    
    /* Load the downloaded file */
    if (LoadSongLengthsWithProgress(objApp, local_filename)) {
        /* Update existing playlist entries */
        if (objApp->playlist_count > 0) {
            PlaylistEntry *entry = objApp->playlist_head;
            ULONG updated = 0;
            ULONG entry_num = 0;

            APP_UpdateStatus("Updating playlist with HVSC song lengths...");

            while (entry) {
                entry_num++;
                if ((entry_num % 10) == 0) {
                    char progress_msg[256];
                    sprintf(progress_msg, "Updating playlist... %u/%u",
                            (unsigned int)entry_num, (unsigned int)objApp->playlist_count);
                    APP_UpdateStatus(progress_msg);
                    
                    ULONG signals;
                    DoMethod(objApp->App, MUIM_Application_Input, &signals);
                }

                SongLengthEntry *db_entry = objApp->songlength_db;
                while (db_entry) {
                    if (MD5Compare(db_entry->md5, entry->md5)) {
                        if (db_entry->num_subsongs > entry->subsongs && db_entry->num_subsongs <= 256) {
                            entry->subsongs = db_entry->num_subsongs;
                            updated++;
                        }
                        entry->duration = FindSongLength(objApp, entry->md5, entry->current_subsong);
                        break;
                    }
                    db_entry = db_entry->next;
                }
                entry = entry->next;
            }

            APP_UpdatePlaylistDisplay();
            if (objApp->current_entry) {
                APP_UpdateCurrentSongCache();
                APP_UpdateCurrentSongDisplay();
            }

            char final_msg[256];
            sprintf(final_msg, "HVSC database loaded! Updated %u of %u playlist entries",
                    (unsigned int)updated, (unsigned int)objApp->playlist_count);
            APP_UpdateStatus(final_msg);
        } else {
            APP_UpdateStatus("HVSC Songlengths database downloaded and loaded successfully!");
        }
    } else {
        APP_UpdateStatus("File downloaded but database loading failed");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }
    
    set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
    set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
    
    return TRUE;
}

static BOOL LoadPlaylistFromFile(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    char line[1024];
    ULONG count = 0;
    ULONG line_number = 0;

    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        APP_UpdateStatus("Failed to open playlist file");
        return FALSE;
    }

    FreePlaylists(obj);
    APP_UpdateStatus("Loading playlist...");

    while (FGets(file, line, sizeof(line))) {
        char *filename_start, *filename_end;
        char *title_start, *title_end;
        char *md5_start, *md5_end;
        char *subsongs_start, *current_start;
        PlaylistEntry *entry;
        char *newline;
        STRPTR unescaped_filename, unescaped_title;

        line_number++;

        if ((line_number % 25) == 0) {
            static char progress_msg[256];
            strcpy(progress_msg, "Loading... line ");
            
            char line_str[32];
            ULONGToString(line_number, line_str);
            strcat(progress_msg, line_str);
            
            APP_UpdateStatus(progress_msg);

            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        filename_start = strchr(line, '"');
        if (!filename_start) continue;
        filename_start++;
        
        filename_end = filename_start;
        while (*filename_end && *filename_end != '"') {
            if (*filename_end == '\\' && *(filename_end + 1)) {
                filename_end += 2;
            } else {
                filename_end++;
            }
        }
        if (*filename_end != '"') continue;
        *filename_end = '\0';

        title_start = strchr(filename_end + 1, '"');
        if (!title_start) {
            *filename_end = '"';
            continue;
        }
        title_start++;
        
        title_end = title_start;
        while (*title_end && *title_end != '"') {
            if (*title_end == '\\' && *(title_end + 1)) {
                title_end += 2;
            } else {
                title_end++;
            }
        }
        if (*title_end != '"') {
            *filename_end = '"';
            continue;
        }
        *title_end = '\0';

        md5_start = title_end + 1;
        while (*md5_start && (*md5_start == ' ' || *md5_start == '\t')) {
            md5_start++;
        }
        
        md5_end = md5_start;
        while (*md5_end && *md5_end != ' ' && *md5_end != '\t') {
            md5_end++;
        }
        if (md5_end - md5_start != 32) {
            *filename_end = '"';
            *title_end = '"';
            continue;
        }

        subsongs_start = md5_end;
        while (*subsongs_start && (*subsongs_start == ' ' || *subsongs_start == '\t')) {
            subsongs_start++;
        }

        current_start = subsongs_start;
        while (*current_start && *current_start != ' ' && *current_start != '\t') {
            current_start++;
        }
        while (*current_start && (*current_start == ' ' || *current_start == '\t')) {
            current_start++;
        }

        entry = AllocVec(sizeof(PlaylistEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!entry) {
            *filename_end = '"';
            *title_end = '"';
            continue;
        }

        unescaped_filename = UnescapeString(filename_start);
        unescaped_title = UnescapeString(title_start);

        *filename_end = '"';
        *title_end = '"';

        if (!unescaped_filename) {
            if (unescaped_title) FreeVec(unescaped_title);
            FreeVec(entry);
            continue;
        }

        entry->filename = unescaped_filename;
        entry->title = unescaped_title;

        char md5_temp[33];
        CopyMem(md5_start, md5_temp, 32);
        md5_temp[32] = '\0';
        if (!HexStringToMD5(md5_temp, entry->md5)) {
            SafeStrFree(entry->filename);
            SafeStrFree(entry->title);
            FreeVec(entry);
            continue;
        }

        entry->subsongs = (UWORD)atoi(subsongs_start);
        entry->current_subsong = (UWORD)atoi(current_start);

        if (entry->subsongs == 0) entry->subsongs = 1;
        if (entry->current_subsong >= entry->subsongs) {
            entry->current_subsong = 0;
        }

        entry->duration = FindSongLength(obj, entry->md5, entry->current_subsong);
        if (entry->duration == 0) {
            entry->duration = DEFAULT_SONG_LENGTH;
        }

        if (!obj->playlist_head) {
            obj->playlist_head = entry;
        } else {
            PlaylistEntry *last = obj->playlist_head;
            while (last->next) {
                last = last->next;
            }
            last->next = entry;
        }

        obj->playlist_count++;
        count++;
    }

    Close(file);

    if (obj->playlist_head && !obj->current_entry) {
        obj->current_entry = obj->playlist_head;
        obj->current_index = 0;
        APP_UpdateCurrentSongCache();
    }

    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();

    static char status_msg[256];
    strcpy(status_msg, "Loaded ");
    
    char count_str[32];
    ULONGToString(count, count_str);
    strcat(status_msg, count_str);
    strcat(status_msg, " entries from playlist");
    
    APP_UpdateStatus(status_msg);

    return TRUE;
}

static BOOL APP_PlaylistSave(void)
{
    if (!objApp) return FALSE;

    if (strlen(objApp->current_playlist_file) > 0) {
        return SavePlaylistToFile(objApp, objApp->current_playlist_file);
    } else {
        return APP_PlaylistSaveAs();
    }
}

static BOOL APP_PlaylistSaveAs(void)
{
    struct FileRequester *req;
    BOOL success = FALSE;

    if (!objApp || !AslBase) return FALSE;

    if (objApp->playlist_count == 0) {
        APP_UpdateStatus("No playlist to save");
        return FALSE;
    }

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, "Save Playlist As",
        ASLFR_DoSaveMode, TRUE,
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, "#?.m3u",
        ASLFR_InitialFile, "playlist.m3u",
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);

    if (req && AslRequest(req, NULL)) {
        char filename[512];
        strcpy(filename, req->rf_Dir);
        AddPart(filename, req->rf_File, sizeof(filename));

        if (!strstr(filename, ".m3u") && !strstr(filename, ".M3U")) {
            strcat(filename, ".m3u");
        }

        if (SavePlaylistToFile(objApp, filename)) {
            strncpy(objApp->current_playlist_file, filename, 
                    sizeof(objApp->current_playlist_file) - 1);
            objApp->current_playlist_file[sizeof(objApp->current_playlist_file) - 1] = '\0';
            success = TRUE;
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    return success;
}

static BOOL APP_PlaylistLoad(void)
{
    struct FileRequester *req;
    BOOL success = FALSE;

    if (!objApp || !AslBase) return FALSE;

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, "Load Playlist",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, "#?.m3u",
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);

    if (req && AslRequest(req, NULL)) {
        char filename[512];
        strcpy(filename, req->rf_Dir);
        AddPart(filename, req->rf_File, sizeof(filename));

        if (LoadPlaylistFromFile(objApp, filename)) {
            strncpy(objApp->current_playlist_file, filename, 
                    sizeof(objApp->current_playlist_file) - 1);
            objApp->current_playlist_file[sizeof(objApp->current_playlist_file) - 1] = '\0';
            success = TRUE;
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    return success;
}

/* Song length database functions */
static BOOL CheckSongLengthsFile(char *filepath, ULONG filepath_size)
{
    BPTR lock;
    char progdir[256];
    BOOL found = FALSE;

    /* Get program directory */
    lock = GetProgramDir();
    if (lock) {
        if (NameFromLock(lock, progdir, sizeof(progdir))) {
            /* Try common filename variations */
            const char *filenames[] = {
                "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5",
                "Songlengths.txt", "songlengths.txt", NULL
            };

            int i;
            for (i = 0; filenames[i]; i++) {
                strncpy(filepath, progdir, filepath_size - 1);
                filepath[filepath_size - 1] = '\0';
                AddPart(filepath, filenames[i], filepath_size);

                BPTR file = Open(filepath, MODE_OLDFILE);
                if (file) {
                    Close(file);
                    U64_DEBUG("Found songlengths file: %s", filepath);
                    found = TRUE;
                    break;
                }
            }
        }
    }

    if (!found) {
        /* Also try current directory */
        const char *filenames[] = {
            "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5", NULL
        };

        int i;
        for (i = 0; filenames[i]; i++) {
            BPTR file = Open(filenames[i], MODE_OLDFILE);
            if (file) {
                Close(file);
                strcpy(filepath, filenames[i]);
                U64_DEBUG("Found songlengths file in current dir: %s", filepath);
                found = TRUE;
                break;
            }
        }
    }

    return found;
}

/* Load song lengths database with progress feedback */
static BOOL
LoadSongLengthsWithProgress(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file;
    char line[1024];
    SongLengthEntry *entry;
    ULONG count = 0;
    ULONG line_count = 0;
    ULONG processed_lines = 0;

    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        APP_UpdateStatus("Cannot open songlengths file");
        return FALSE;
    }

    /* First pass: count total lines for progress */
    APP_UpdateStatus("Analyzing songlengths database...");
    while (FGets(file, line, sizeof(line))) {
        line_count++;
        if ((line_count % 1000) == 0) {
            /* Build progress message manually */
            static char progress_msg[256];
            strcpy(progress_msg, "Analyzing... ");
            
            /* Convert line_count to string manually */
            char count_str[32];
            ULONG temp_count = line_count;
            int pos = 0;
            
            if (temp_count == 0) {
                count_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;
                
                while (temp_count > 0) {
                    digits[digit_pos++] = '0' + (temp_count % 10);
                    temp_count /= 10;
                }
                
                /* Reverse digits */
                for (int i = digit_pos - 1; i >= 0; i--) {
                    count_str[pos++] = digits[i];
                }
            }
            count_str[pos] = '\0';
            
            strcat(progress_msg, count_str);
            strcat(progress_msg, " lines");
            APP_UpdateStatus(progress_msg);

            /* Process MUI events to keep interface responsive */
            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }
    }

    /* Reset to beginning */
    Seek(file, 0, OFFSET_BEGINNING);

    /* Free existing database */
    FreeSongLengthDB(obj);

    APP_UpdateStatus("Loading song lengths database...");

    while (FGets(file, line, sizeof(line))) {
        char *md5_str, *length_str, *pos;
        UBYTE md5[MD5_HASH_SIZE];
        char *newline;

        processed_lines++;

        /* Update progress every 100 lines */
        if ((processed_lines % 100) == 0) {
            /* Build progress message manually */
            static char progress_msg[256];
            strcpy(progress_msg, "Loading database... ");
            
            /* Calculate percentage manually */
            ULONG percent = (processed_lines * 100) / line_count;
            
            /* Convert percent to string */
            char percent_str[8];
            ULONG temp_percent = percent;
            int pos = 0;
            
            if (temp_percent == 0) {
                percent_str[0] = '0';
                pos = 1;
            } else {
                char digits[8];
                int digit_pos = 0;
                
                while (temp_percent > 0) {
                    digits[digit_pos++] = '0' + (temp_percent % 10);
                    temp_percent /= 10;
                }
                
                for (int i = digit_pos - 1; i >= 0; i--) {
                    percent_str[pos++] = digits[i];
                }
            }
            percent_str[pos] = '\0';
            
            strcat(progress_msg, percent_str);
            strcat(progress_msg, "% (");
            
            /* Add processed_lines count */
            char processed_str[32];
            ULONG temp_processed = processed_lines;
            pos = 0;
            
            if (temp_processed == 0) {
                processed_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;
                
                while (temp_processed > 0) {
                    digits[digit_pos++] = '0' + (temp_processed % 10);
                    temp_processed /= 10;
                }
                
                for (int i = digit_pos - 1; i >= 0; i--) {
                    processed_str[pos++] = digits[i];
                }
            }
            processed_str[pos] = '\0';
            
            strcat(progress_msg, processed_str);
            strcat(progress_msg, "/");
            
            /* Add total line count */
            char total_str[32];
            ULONG temp_total = line_count;
            pos = 0;
            
            if (temp_total == 0) {
                total_str[0] = '0';
                pos = 1;
            } else {
                char digits[16];
                int digit_pos = 0;
                
                while (temp_total > 0) {
                    digits[digit_pos++] = '0' + (temp_total % 10);
                    temp_total /= 10;
                }
                
                for (int i = digit_pos - 1; i >= 0; i--) {
                    total_str[pos++] = digits[i];
                }
            }
            total_str[pos] = '\0';
            
            strcat(progress_msg, total_str);
            strcat(progress_msg, " entries)");
            
            APP_UpdateStatus(progress_msg);

            /* Process MUI events to keep interface responsive */
            ULONG signals;
            DoMethod(obj->App, MUIM_Application_Input, &signals);
        }

        /* Remove trailing newline */
        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        /* Find the '=' separator */
        pos = strchr(line, '=');
        if (!pos) {
            continue;
        }

        *pos = '\0';
        md5_str = line;
        length_str = pos + 1;

        /* Validate MD5 string length */
        if (strlen(md5_str) != 32) {
            continue;
        }

        /* Parse MD5 */
        if (!HexStringToMD5(md5_str, md5)) {
            continue;
        }

        /* Parse lengths */
        ULONG lengths[256];
        UWORD num_lengths = 0;

        char length_copy[512];
        strncpy(length_copy, length_str, sizeof(length_copy) - 1);
        length_copy[sizeof(length_copy) - 1] = '\0';

        char *token = strtok(length_copy, " \t");

        while (token && num_lengths < 256) {
            ULONG duration = ParseTimeString(token);
            if (duration > 0) {
                lengths[num_lengths] = duration + 1; /* +1 second buffer */
                num_lengths++;
            }
            token = strtok(NULL, " \t");
        }

        if (num_lengths == 0) {
            continue;
        }

        /* Create database entry */
        entry = AllocVec(sizeof(SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!entry) {
            continue;
        }

        CopyMem(md5, entry->md5, MD5_HASH_SIZE);
        entry->num_subsongs = num_lengths;
        entry->lengths = AllocVec(sizeof(ULONG) * num_lengths, MEMF_PUBLIC);

        if (!entry->lengths) {
            FreeVec(entry);
            continue;
        }

        CopyMem(lengths, entry->lengths, sizeof(ULONG) * num_lengths);

        /* Add to database */
        entry->next = obj->songlength_db;
        obj->songlength_db = entry;
        count++;
    }

    Close(file);

    /* Build final status message manually */
    static char status_msg[256];
    strcpy(status_msg, "Loaded ");
    
    /* Convert count to string manually */
    char count_str[32];
    ULONG temp_count = count;
    int pos = 0;
    
    if (temp_count == 0) {
        count_str[0] = '0';
        pos = 1;
    } else {
        char digits[16];
        int digit_pos = 0;
        
        while (temp_count > 0) {
            digits[digit_pos++] = '0' + (temp_count % 10);
            temp_count /= 10;
        }
        
        for (int i = digit_pos - 1; i >= 0; i--) {
            count_str[pos++] = digits[i];
        }
    }
    count_str[pos] = '\0';
    
    strcat(status_msg, count_str);
    strcat(status_msg, " song length entries (+1 sec buffer)");
    APP_UpdateStatus(status_msg);

    U64_DEBUG("Total entries loaded: %u (all durations include +1 second buffer)",
              (unsigned int)count);
    return TRUE;
}

/* Auto-load songlengths on startup */
static void AutoLoadSongLengths(struct ObjApp *obj)
{
    char filepath[512];

    U64_DEBUG("=== AutoLoadSongLengths START ===");

    if (CheckSongLengthsFile(filepath, sizeof(filepath))) {
        U64_DEBUG("Auto-loading songlengths from: %s", filepath);

        APP_UpdateStatus("Auto-loading songlengths database...");

        /* Disable the Load Songlengths button temporarily */
        set(obj->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
        set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Loading...");

        if (LoadSongLengthsWithProgress(obj, filepath)) {
            /* Update any existing playlist entries */
            if (obj->playlist_count > 0) {
                APP_UpdateStatus("Updating playlist with song lengths...");

                PlaylistEntry *entry = obj->playlist_head;
                ULONG updated = 0;
                ULONG entry_num = 0;

                while (entry) {
                    entry_num++;

                    /* Update progress every 10 entries */
                    if ((entry_num % 10) == 0) {
                        char progress_msg[256];
                        sprintf(progress_msg, "Updating playlist... %u/%u",
                                (unsigned int)entry_num,
                                (unsigned int)obj->playlist_count);
                        APP_UpdateStatus(progress_msg);

                        /* Process MUI events */
                        ULONG signals;
                        DoMethod(obj->App, MUIM_Application_Input, &signals);
                    }

                    /* Check if we have this SID in database */
                    SongLengthEntry *db_entry = obj->songlength_db;

                    while (db_entry) {
                        if (memcmp(db_entry->md5, entry->md5, MD5_HASH_SIZE) == 0) {
                            UWORD old_subsongs = entry->subsongs;

                            if (db_entry->num_subsongs > old_subsongs
                                && db_entry->num_subsongs <= 256) {
                                entry->subsongs = db_entry->num_subsongs;
                                updated++;
                                U64_DEBUG("Updated %s: %d -> %d subsongs",
                                          FilePart(entry->filename),
                                          old_subsongs, entry->subsongs);
                            }

                            /* Update duration for current subsong */
                            entry->duration = FindSongLength(obj, entry->md5, entry->current_subsong);
                            break;
                        }
                        db_entry = db_entry->next;
                    }

                    entry = entry->next;
                }

                /* Update display */
                APP_UpdatePlaylistDisplay();
                if (obj->current_entry) {
                    APP_UpdateCurrentSongCache();
                    APP_UpdateCurrentSongDisplay();
                }

                char final_msg[256];
                sprintf(final_msg,
                        "Database loaded. Updated %u of %u playlist entries",
                        (unsigned int)updated,
                        (unsigned int)obj->playlist_count);
                APP_UpdateStatus(final_msg);
            } else {
                APP_UpdateStatus("Songlengths database loaded successfully");
            }
        }

        /* Re-enable the button */
        set(obj->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
    } else {
        U64_DEBUG("No songlengths file found in program directory");
        APP_UpdateStatus("Ready - No songlengths database found");
    }

    U64_DEBUG("=== AutoLoadSongLengths END ===");
}

/* Menu creation */
static void CreateMenu(struct ObjApp *obj)
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

static void CreateMenuEvents(struct ObjApp *obj)
{
    DoMethod(obj->MN_Project_About, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_ABOUT);

    DoMethod(obj->MN_Project_Config, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_OPEN);

    DoMethod(obj->MN_Project_Quit, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* NEW: Playlist menu events */
    DoMethod(obj->MN_Playlist_Load, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_LOAD);

    DoMethod(obj->MN_Playlist_Save, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_SAVE);

    DoMethod(obj->MN_Playlist_SaveAs, MUIM_Notify, MUIA_Menuitem_Trigger,
             MUIV_EveryTime, obj->App, 2, MUIM_Application_ReturnID, EVENT_PLAYLIST_SAVE_AS);
}

/* Window creation */
static void CreateWindowMain(struct ObjApp *obj)
{
    Object *group1, *group2, *group3, *group4, *group0;

    /* Create controls */
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

    obj->BTN_AddFile = U64SimpleButton("Add Files");
    obj->BTN_RemoveFile = U64SimpleButton("Remove");
    obj->BTN_ClearPlaylist = U64SimpleButton("Clear All");

    obj->BTN_Play = U64SimpleButton("Play");
    obj->BTN_Stop = U64SimpleButton("Stop");
    obj->BTN_Next = U64SimpleButton(">>");
    obj->BTN_Prev = U64SimpleButton("<<");

    obj->CHK_Shuffle = U64CheckMark(FALSE);
    obj->CHK_Repeat = U64CheckMark(FALSE);

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
    group1 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Connection",
        MUIA_Group_Horiz, TRUE,
        Child, obj->BTN_Connect,
        Child, obj->BTN_LoadSongLengths,
        Child, MUI_NewObject(MUIC_Rectangle, TAG_DONE), /* spacer */
        Child, U64Label("Status:"),
        Child, obj->TXT_ConnectionStatus,
        TAG_DONE);

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

    group4 = MUI_NewObject(MUIC_Group,
        MUIA_Frame, MUIV_Frame_Group,
        MUIA_FrameTitle, "Playlist",
        Child, MUI_NewObject(MUIC_Group,
            MUIA_Group_Horiz, TRUE,
            Child, obj->BTN_AddFile,
            Child, obj->BTN_RemoveFile,
            Child, obj->BTN_ClearPlaylist,
            Child, MUI_NewObject(MUIC_Rectangle, TAG_DONE), /* spacer */
            TAG_DONE),
        Child, obj->LSV_PlaylistList,
        TAG_DONE);

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

static void CreateWindowMainEvents(struct ObjApp *obj)
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
}

static void CreateWindowConfig(struct ObjApp *obj)
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

static void CreateWindowConfigEvents(struct ObjApp *obj)
{
    DoMethod(obj->BTN_ConfigOK, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_OK);

    DoMethod(obj->BTN_ConfigCancel, MUIM_Notify, MUIA_Pressed, FALSE,
             obj->App, 2, MUIM_Application_ReturnID, EVENT_CONFIG_CANCEL);
}

/* Create application */
static struct ObjApp *CreateApp(void)
{
    struct ObjApp *obj;

    U64_DEBUG("Creating App...");

    if ((obj = AllocVec(sizeof(struct ObjApp), MEMF_PUBLIC | MEMF_CLEAR))) {
        U64_DEBUG("Memory allocated");

        /* Create windows */
        CreateMenu(obj);
        CreateWindowMain(obj);
        CreateWindowConfig(obj);

        if (obj->WIN_Main && obj->WIN_Config) {
            /* Create Application object */
            obj->App = MUI_NewObject(MUIC_Application,
                MUIA_Application_Title, "Ultimate64 SID Player",
                MUIA_Application_Version, version,
                MUIA_Application_Copyright, "2025",
                MUIA_Application_Author, "Marcin Spoczynski",
                MUIA_Application_Description, "SID Player with Playlist for Ultimate64",
                MUIA_Application_Base, "U64SIDPLAYER",
                MUIA_Application_Menustrip, obj->MN_Main,
                SubWindow, obj->WIN_Main,
                SubWindow, obj->WIN_Config,
                TAG_DONE);

            if (obj->App) {
                U64_DEBUG("Application object created");

                /* Create events */
                CreateMenuEvents(obj);
                CreateWindowMainEvents(obj);
                CreateWindowConfigEvents(obj);

                U64_DEBUG("App creation complete");
                return obj;
            }
        }
        
        /* Cleanup on failure */
        FreeVec(obj);
    }
    
    return NULL;
}

static void DisposeApp(struct ObjApp *obj)
{
    if (obj) {
        /* NEW: Stop timer device */
        StopTimerDevice();
        
        FreePlaylists(obj);
        FreeSongLengthDB(obj);
        
        if (obj->connection) {
            U64_Disconnect(obj->connection);
        }
        
        if (obj->App) {
            MUI_DisposeObject(obj->App);
        }
        
        FreeVec(obj);
    }
}

/* Memory management functions */
static void FreePlaylists(struct ObjApp *obj)
{
    PlaylistEntry *entry = obj->playlist_head;
    PlaylistEntry *next;

    /* Clear the MUI list first to prevent access to freed memory */
    if (obj->LSV_PlaylistList) {
        set(obj->LSV_PlaylistList, MUIA_List_Quiet, TRUE);
        DoMethod(obj->LSV_PlaylistList, MUIM_List_Clear);
        set(obj->LSV_PlaylistList, MUIA_List_Quiet, FALSE);
    }

    /* Free playlist entries */
    while (entry) {
        next = entry->next;
        
        SafeStrFree(entry->filename);
        SafeStrFree(entry->title);
        FreeVec(entry);
        
        entry = next;
    }

    obj->playlist_head = NULL;
    obj->current_entry = NULL;
    obj->playlist_count = 0;
    obj->current_index = 0;
}

static void FreeSongLengthDB(struct ObjApp *obj)
{
    SongLengthEntry *entry = obj->songlength_db;
    SongLengthEntry *next;

    while (entry) {
        next = entry->next;

        if (entry->lengths) {
            FreeVec(entry->lengths);
        }
        FreeVec(entry);
        
        entry = next;
    }

    obj->songlength_db = NULL;
}

/* Helper functions */
static BOOL MD5Compare(const UBYTE hash1[MD5_HASH_SIZE], const UBYTE hash2[MD5_HASH_SIZE])
{
    return (memcmp(hash1, hash2, MD5_HASH_SIZE) == 0);
}

static ULONG FindSongLength(struct ObjApp *obj, const UBYTE md5[MD5_HASH_SIZE], UWORD subsong)
{
    SongLengthEntry *entry = obj->songlength_db;

    while (entry) {
        if (MD5Compare(entry->md5, md5)) {
            if (subsong < entry->num_subsongs) {
                return entry->lengths[subsong];
            }
            break;
        }
        entry = entry->next;
    }

    return DEFAULT_SONG_LENGTH;
}

static void APP_UpdateCurrentSongCache(void)
{
    if (!objApp->current_entry) {
        current_song_cache.cache_valid = FALSE;
        return;
    }

    /* Disable interrupts while updating cache to prevent corruption */
    Forbid();

    current_song_cache.cached_subsongs = objApp->current_entry->subsongs;
    current_song_cache.cached_current = objApp->current_entry->current_subsong;
    current_song_cache.cached_duration = objApp->current_entry->duration;

    if (objApp->current_entry->title) {
        strncpy(current_song_cache.cached_title, objApp->current_entry->title,
                sizeof(current_song_cache.cached_title) - 1);
        current_song_cache.cached_title[sizeof(current_song_cache.cached_title) - 1] = '\0';
    } else {
        current_song_cache.cached_title[0] = '\0';
    }

    if (objApp->current_entry->filename) {
        strncpy(current_song_cache.cached_filename, objApp->current_entry->filename,
                sizeof(current_song_cache.cached_filename) - 1);
        current_song_cache.cached_filename[sizeof(current_song_cache.cached_filename) - 1] = '\0';
    } else {
        current_song_cache.cached_filename[0] = '\0';
    }

    current_song_cache.cache_valid = TRUE;

    Permit();
}

/* Application functions implementation */
static BOOL APP_Init(void)
{
    if (!U64_InitLibrary()) {
        U64_DEBUG("Failed to initialize Ultimate64 library");
        return FALSE;
    }
    
    /* NEW: Initialize timer device */
    if (!StartTimerDevice()) {
        U64_DEBUG("Failed to initialize timer device - continuing without timer");
    }
    #ifdef DEBUG_BUILD
    U64_SetVerboseMode(TRUE);
    #endif
    LoadConfig(objApp);
    srand(time(NULL));
    
    return TRUE;
}


static void
APP_UpdatePlaylistDisplay(void)
{
    PlaylistEntry *entry;
    ULONG i;

    if (!objApp || !objApp->LSV_PlaylistList) return;

    U64_DEBUG("=== APP_UpdatePlaylistDisplay FIXED START ===");

    /* Clear list first */
    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, TRUE);
    DoMethod(objApp->LSV_PlaylistList, MUIM_List_Clear);

    if (objApp->playlist_count == 0) {
        set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);
        U64_DEBUG("Playlist is empty");
        return;
    }

    /* Add entries one by one to the list */
    entry = objApp->playlist_head;
    for (i = 0; i < objApp->playlist_count && entry; i++) {
        char *basename = FilePart(entry->filename);

        U64_DEBUG("=== PROCESSING ENTRY %d ===", (int)i);
        U64_DEBUG("Entry address: %p", entry);
        U64_DEBUG("Filename: %s", basename);

        /* Read values safely into local int variables */
        int entry_subsongs = (int)entry->subsongs;
        int entry_current = (int)entry->current_subsong;
        int entry_duration = (int)entry->duration;

        U64_DEBUG("Raw entry values: subsongs=%d, current=%d, duration=%d",
                  entry_subsongs, entry_current, entry_duration);

        /* Validate values */
        if (entry_subsongs <= 0) {
            entry_subsongs = 1;
            U64_DEBUG("WARNING: Fixed subsongs to 1");
        }
        if (entry_current < 0 || entry_current >= entry_subsongs) {
            entry_current = 0;
            U64_DEBUG("WARNING: Fixed current subsong to 0");
        }

        /* Remove .sid extension for cleaner display */
        char clean_name[256];
        strncpy(clean_name, basename, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = '\0';
        char *dot = strrchr(clean_name, '.');
        if (dot && stricmp(dot, ".sid") == 0) {
            *dot = '\0';
        }

        /* Use title if available, otherwise use cleaned filename */
        char *display_name;
        if (entry->title && strlen(entry->title) > 0) {
            display_name = entry->title;
        } else {
            display_name = clean_name;
        }

        /* Allocate display string on heap */
        STRPTR list_string = AllocVec(512, MEMF_PUBLIC | MEMF_CLEAR);
        if (!list_string) {
            U64_DEBUG("ERROR: Failed to allocate display string");
            entry = entry->next;
            continue;
        }

        /* Build display string manually without sprintf */
        strcpy(list_string, display_name);

        if (entry_subsongs > 1) {
            /* Add subsong info: " [1/12]" */
            strcat(list_string, " [");

            /* Add current subsong number (1-based) */
            int display_current = entry_current + 1;
            if (display_current < 10) {
                char c = '0' + display_current;
                strncat(list_string, &c, 1);
            } else if (display_current < 100) {
                char temp[3];
                temp[0] = '0' + (display_current / 10);
                temp[1] = '0' + (display_current % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            } else {
                strcat(list_string, "99+"); /* Fallback for very high numbers */
            }

            strcat(list_string, "/");

            /* Add total subsongs */
            if (entry_subsongs < 10) {
                char c = '0' + entry_subsongs;
                strncat(list_string, &c, 1);
            } else if (entry_subsongs < 100) {
                char temp[3];
                temp[0] = '0' + (entry_subsongs / 10);
                temp[1] = '0' + (entry_subsongs % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            } else {
                strcat(list_string, "99+"); /* Fallback */
            }

            strcat(list_string, "] ");

            /* Get duration for current subsong */
            ULONG current_duration = FindSongLength(objApp, entry->md5, entry_current);
            if (current_duration == 0) {
                current_duration = (ULONG)entry_duration;
                if (current_duration == 0) {
                    current_duration = DEFAULT_SONG_LENGTH;
                }
            }

            /* Add time manually */
            int minutes = (int)(current_duration / 60);
            int seconds = (int)(current_duration % 60);

            /* Add minutes */
            if (minutes < 10) {
                char c = '0' + minutes;
                strncat(list_string, &c, 1);
            } else {
                char temp[3];
                temp[0] = '0' + (minutes / 10);
                temp[1] = '0' + (minutes % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            }

            strcat(list_string, ":");

            /* Add seconds (always 2 digits) */
            char temp[3];
            temp[0] = '0' + (seconds / 10);
            temp[1] = '0' + (seconds % 10);
            temp[2] = '\0';
            strcat(list_string, temp);
        } else {
            /* Single subsong: show just time */
            ULONG duration = (ULONG)entry_duration;
            if (duration == 0) {
                duration = FindSongLength(objApp, entry->md5, 0);
                if (duration == 0) {
                    duration = DEFAULT_SONG_LENGTH;
                }
            }

            strcat(list_string, " - ");

            /* Add time manually */
            int minutes = (int)(duration / 60);
            int seconds = (int)(duration % 60);

            /* Add minutes */
            if (minutes < 10) {
                char c = '0' + minutes;
                strncat(list_string, &c, 1);
            } else {
                char temp[3];
                temp[0] = '0' + (minutes / 10);
                temp[1] = '0' + (minutes % 10);
                temp[2] = '\0';
                strcat(list_string, temp);
            }

            strcat(list_string, ":");

            /* Add seconds (always 2 digits) */
            char temp[3];
            temp[0] = '0' + (seconds / 10);
            temp[1] = '0' + (seconds % 10);
            temp[2] = '\0';
            strcat(list_string, temp);
        }

        U64_DEBUG("Final display string: '%s'", list_string);

        /* Add to MUI list */
        DoMethod(objApp->LSV_PlaylistList, MUIM_List_InsertSingle, list_string,
                 MUIV_List_Insert_Bottom);

        U64_DEBUG("=== END ENTRY %d ===", (int)i);
        entry = entry->next;
    }

    set(objApp->LSV_PlaylistList, MUIA_List_Quiet, FALSE);

    /* Highlight current entry */
    if (objApp->current_entry) {
        set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
    }

    U64_DEBUG("=== APP_UpdatePlaylistDisplay FIXED END ===");
}

static void
APP_UpdateCurrentSongDisplay(void)
{
    char song_str[256];
    
    if (!objApp) return;

    U64_DEBUG("=== APP_UpdateCurrentSongDisplay FIXED START ===");

    if (!objApp->current_entry || !current_song_cache.cache_valid) {
        set(objApp->TXT_CurrentSong, MUIA_Text_Contents, "No song loaded");
        set(objApp->TXT_CurrentTime, MUIA_Text_Contents, "0:00");
        set(objApp->TXT_TotalTime, MUIA_Text_Contents, "0:00");
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, "");
        set(objApp->GAU_Progress, MUIA_Gauge_Current, 0);
        return;
    }

    /* Use cached values */
    int subsongs = (int)current_song_cache.cached_subsongs;
    int current = (int)current_song_cache.cached_current;

    U64_DEBUG("Values: subsongs=%d, current=%d", subsongs, current);
    U64_DEBUG("Times: current_time=%lu, total_time=%lu",
              (unsigned long)objApp->current_time, (unsigned long)objApp->total_time);

    /* Song title */
    if (current_song_cache.cached_title[0] != '\0') {
        strcpy(song_str, current_song_cache.cached_title);
    } else {
        char *basename = FilePart(current_song_cache.cached_filename);
        strcpy(song_str, basename);
        char *dot = strrchr(song_str, '.');
        if (dot && stricmp(dot, ".sid") == 0) {
            *dot = '\0';
        }
    }
    set(objApp->TXT_CurrentSong, MUIA_Text_Contents, song_str);

    /* Current time string */
    static char current_time_str[16];
    current_time_str[0] = '\0';

    int current_minutes = (int)(objApp->current_time / 60);
    int current_seconds = (int)(objApp->current_time % 60);

    /* Build current time manually */
    if (current_minutes < 10) {
        char c = '0' + current_minutes;
        strncat(current_time_str, &c, 1);
    } else {
        char temp[3];
        temp[0] = '0' + (current_minutes / 10);
        temp[1] = '0' + (current_minutes % 10);
        temp[2] = '\0';
        strcat(current_time_str, temp);
    }

    strcat(current_time_str, ":");

    /* Add seconds (always 2 digits) */
    char temp[3];
    temp[0] = '0' + (current_seconds / 10);
    temp[1] = '0' + (current_seconds % 10);
    temp[2] = '\0';
    strcat(current_time_str, temp);

    U64_DEBUG("Built current time string: '%s'", current_time_str);
    set(objApp->TXT_CurrentTime, MUIA_Text_Contents, current_time_str);

    /* Total time string */
    static char total_time_str[16];
    total_time_str[0] = '\0';

    int total_minutes = (int)(objApp->total_time / 60);
    int total_seconds = (int)(objApp->total_time % 60);

    /* Build total time manually */
    if (total_minutes < 10) {
        char c = '0' + total_minutes;
        strncat(total_time_str, &c, 1);
    } else {
        char temp2[3];
        temp2[0] = '0' + (total_minutes / 10);
        temp2[1] = '0' + (total_minutes % 10);
        temp2[2] = '\0';
        strcat(total_time_str, temp2);
    }

    strcat(total_time_str, ":");

    /* Add seconds (always 2 digits) */
    char temp3[3];
    temp3[0] = '0' + (total_seconds / 10);
    temp3[1] = '0' + (total_seconds % 10);
    temp3[2] = '\0';
    strcat(total_time_str, temp3);

    U64_DEBUG("Built total time string: '%s'", total_time_str);
    set(objApp->TXT_TotalTime, MUIA_Text_Contents, total_time_str);

    /* Subsong info - build manually */
    if (subsongs > 1) {
        /* Use static string to avoid stack issues */
        static char subsong_str[64];
        subsong_str[0] = '\0';

        strcat(subsong_str, "Subsong ");

        /* Add current subsong number (1-based) */
        int display_current = current + 1;
        if (display_current < 10) {
            char c = '0' + display_current;
            strncat(subsong_str, &c, 1);
        } else if (display_current < 100) {
            char temp4[3];
            temp4[0] = '0' + (display_current / 10);
            temp4[1] = '0' + (display_current % 10);
            temp4[2] = '\0';
            strcat(subsong_str, temp4);
        }

        strcat(subsong_str, " of ");

        /* Add total subsongs */
        if (subsongs < 10) {
            char c = '0' + subsongs;
            strncat(subsong_str, &c, 1);
        } else if (subsongs < 100) {
            char temp5[3];
            temp5[0] = '0' + (subsongs / 10);
            temp5[1] = '0' + (subsongs % 10);
            temp5[2] = '\0';
            strcat(subsong_str, temp5);
        }

        U64_DEBUG("Built subsong string: '%s'", subsong_str);
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, subsong_str);
    } else {
        set(objApp->TXT_SubsongInfo, MUIA_Text_Contents, "");
    }

    /* Progress gauge */
    if (objApp->total_time > 0) {
        ULONG progress = (objApp->current_time * 100) / objApp->total_time;
        if (progress > 100) progress = 100;
        set(objApp->GAU_Progress, MUIA_Gauge_Current, progress);
        U64_DEBUG("Progress: %lu%%", (unsigned long)progress);
    } else {
        set(objApp->GAU_Progress, MUIA_Gauge_Current, 0);
        U64_DEBUG("No total time, progress = 0");
    }

    U64_DEBUG("=== APP_UpdateCurrentSongDisplay FIXED END ===");
}
static BOOL AddPlaylistEntry(struct ObjApp *obj, CONST_STRPTR filename)
{
    UBYTE *file_data;
    ULONG file_size;
    PlaylistEntry *entry;
    PlaylistEntry *last;

    /* Load file to calculate MD5 and parse header */
    if (!LoadFile(filename, &file_data, &file_size)) {
        return FALSE;
    }

    /* Create new entry */
    entry = AllocVec(sizeof(PlaylistEntry), MEMF_PUBLIC | MEMF_CLEAR);
    if (!entry) {
        FreeVec(file_data);
        return FALSE;
    }

    /* Copy filename */
    entry->filename = SafeStrDup(filename);
    if (!entry->filename) {
        FreeVec(entry);
        FreeVec(file_data);
        return FALSE;
    }

    /* Calculate MD5 */
    CalculateMD5(file_data, file_size, entry->md5);

    /* Parse SID header for basic subsong count */
    UWORD header_subsongs = ParseSIDSubsongs(file_data, file_size);
    entry->subsongs = header_subsongs;
    entry->current_subsong = 0; /* Always start with first subsong */

    /* Check if we have this SID in our songlength database */
    if (obj->songlength_db) {
        SongLengthEntry *db_entry = obj->songlength_db;

        while (db_entry) {
            if (MD5Compare(db_entry->md5, entry->md5)) {
                U64_DEBUG("Found %s in database: %d subsongs (header said %d)",
                          FilePart(filename), db_entry->num_subsongs, header_subsongs);

                /* Use database value if it's larger and reasonable */
                if (db_entry->num_subsongs > header_subsongs
                    && db_entry->num_subsongs <= 256) {
                    entry->subsongs = db_entry->num_subsongs;
                }
                break;
            }
            db_entry = db_entry->next;
        }
    }

    /* Extract title */
    entry->title = ExtractSIDTitle(file_data, file_size);
    if (!entry->title) {
        /* Use filename as title */
        STRPTR basename = FilePart(filename);
        entry->title = SafeStrDup(basename);
    }

    /* Find song length for the first subsong (subsong 0) */
    entry->duration = FindSongLength(obj, entry->md5, 0);
    if (entry->duration == 0) {
        entry->duration = DEFAULT_SONG_LENGTH;
    }

    FreeVec(file_data);

    /* Add to playlist */
    if (!obj->playlist_head) {
        obj->playlist_head = entry;
    } else {
        last = obj->playlist_head;
        while (last->next) {
            last = last->next;
        }
        last->next = entry;
    }

    obj->playlist_count++;
    return TRUE;
}

static BOOL
PlayCurrentSong(struct ObjApp *obj)
{
    UBYTE *file_data;
    ULONG file_size;
    STRPTR error_details = NULL;
    LONG result;

    if (!obj->connection || !obj->current_entry) {
        return FALSE;
    }

    U64_DEBUG("=== PlayCurrentSong FIXED START ===");

    /* Update cache BEFORE doing anything else */
    APP_UpdateCurrentSongCache();

    /* Use cached values for safety */
    UWORD current_subsong = current_song_cache.cached_current;
    UWORD total_subsongs = current_song_cache.cached_subsongs;

    /* Ultimate64 expects subsong numbers starting from 1, not 0 */
    UBYTE ultimate64_subsong = current_subsong + 1;

    U64_DEBUG("PlayCurrentSong: Playing subsong %d (0-based) = %d (Ultimate64 1-based)",
              current_subsong, ultimate64_subsong);

    /* Load file */
    if (!LoadFile(obj->current_entry->filename, &file_data, &file_size)) {
        APP_UpdateStatus("Failed to load SID file");
        return FALSE;
    }

    /* Play SID with specific subsong (1-based for Ultimate64) */
    result = U64_PlaySID(obj->connection, file_data, file_size, ultimate64_subsong, &error_details);

    FreeVec(file_data);

    if (result != U64_OK) {
        char error_msg[512];
        /* Build error message manually */
        strcpy(error_msg, "SID playback failed: ");
        if (error_details) {
            strcat(error_msg, error_details);
        } else {
            strcat(error_msg, U64_GetErrorString(result));
        }
        APP_UpdateStatus(error_msg);

        if (error_details) {
            FreeVec(error_details);
        }
        return FALSE;
    }

    if (error_details) {
        FreeVec(error_details);
    }

    /* Set proper total time for current subsong */
    obj->state = PLAYER_PLAYING;
    obj->current_time = 0;

    /* Get duration for the specific subsong being played */
    obj->total_time = FindSongLength(obj, obj->current_entry->md5, current_subsong);

    U64_DEBUG("Set total_time to %lu seconds for subsong %d",
              (unsigned long)obj->total_time, current_subsong);

    obj->timer_counter = 0;

    /* Update display using cached values */
    APP_UpdateCurrentSongDisplay();

    char *basename = FilePart(current_song_cache.cached_filename);

    /* Use static string to avoid stack corruption */
    static char status_msg[256];
    strcpy(status_msg, "Playing ");
    strcat(status_msg, basename);
    strcat(status_msg, " [");

    /* Add current subsong (1-based) manually */
    int display_current = (int)current_subsong + 1;
    if (display_current < 10) {
        char c = '0' + display_current;
        strncat(status_msg, &c, 1);
    } else if (display_current < 100) {
        char temp[3];
        temp[0] = '0' + (display_current / 10);
        temp[1] = '0' + (display_current % 10);
        temp[2] = '\0';
        strcat(status_msg, temp);
    }

    strcat(status_msg, "/");

    /* Add total subsongs manually */
    int display_total = (int)total_subsongs;
    if (display_total < 10) {
        char c = '0' + display_total;
        strncat(status_msg, &c, 1);
    } else if (display_total < 100) {
        char temp[3];
        temp[0] = '0' + (display_total / 10);
        temp[1] = '0' + (display_total % 10);
        temp[2] = '\0';
        strcat(status_msg, temp);
    }

    strcat(status_msg, "]");

    U64_DEBUG("Status message built: '%s'", status_msg);
    APP_UpdateStatus(status_msg);

    U64_DEBUG("=== PlayCurrentSong FIXED END ===");
    return TRUE;
}

static BOOL APP_Connect(void)
{
    if (!objApp) return FALSE;

    if (objApp->connection) {
        /* Disconnect */
        if (objApp->state == PLAYER_PLAYING) {
            objApp->state = PLAYER_STOPPED;
        }

        U64_Disconnect(objApp->connection);
        objApp->connection = NULL;

        set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Disconnected");
        set(objApp->BTN_Connect, MUIA_Text_Contents, "Connect");
        APP_UpdateStatus("Disconnected");

        /* Disable controls */
        set(objApp->BTN_Play, MUIA_Disabled, TRUE);
        set(objApp->BTN_Stop, MUIA_Disabled, TRUE);
        set(objApp->BTN_Next, MUIA_Disabled, TRUE);
        set(objApp->BTN_Prev, MUIA_Disabled, TRUE);
    } else {
        /* Check if configuration is complete */
        if (strlen(objApp->host) == 0) {
            APP_UpdateStatus("Please configure connection settings first");
            APP_ConfigOpen();
            return FALSE;
        }

        /* Connect */
        objApp->connection = U64_Connect(objApp->host,
            strlen(objApp->password) > 0 ? objApp->password : NULL);

        if (objApp->connection) {
            char status[256];
            sprintf(status, "Connected to %s", objApp->host);
            set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Connected");
            set(objApp->BTN_Connect, MUIA_Text_Contents, "Disconnect");
            APP_UpdateStatus(status);

            /* Enable controls */
            set(objApp->BTN_Play, MUIA_Disabled, FALSE);
            set(objApp->BTN_Stop, MUIA_Disabled, FALSE);
            set(objApp->BTN_Next, MUIA_Disabled, FALSE);
            set(objApp->BTN_Prev, MUIA_Disabled, FALSE);
        } else {
            set(objApp->TXT_ConnectionStatus, MUIA_Text_Contents, "Disconnected");
            APP_UpdateStatus("Connection failed");
        }
    }

    return TRUE;
}

static BOOL APP_AddFiles(void)
{
    struct FileRequester *req = NULL;
    BOOL success = FALSE;
    ULONG added = 0;
    BOOL use_remembered_dir = FALSE;

    if (!AslBase) {
        APP_UpdateStatus("ASL library not available");
        return FALSE;
    }

    /* Check remembered directory */
    if (strlen(objApp->last_sid_dir) > 0) {
        BPTR lock = Lock(objApp->last_sid_dir, ACCESS_READ);
        if (lock) {
            UnLock(lock);
            use_remembered_dir = TRUE;
        } else {
            objApp->last_sid_dir[0] = '\0';
        }
    }

    /* Create ASL requester */
    if (use_remembered_dir) {
        req = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_TitleText, "Select SID files to add",
            ASLFR_DoPatterns, TRUE,
            ASLFR_InitialPattern, "#?.sid",
            ASLFR_DoMultiSelect, TRUE,
            ASLFR_InitialDrawer, objApp->last_sid_dir,
            ASLFR_RejectIcons, TRUE,
            TAG_DONE);
    } else {
        req = AllocAslRequestTags(ASL_FileRequest,
            ASLFR_TitleText, "Select SID files to add",
            ASLFR_DoPatterns, TRUE,
            ASLFR_InitialPattern, "#?.sid",
            ASLFR_DoMultiSelect, TRUE,
            ASLFR_RejectIcons, TRUE,
            TAG_DONE);
    }

    if (!req) {
        APP_UpdateStatus("Failed to create file requester");
        return FALSE;
    }

    /* Show ASL requester */
    success = AslRequest(req, NULL);

    /* Process results */
    if (success) {
        struct WBArg *args = req->rf_ArgList;
        LONG num_args = req->rf_NumArgs;

        if (args && num_args > 0 && num_args <= 1000) {
            /* Remember directory */
            if (req->rf_Dir && strlen(req->rf_Dir) > 0) {
                strncpy(objApp->last_sid_dir, req->rf_Dir, sizeof(objApp->last_sid_dir) - 1);
                objApp->last_sid_dir[sizeof(objApp->last_sid_dir) - 1] = '\0';
            }

            for (LONG i = 0; i < num_args; i++) {
                char filename[512];
                
                if (args[i].wa_Lock && args[i].wa_Name) {
                    if (NameFromLock(args[i].wa_Lock, filename, sizeof(filename) - 32)) {
                        if (AddPart(filename, args[i].wa_Name, sizeof(filename))) {
                            if (AddPlaylistEntry(objApp, filename)) {
                                added++;
                            }
                        }
                    }
                }
                
                if ((i % 10) == 0) {
                    Delay(1);
                }
            }
        }
    }

    if (req) {
        FreeAslRequest(req);
    }

    /* Update displays */
    if (success && added > 0) {
        APP_UpdatePlaylistDisplay();
        
        if (!objApp->current_entry && objApp->playlist_head) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
            objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5, 
                                                             objApp->current_entry->current_subsong);
            if (objApp->current_entry->duration == 0) {
                objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
            }
            APP_UpdateCurrentSongCache();
            APP_UpdateCurrentSongDisplay();
        }

        char status_msg[256];
        sprintf(status_msg, "Added %lu files to playlist", (unsigned int)added);
        APP_UpdateStatus(status_msg);
    } else if (success) {
        APP_UpdateStatus("No valid SID files found");
    } else {
        APP_UpdateStatus("File selection cancelled");
    }

    return success;
}

static BOOL APP_RemoveFile(void)
{
    LONG active;

    if (!objApp) return FALSE;

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &active);

    if (active == MUIV_List_Active_Off || active < 0) {
        APP_UpdateStatus("No file selected");
        return FALSE;
    }

    /* Find entry to remove */
    PlaylistEntry *entry = objApp->playlist_head;
    PlaylistEntry *prev = NULL;

    for (ULONG i = 0; i < (ULONG)active && entry; i++) {
        prev = entry;
        entry = entry->next;
    }

    if (!entry) {
        return FALSE;
    }

    /* Stop playback if removing current song */
    if (entry == objApp->current_entry) {
        if (objApp->state == PLAYER_PLAYING) {
            objApp->state = PLAYER_STOPPED;
        }
        objApp->current_entry = entry->next;
        if (!objApp->current_entry && objApp->playlist_head != entry) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
        }
    }

    /* Remove from list */
    if (prev) {
        prev->next = entry->next;
    } else {
        objApp->playlist_head = entry->next;
    }

    /* Adjust current index */
    if (objApp->current_index > (ULONG)active) {
        objApp->current_index--;
    }

    /* Free entry */
    SafeStrFree(entry->filename);
    SafeStrFree(entry->title);
    FreeVec(entry);

    objApp->playlist_count--;

    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();
    APP_UpdateStatus("File removed from playlist");

    return TRUE;
}

static BOOL APP_ClearPlaylist(void)
{
    if (!objApp) return FALSE;

    /* Stop playback */
    if (objApp->state == PLAYER_PLAYING) {
        objApp->state = PLAYER_STOPPED;
    }

    FreePlaylists(objApp);
    APP_UpdatePlaylistDisplay();
    APP_UpdateCurrentSongDisplay();
    APP_UpdateStatus("Playlist cleared");

    return TRUE;
}

static BOOL APP_Play(void)
{
    LONG selected_index;

    if (!objApp) return FALSE;

    if (objApp->playlist_count == 0) {
        APP_UpdateStatus("No songs in playlist");
        return FALSE;
    }

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &selected_index);

    if (objApp->current_entry) {
        set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
        
        APP_UpdateCurrentSongCache();
        if (PlayCurrentSong(objApp)) {
            /* NEW: Start periodic timer */
            StartPeriodicTimer();
            
            APP_UpdatePlaylistDisplay();
            return TRUE;
        } else {
            return FALSE;
        }
    }
    else if (objApp->playlist_head) {
        objApp->current_entry = objApp->playlist_head;
        objApp->current_index = 0;

        set(objApp->LSV_PlaylistList, MUIA_List_Active, 0);

        APP_UpdateCurrentSongCache();
        if (PlayCurrentSong(objApp)) {
            /* NEW: Start periodic timer */
            StartPeriodicTimer();
            
            APP_UpdatePlaylistDisplay();
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        APP_UpdateStatus("No songs in playlist");
        return FALSE;
    }
}

static BOOL APP_Stop(void)
{
    LONG result;
    
    if (!objApp) return FALSE;

    if (objApp->state == PLAYER_PLAYING) {
        objApp->state = PLAYER_STOPPED;
        objApp->current_time = 0;
        
        /* NEW: Stop periodic timer */
        StopPeriodicTimer();
        
        /* NEW: Reset the Ultimate64 when stopping playback */
        if (objApp->connection) {
            result = U64_Reset(objApp->connection);
            if (result != U64_OK) {
                static char error_msg[256];
                strcpy(error_msg, "Reset failed: ");
                strcat(error_msg, U64_GetErrorString(result));
                APP_UpdateStatus(error_msg);
                U64_DEBUG("Reset failed: %s", U64_GetErrorString(result));
            } else {
                U64_DEBUG("Reset successful after stop");
            }
        }
        
        APP_UpdateCurrentSongDisplay();
        APP_UpdateStatus("Stopped");
    }

    return TRUE;
}

static BOOL APP_Next(void)
{
    if (!objApp || !objApp->current_entry) {
        return FALSE;
    }

    /* Check if we have more subsongs */
    if (objApp->current_entry->current_subsong + 1 < objApp->current_entry->subsongs) {
        objApp->current_entry->current_subsong++;
        objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5, 
                                                         objApp->current_entry->current_subsong);
        if (objApp->current_entry->duration == 0) {
            objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

        APP_UpdateCurrentSongCache();
        APP_UpdatePlaylistDisplay();
        
        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        } else {
            APP_UpdateCurrentSongDisplay();
        }
        return TRUE;
    }

    /* Reset current subsong and move to next entry */
    objApp->current_entry->current_subsong = 0;

    /* Move to next entry */
    if (objApp->shuffle_mode) {
        /* Random selection */
        if (objApp->playlist_count > 1) {
            ULONG random_index;
            PlaylistEntry *entry;

            do {
                random_index = rand() % objApp->playlist_count;
            } while (random_index == objApp->current_index && objApp->playlist_count > 1);

            entry = objApp->playlist_head;
            for (ULONG i = 0; i < random_index && entry; i++) {
                entry = entry->next;
            }

            objApp->current_entry = entry;
            objApp->current_index = random_index;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, random_index);
        }
    } else {
        /* Sequential */
        if (objApp->current_entry->next) {
            objApp->current_entry = objApp->current_entry->next;
            objApp->current_index++;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
        } else if (objApp->repeat_mode) {
            objApp->current_entry = objApp->playlist_head;
            objApp->current_index = 0;
            set(objApp->LSV_PlaylistList, MUIA_List_Active, 0);
        } else {
            LONG result;
            
            objApp->state = PLAYER_STOPPED;
            
            /* NEW: Reset the Ultimate64 when playlist ends */
            if (objApp->connection) {
                result = U64_Reset(objApp->connection);
                if (result != U64_OK) {
                    static char error_msg[256];
                    strcpy(error_msg, "Reset failed at playlist end: ");
                    strcat(error_msg, U64_GetErrorString(result));
                    APP_UpdateStatus(error_msg);
                    U64_DEBUG("Reset failed at playlist end: %s", U64_GetErrorString(result));
                } else {
                    U64_DEBUG("Reset successful at playlist end");
                    APP_UpdateStatus("Playlist finished - Reset complete");
                }
            } else {
                APP_UpdateStatus("Playlist finished");
            }
            
            APP_UpdateCurrentSongDisplay();
            return TRUE;

        }
    }

    /* Update duration for new song */
    objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5, 
                                                     objApp->current_entry->current_subsong);
    if (objApp->current_entry->duration == 0) {
        objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
    }

    APP_UpdateCurrentSongCache();
    APP_UpdatePlaylistDisplay();

    if (objApp->state == PLAYER_PLAYING) {
        PlayCurrentSong(objApp);
    } else {
        APP_UpdateCurrentSongDisplay();
    }

    return TRUE;
}

static BOOL APP_Prev(void)
{
    if (!objApp || !objApp->current_entry) {
        return FALSE;
    }

    /* If we're more than 3 seconds into the song, restart current song */
    if (objApp->current_time > 3) {
        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        }
        return TRUE;
    }

    /* Check if we can go to previous subsong */
    if (objApp->current_entry->current_subsong > 0) {
        objApp->current_entry->current_subsong--;
        objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5, 
                                                         objApp->current_entry->current_subsong);
        if (objApp->current_entry->duration == 0) {
            objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

        APP_UpdateCurrentSongCache();
        APP_UpdatePlaylistDisplay();

        if (objApp->state == PLAYER_PLAYING) {
            PlayCurrentSong(objApp);
        } else {
            APP_UpdateCurrentSongDisplay();
        }
        return TRUE;
    }

    /* Move to previous entry */
    if (objApp->current_index > 0) {
        PlaylistEntry *entry = objApp->playlist_head;

        for (ULONG i = 0; i < objApp->current_index - 1 && entry; i++) {
            entry = entry->next;
        }

        if (entry) {
            objApp->current_entry = entry;
            objApp->current_index--;

            /* Set to last subsong */
            objApp->current_entry->current_subsong = objApp->current_entry->subsongs - 1;
            objApp->current_entry->duration = FindSongLength(objApp, objApp->current_entry->md5, 
                                                             objApp->current_entry->current_subsong);
            if (objApp->current_entry->duration == 0) {
                objApp->current_entry->duration = DEFAULT_SONG_LENGTH;
            }

            set(objApp->LSV_PlaylistList, MUIA_List_Active, objApp->current_index);
            APP_UpdateCurrentSongCache();
            APP_UpdatePlaylistDisplay();

            if (objApp->state == PLAYER_PLAYING) {
                PlayCurrentSong(objApp);
            } else {
                APP_UpdateCurrentSongDisplay();
            }
        }
    }

    return TRUE;
}

static BOOL APP_LoadSongLengths(void)
{
    struct FileRequester *req;

    if (!AslBase) {
        APP_UpdateStatus("ASL library not available");
        return FALSE;
    }

    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, "Select Songlengths.md5 file",
        ASLFR_DoPatterns, TRUE,
        ASLFR_InitialPattern, "#?.md5",
        TAG_DONE);

    if (req && AslRequest(req, NULL)) {
        char filename[512];
        strcpy(filename, req->rf_Dir);
        AddPart(filename, req->rf_File, sizeof(filename));

        U64_DEBUG("=== Manual LoadSongLengths START ===");

        /* Disable button during loading */
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Loading...");

        if (LoadSongLengthsWithProgress(objApp, filename)) {
            /* Update existing playlist entries with progress feedback */
            if (objApp->playlist_count > 0) {
                PlaylistEntry *entry = objApp->playlist_head;
                ULONG updated = 0;
                ULONG entry_num = 0;

                APP_UpdateStatus("Updating playlist entries...");

                while (entry) {
                    entry_num++;

                    /* Update progress */
                    if ((entry_num % 5) == 0) {
                        char progress_msg[256];
                        sprintf(progress_msg, "Updating playlist... %u/%u",
                                (unsigned int)entry_num,
                                (unsigned int)objApp->playlist_count);
                        APP_UpdateStatus(progress_msg);

                        /* Process MUI events */
                        ULONG signals;
                        DoMethod(objApp->App, MUIM_Application_Input, &signals);
                    }

                    /* Update subsong count from database */
                    SongLengthEntry *db_entry = objApp->songlength_db;

                    while (db_entry) {
                        if (MD5Compare(db_entry->md5, entry->md5)) {
                            if (db_entry->num_subsongs > entry->subsongs
                                && db_entry->num_subsongs <= 256) {
                                entry->subsongs = db_entry->num_subsongs;
                                updated++;
                            }
                            entry->duration = FindSongLength(objApp, entry->md5, entry->current_subsong);
                            break;
                        }
                        db_entry = db_entry->next;
                    }

                    entry = entry->next;
                }

                /* Update displays */
                APP_UpdatePlaylistDisplay();
                if (objApp->current_entry) {
                    APP_UpdateCurrentSongCache();
                    APP_UpdateCurrentSongDisplay();
                }

                char final_msg[256];
                sprintf(final_msg,
                        "Database loaded. Updated %u of %u playlist entries",
                        (unsigned int)updated,
                        (unsigned int)objApp->playlist_count);
                APP_UpdateStatus(final_msg);
            }
        }

        /* Re-enable button */
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Load Songlengths");
    }

    if (req) {
        FreeAslRequest(req);
    }

    return TRUE;
}

static BOOL APP_About(void)
{
    if (!objApp) return FALSE;

    MUI_RequestA(objApp->App, 
                objApp->WIN_Main, 
                0, 
                NULL,
                "Continue",
                "\33c\0338Ultimate64 SID Player v1.0\n\n"
                "\0332Playlist SID Player for Ultimate64\n\n"
                "2025 Marcin Spoczynski",
                NULL);
    return TRUE;
}

static BOOL APP_ConfigOpen(void)
{
    if (!objApp || !objApp->WIN_Config) return FALSE;

    /* Update string gadgets with current values */
    set(objApp->STR_ConfigHost, MUIA_String_Contents, objApp->host);
    set(objApp->STR_ConfigPassword, MUIA_String_Contents, objApp->password);
    
    /* Open the window */
    set(objApp->WIN_Config, MUIA_Window_Open, TRUE);
    return TRUE;
}

static BOOL APP_ConfigOK(void)
{
    STRPTR host_str, password_str;
    
    if (!objApp || !objApp->WIN_Config) return FALSE;
    
    /* Get values from string gadgets */
    get(objApp->STR_ConfigHost, MUIA_String_Contents, &host_str);
    get(objApp->STR_ConfigPassword, MUIA_String_Contents, &password_str);
    
    /* Copy values */
    if (host_str) {
        strncpy(objApp->host, host_str, sizeof(objApp->host) - 1);
        objApp->host[sizeof(objApp->host) - 1] = '\0';
    }
    
    if (password_str) {
        strncpy(objApp->password, password_str, sizeof(objApp->password) - 1);
        objApp->password[sizeof(objApp->password) - 1] = '\0';
    }
    
    /* Save configuration */
    SaveConfig(objApp);
    
    /* Close window */
    set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
    APP_UpdateStatus("Configuration saved");
    
    return TRUE;
}

static BOOL APP_ConfigCancel(void)
{
    if (!objApp || !objApp->WIN_Config) return FALSE;
    
    set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
    return TRUE;
}

static BOOL
APP_PlaylistDoubleClick(void)
{
    LONG active;

    get(objApp->LSV_PlaylistList, MUIA_List_Active, &active);

    U64_DEBUG("=== APP_PlaylistDoubleClick (DOUBLE CLICK) ===");
    U64_DEBUG("Double-clicked index: %ld", active);

    if (active == MUIV_List_Active_Off || active < 0) {
        U64_DEBUG("No valid selection for double-click");
        return FALSE;
    }

    /* Find the entry */
    PlaylistEntry *entry = objApp->playlist_head;
    for (ULONG i = 0; i < (ULONG)active && entry; i++) {
        entry = entry->next;
    }

    if (entry) {
        U64_DEBUG("Double-clicked on: %s", FilePart(entry->filename));

        objApp->current_entry = entry;
        objApp->current_index = (ULONG)active;

        /* Update cache immediately */
        APP_UpdateCurrentSongCache();

        /* ALWAYS start playing on double-click, regardless of current state */
        U64_DEBUG("Starting playback due to double-click");
        if (PlayCurrentSong(objApp)) {
            /* Update playlist display to show current selection */
            APP_UpdatePlaylistDisplay();
            U64_DEBUG("Successfully started playing double-clicked song");
        } else {
            U64_DEBUG("Failed to start playing double-clicked song");
        }
    } else {
        U64_DEBUG("ERROR: Could not find double-clicked entry");
    }

    return TRUE;
}
static BOOL
APP_PlaylistActive(void)
{
    LONG index;
    
    if (!objApp) return FALSE;
    
    get(objApp->LSV_PlaylistList, MUIA_List_Active, &index);
    
    U64_DEBUG("=== APP_PlaylistActive (SINGLE CLICK) ===");
    U64_DEBUG("Selected index: %ld", index);
    
    if (index == MUIV_List_Active_Off || index < 0) {
        U64_DEBUG("No valid selection");
        return FALSE;
    }

    /* Find the selected entry */
    PlaylistEntry *entry = objApp->playlist_head;
    for (ULONG i = 0; i < (ULONG)index && entry; i++) {
        entry = entry->next;
    }

    if (entry) {
        U64_DEBUG("Single-clicked on: %s", FilePart(entry->filename));
        
        /* ONLY update current entry - do NOT start playing */
        objApp->current_entry = entry;
        objApp->current_index = (ULONG)index;
        
        /* Update cache and display */
        APP_UpdateCurrentSongCache();
        APP_UpdateCurrentSongDisplay();
        
        /* Build status message manually - show selection, not playing */
        static char status_msg[256];
        strcpy(status_msg, "Selected: ");
        
        char *basename = FilePart(entry->filename);
        strcat(status_msg, basename);
        
        if (entry->subsongs > 1) {
            strcat(status_msg, " [");
            
            /* Add current subsong (1-based) */
            int display_current = (int)entry->current_subsong + 1;
            if (display_current < 10) {
                char c = '0' + display_current;
                strncat(status_msg, &c, 1);
            } else if (display_current < 100) {
                char temp[3];
                temp[0] = '0' + (display_current / 10);
                temp[1] = '0' + (display_current % 10);
                temp[2] = '\0';
                strcat(status_msg, temp);
            }
            
            strcat(status_msg, "/");
            
            /* Add total subsongs */
            int display_total = (int)entry->subsongs;
            if (display_total < 10) {
                char c = '0' + display_total;
                strncat(status_msg, &c, 1);
            } else if (display_total < 100) {
                char temp[3];
                temp[0] = '0' + (display_total / 10);
                temp[1] = '0' + (display_total % 10);
                temp[2] = '\0';
                strcat(status_msg, temp);
            }
            
            strcat(status_msg, "]");
        }
        
        APP_UpdateStatus(status_msg);
        U64_DEBUG("Updated selection without playing");
    } else {
        U64_DEBUG("ERROR: Could not find selected entry");
    }

    return TRUE;
}

static void APP_TimerUpdate(void)
{
    if (!objApp) return;

    if (objApp->state == PLAYER_PLAYING) {
        objApp->timer_counter++;

        /* Update every 10 cycles (approximately 1 second) */
        if (objApp->timer_counter >= 10) {
            objApp->timer_counter = 0;
            objApp->current_time++;
            APP_UpdateCurrentSongDisplay();

            /* Check if current subsong is finished */
            if (objApp->current_time >= objApp->total_time) {
                APP_Next(); /* Auto-advance to next subsong/song */
            }
        }
    }
}

/* Main function */
int main(void)
{
    int result = RETURN_FAIL;
    BOOL running = TRUE;
    ULONG signals = 0;

    if (!InitLibs()) {
        return 20;
    }

    if ((objApp = CreateApp())) {
        if (!APP_Init()) {
            U64_DEBUG("Failed to initialize application");
            DisposeApp(objApp);
            CleanupLibs();
            return RETURN_FAIL;
        }

        /* Initialize current_playlist_file */
        objApp->current_playlist_file[0] = '\0';

        LoadConfig(objApp);
        
        set(objApp->BTN_Play, MUIA_Disabled, TRUE);
        set(objApp->BTN_Stop, MUIA_Disabled, TRUE);
        set(objApp->BTN_Next, MUIA_Disabled, TRUE);
        set(objApp->BTN_Prev, MUIA_Disabled, TRUE);

        DoMethod(objApp->WIN_Main, MUIM_Set, MUIA_Window_Open, TRUE);

        ULONG isOpen = FALSE;
        get(objApp->WIN_Main, MUIA_Window_Open, &isOpen);
        if (!isOpen) {
            running = FALSE;
        } else {
            APP_UpdateStatus("Ready - Configure connection to get started");
        }

        AutoLoadSongLengths(objApp);

        while (running) {
           signals = 0; 
            ULONG id = DoMethod(objApp->App, MUIM_Application_NewInput, &signals);


            if (id > 0) {
                switch (id) {
                    case EVENT_CONNECT: APP_Connect(); break;
                    case EVENT_ADD_FILE: APP_AddFiles(); break;
                    case EVENT_REMOVE_FILE: APP_RemoveFile(); break;
                    case EVENT_CLEAR_PLAYLIST: APP_ClearPlaylist(); break;
                    case EVENT_PLAY: APP_Play(); break;
                    case EVENT_STOP: APP_Stop(); break;
                    case EVENT_NEXT: APP_Next(); break;
                    case EVENT_PREV: APP_Prev(); break;
                    case EVENT_SHUFFLE:
                        get(objApp->CHK_Shuffle, MUIA_Selected, &objApp->shuffle_mode);
                        APP_UpdateStatus(objApp->shuffle_mode ? "Shuffle enabled" : "Shuffle disabled");
                        break;
                    case EVENT_REPEAT:
                        get(objApp->CHK_Repeat, MUIA_Selected, &objApp->repeat_mode);
                        APP_UpdateStatus(objApp->repeat_mode ? "Repeat enabled" : "Repeat disabled");
                        break;
                    case EVENT_DOWNLOAD_SONGLENGTHS: APP_DownloadSongLengths(); break;
                    case EVENT_ABOUT: APP_About(); break;
                    case EVENT_CONFIG_OPEN: APP_ConfigOpen(); break;
                    case EVENT_CONFIG_OK: APP_ConfigOK(); break;
                    case EVENT_CONFIG_CANCEL: APP_ConfigCancel(); break;
                    case EVENT_PLAYLIST_ACTIVE: APP_PlaylistActive(); break;
                    case EVENT_PLAYLIST_DCLICK: APP_PlaylistDoubleClick(); break;
                    case EVENT_PLAYLIST_LOAD: APP_PlaylistLoad(); break;
                    case EVENT_PLAYLIST_SAVE: APP_PlaylistSave(); break;
                    case EVENT_PLAYLIST_SAVE_AS: APP_PlaylistSaveAs(); break;
                    case MUIV_Application_ReturnID_Quit: running = FALSE; break;
                }
            }
            
            if (id == MUIV_Application_ReturnID_Quit) {
                running = FALSE;
                break;
            }

            // Build wait signal mask - include timer if running
            ULONG waitSignals = signals | SIGBREAKF_CTRL_C;
            if (TimerRunning && TimerSig) {
                waitSignals |= TimerSig;
            }

            // Only wait if we have signals to wait for
            if (waitSignals & ~SIGBREAKF_CTRL_C) {  // If more than just CTRL_C
                ULONG receivedSignals = Wait(waitSignals);
                
                if (receivedSignals & SIGBREAKF_CTRL_C) {
                    running = FALSE;
                    break;
                }
                CheckTimerSignal(receivedSignals);
            }
        }

        set(objApp->WIN_Main, MUIA_Window_Open, FALSE);
        if (objApp->WIN_Config) {
            set(objApp->WIN_Config, MUIA_Window_Open, FALSE);
        }
        
        SaveConfig(objApp);
        DisposeApp(objApp);
        result = RETURN_OK;
    }

    U64_CleanupLibrary();
    CleanupLibs();
    
    return result;
}
