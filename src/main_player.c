/* Ultimate64 SID Player with Playlist
 * For Amiga OS 3.x by Marcin Spoczynski
 * Fixed version based on working MUI implementation
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

/* Version string */
static const char version[] = "$VER: u64sidplayer 1.0 (2025)";

/* MUI Object creation macros */
#define ApplicationObject   MUI_NewObject(MUIC_Application
#define WindowObject        MUI_NewObject(MUIC_Window
#define VGroup              MUI_NewObject(MUIC_Group
#define HGroup              MUI_NewObject(MUIC_Group, MUIA_Group_Horiz, TRUE
#define GroupObject         MUI_NewObject(MUIC_Group
#define StringObject        MUI_NewObject(MUIC_String
#define TextObject          MUI_NewObject(MUIC_Text
#define ListviewObject      MUI_NewObject(MUIC_Listview
#define GaugeObject         MUI_NewObject(MUIC_Gauge
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

/* Constants */
#define DEFAULT_SONG_LENGTH 300 /* 5 minutes in seconds */
#define MAX_PLAYLIST_ENTRIES 1000
#define MD5_HASH_SIZE 16
#define MD5_STRING_SIZE 33 /* 32 hex chars + null terminator */

/* Environment variable names */
#define ENV_ULTIMATE64_HOST "Ultimate64/Host"
#define ENV_ULTIMATE64_PASSWORD "Ultimate64/Password"
#define ENV_ULTIMATE64_SID_DIR "Ultimate64/SidDir"

#ifdef DEBUG
#define DPRINTF(fmt, ...) DPRINTF (fmt, ##__VA_ARGS__)
#else
#define DPRINTF(fmt, ...) /* nothing */
#endif

/* MUI IDs */
enum
{
  ID_CONNECT = 1,
  ID_ADD_FILE,
  ID_REMOVE_FILE,
  ID_CLEAR_PLAYLIST,
  ID_PLAY,
  ID_STOP,
  ID_NEXT,
  ID_PREV,
  ID_SHUFFLE,
  ID_REPEAT,
  ID_LOAD_SONGLENGTHS,
  ID_QUIT,
  ID_ABOUT,
  ID_PLAYLIST_DCLICK,
  ID_CONFIG_OPEN,
  ID_CONFIG_OK,
  ID_CONFIG_CANCEL,
  ID_CONFIG_CLEAR,
  ID_TIMER_UPDATE
};

/* Playlist entry structure */
typedef struct PlaylistEntry
{
  STRPTR filename;
  STRPTR title;
  UBYTE md5[MD5_HASH_SIZE];
  ULONG duration; /* in seconds, 0 = unknown */
  UWORD subsongs; /* Move UWORD fields together for better alignment */
  UWORD current_subsong;
  ULONG _padding; /* Add padding to ensure proper alignment */
  struct PlaylistEntry *next;
} PlaylistEntry;

/* Global variables to cache current song info - this prevents corruption */
static struct
{
  UWORD cached_subsongs;
  UWORD cached_current;
  ULONG cached_duration;
  char cached_title[256];
  char cached_filename[256];
  BOOL cache_valid;
} current_song_cache = { 0, 0, 0, "", "", FALSE };

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
struct SIDPlayerData
{
  Object *app;
  Object *window;

  /* Configuration window */
  Object *config_window;
  Object *config_host_string;
  Object *config_password_string;

  /* Connection */
  Object *btn_connect;
  Object *btn_load_songlengths;
  Object *txt_status;
  Object *txt_connection_status;

  /* Playlist */
  Object *playlist_list;
  Object *btn_add_file;
  Object *btn_remove_file;
  Object *btn_clear_playlist;

  /* Player controls */
  Object *btn_play;
  Object *btn_stop;
  Object *btn_next;
  Object *btn_prev;
  Object *btn_shuffle;
  Object *btn_repeat;

  /* Current song info */
  Object *txt_current_song;
  Object *txt_current_time;
  Object *txt_total_time;
  Object *progress_gauge;
  Object *txt_subsong_info;

  /* Configuration strings */
  char host[256];
  char password[256];
  char last_sid_dir[256];

  /* Data */
  U64Connection *connection;
  PlaylistEntry *playlist_head;
  PlaylistEntry *current_entry;
  SongLengthEntry *songlength_db;
  ULONG playlist_count;
  ULONG current_index;

  /* Player state */
  PlayerState state;
  ULONG current_time; /* Current playback time in seconds */
  ULONG total_time;   /* Total song time in seconds */
  BOOL shuffle_mode;
  BOOL repeat_mode;
  ULONG timer_counter; /* Simple counter for timing */
};

/* Library bases */
struct Library *MUIMasterBase = NULL;

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

/* Simple MD5 implementation for SID files */
typedef struct
{
  ULONG state[4];
  ULONG count[2];
  UBYTE buffer[64];
} MD5_CTX;

static char global_subsong_str[64] = "";

/* Forward declarations to fix compilation errors */
static void UpdateStatus (struct SIDPlayerData *data, CONST_STRPTR text);
static void UpdatePlaylistDisplay (struct SIDPlayerData *data);
static void UpdateCurrentSongDisplay (struct SIDPlayerData *data);
static ULONG ParseTimeString (const char *time_str);
static void FreeSongLengthDB (struct SIDPlayerData *data);
static void ClearSidDirectoryPref (struct SIDPlayerData *data);
static ULONG FindSongLength (struct SIDPlayerData *data,
                             const UBYTE md5[MD5_HASH_SIZE], UWORD subsong);
/* MD5 function prototypes */
static void MD5Init (MD5_CTX *ctx);
static void MD5Update (MD5_CTX *ctx, const UBYTE *input, ULONG inputLen);
static void MD5Final (UBYTE digest[MD5_HASH_SIZE], MD5_CTX *ctx);
static void MD5Transform (ULONG state[4], const UBYTE block[64]);

/* MD5 constants */
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

/* MD5 auxiliary functions */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac)                                              \
  {                                                                           \
    (a) += F ((b), (c), (d)) + (x) + (ULONG)(ac);                             \
    (a) = ROTATE_LEFT ((a), (s));                                             \
    (a) += (b);                                                               \
  }
#define GG(a, b, c, d, x, s, ac)                                              \
  {                                                                           \
    (a) += G ((b), (c), (d)) + (x) + (ULONG)(ac);                             \
    (a) = ROTATE_LEFT ((a), (s));                                             \
    (a) += (b);                                                               \
  }
#define HH(a, b, c, d, x, s, ac)                                              \
  {                                                                           \
    (a) += H ((b), (c), (d)) + (x) + (ULONG)(ac);                             \
    (a) = ROTATE_LEFT ((a), (s));                                             \
    (a) += (b);                                                               \
  }
#define II(a, b, c, d, x, s, ac)                                              \
  {                                                                           \
    (a) += I ((b), (c), (d)) + (x) + (ULONG)(ac);                             \
    (a) = ROTATE_LEFT ((a), (s));                                             \
    (a) += (b);                                                               \
  }

/* MD5 implementation */
static UBYTE PADDING[64]
    = { 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static void
MD5Init (MD5_CTX *ctx)
{
  ctx->count[0] = ctx->count[1] = 0;
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xefcdab89;
  ctx->state[2] = 0x98badcfe;
  ctx->state[3] = 0x10325476;
}

static void
MD5Update (MD5_CTX *ctx, const UBYTE *input, ULONG inputLen)
{
  ULONG i, index, partLen;

  index = (ULONG)((ctx->count[0] >> 3) & 0x3F);

  if ((ctx->count[0] += ((ULONG)inputLen << 3)) < ((ULONG)inputLen << 3))
    ctx->count[1]++;
  ctx->count[1] += ((ULONG)inputLen >> 29);

  partLen = 64 - index;

  if (inputLen >= partLen)
    {
      CopyMem ((APTR)input, (APTR)&ctx->buffer[index], partLen);
      MD5Transform (ctx->state, ctx->buffer);

      for (i = partLen; i + 63 < inputLen; i += 64)
        MD5Transform (ctx->state, &input[i]);

      index = 0;
    }
  else
    i = 0;

  CopyMem ((APTR)&input[i], (APTR)&ctx->buffer[index], inputLen - i);
}

static void
MD5Final (UBYTE digest[MD5_HASH_SIZE], MD5_CTX *ctx)
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
  MD5Update (ctx, PADDING, padLen);

  /* Append length (before padding) */
  MD5Update (ctx, bits, 8);

  /* Store state in digest */
  for (index = 0; index < 16; index++)
    {
      digest[index]
          = (UBYTE)((ctx->state[index >> 2] >> ((index & 3) << 3)) & 0xFF);
    }

  /* Zeroize sensitive information. */
  memset ((APTR)ctx, 0, sizeof (*ctx));
}

static void
MD5Transform (ULONG state[4], const UBYTE block[64])
{
  ULONG a = state[0], b = state[1], c = state[2], d = state[3], x[16];
  ULONG i;

  for (i = 0; i < 16; i++)
    {
      x[i] = (ULONG)block[i * 4] | ((ULONG)block[i * 4 + 1] << 8)
             | ((ULONG)block[i * 4 + 2] << 16)
             | ((ULONG)block[i * 4 + 3] << 24);
    }

  /* Round 1 */
  FF (a, b, c, d, x[0], S11, 0xd76aa478);
  FF (d, a, b, c, x[1], S12, 0xe8c7b756);
  FF (c, d, a, b, x[2], S13, 0x242070db);
  FF (b, c, d, a, x[3], S14, 0xc1bdceee);
  FF (a, b, c, d, x[4], S11, 0xf57c0faf);
  FF (d, a, b, c, x[5], S12, 0x4787c62a);
  FF (c, d, a, b, x[6], S13, 0xa8304613);
  FF (b, c, d, a, x[7], S14, 0xfd469501);
  FF (a, b, c, d, x[8], S11, 0x698098d8);
  FF (d, a, b, c, x[9], S12, 0x8b44f7af);
  FF (c, d, a, b, x[10], S13, 0xffff5bb1);
  FF (b, c, d, a, x[11], S14, 0x895cd7be);
  FF (a, b, c, d, x[12], S11, 0x6b901122);
  FF (d, a, b, c, x[13], S12, 0xfd987193);
  FF (c, d, a, b, x[14], S13, 0xa679438e);
  FF (b, c, d, a, x[15], S14, 0x49b40821);

  /* Round 2 */
  GG (a, b, c, d, x[1], S21, 0xf61e2562);
  GG (d, a, b, c, x[6], S22, 0xc040b340);
  GG (c, d, a, b, x[11], S23, 0x265e5a51);
  GG (b, c, d, a, x[0], S24, 0xe9b6c7aa);
  GG (a, b, c, d, x[5], S21, 0xd62f105d);
  GG (d, a, b, c, x[10], S22, 0x2441453);
  GG (c, d, a, b, x[15], S23, 0xd8a1e681);
  GG (b, c, d, a, x[4], S24, 0xe7d3fbc8);
  GG (a, b, c, d, x[9], S21, 0x21e1cde6);
  GG (d, a, b, c, x[14], S22, 0xc33707d6);
  GG (c, d, a, b, x[3], S23, 0xf4d50d87);
  GG (b, c, d, a, x[8], S24, 0x455a14ed);
  GG (a, b, c, d, x[13], S21, 0xa9e3e905);
  GG (d, a, b, c, x[2], S22, 0xfcefa3f8);
  GG (c, d, a, b, x[7], S23, 0x676f02d9);
  GG (b, c, d, a, x[12], S24, 0x8d2a4c8a);

  /* Round 3 */
  HH (a, b, c, d, x[5], S31, 0xfffa3942);
  HH (d, a, b, c, x[8], S32, 0x8771f681);
  HH (c, d, a, b, x[11], S33, 0x6d9d6122);
  HH (b, c, d, a, x[14], S34, 0xfde5380c);
  HH (a, b, c, d, x[1], S31, 0xa4beea44);
  HH (d, a, b, c, x[4], S32, 0x4bdecfa9);
  HH (c, d, a, b, x[7], S33, 0xf6bb4b60);
  HH (b, c, d, a, x[10], S34, 0xbebfbc70);
  HH (a, b, c, d, x[13], S31, 0x289b7ec6);
  HH (d, a, b, c, x[0], S32, 0xeaa127fa);
  HH (c, d, a, b, x[3], S33, 0xd4ef3085);
  HH (b, c, d, a, x[6], S34, 0x4881d05);
  HH (a, b, c, d, x[9], S31, 0xd9d4d039);
  HH (d, a, b, c, x[12], S32, 0xe6db99e5);
  HH (c, d, a, b, x[15], S33, 0x1fa27cf8);
  HH (b, c, d, a, x[2], S34, 0xc4ac5665);

  /* Round 4 */
  II (a, b, c, d, x[0], S41, 0xf4292244);
  II (d, a, b, c, x[7], S42, 0x432aff97);
  II (c, d, a, b, x[14], S43, 0xab9423a7);
  II (b, c, d, a, x[5], S44, 0xfc93a039);
  II (a, b, c, d, x[12], S41, 0x655b59c3);
  II (d, a, b, c, x[3], S42, 0x8f0ccc92);
  II (c, d, a, b, x[10], S43, 0xffeff47d);
  II (b, c, d, a, x[1], S44, 0x85845dd1);
  II (a, b, c, d, x[8], S41, 0x6fa87e4f);
  II (d, a, b, c, x[15], S42, 0xfe2ce6e0);
  II (c, d, a, b, x[6], S43, 0xa3014314);
  II (b, c, d, a, x[13], S44, 0x4e0811a1);
  II (a, b, c, d, x[4], S41, 0xf7537e82);
  II (d, a, b, c, x[11], S42, 0xbd3af235);
  II (c, d, a, b, x[2], S43, 0x2ad7d2bb);
  II (b, c, d, a, x[9], S44, 0xeb86d391);

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;

  /* Zeroize sensitive information. */
  memset ((APTR)x, 0, sizeof (x));
}

/* Helper function to calculate MD5 of a buffer */
static void
CalculateMD5 (const UBYTE *data, ULONG size, UBYTE digest[MD5_HASH_SIZE])
{
  MD5_CTX ctx;
  MD5Init (&ctx);
  MD5Update (&ctx, data, size);
  MD5Final (digest, &ctx);
}

/* Convert MD5 hash to hex string */
static void
MD5ToHexString (const UBYTE hash[MD5_HASH_SIZE],
                char hex_string[MD5_STRING_SIZE])
{
  static const char hex_chars[] = "0123456789abcdef";
  int i;

  for (i = 0; i < MD5_HASH_SIZE; i++)
    {
      hex_string[i * 2] = hex_chars[(hash[i] >> 4) & 0x0F];
      hex_string[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
  hex_string[MD5_STRING_SIZE - 1] = '\0';
}

static BOOL
HexStringToMD5 (const char *hex_string, UBYTE hash[MD5_HASH_SIZE])
{
  int i;

  if (strlen (hex_string) != 32)
    {
      return FALSE;
    }

  for (i = 0; i < MD5_HASH_SIZE; i++)
    {
      char high = hex_string[i * 2];
      char low = hex_string[i * 2 + 1];

      if (!isxdigit (high) || !isxdigit (low))
        {
          return FALSE;
        }

      high = tolower (high);
      low = tolower (low);

      UBYTE high_val = (high >= 'a') ? (high - 'a' + 10) : (high - '0');
      UBYTE low_val = (low >= 'a') ? (low - 'a' + 10) : (low - '0');

      hash[i] = (high_val << 4) | low_val;
    }

  return TRUE;
}

static BOOL
CheckSongLengthsFile (char *filepath, ULONG filepath_size)
{
  BPTR lock;
  char progdir[256];
  BOOL found = FALSE;

  /* Get program directory */
  lock = GetProgramDir ();
  if (lock)
    {
      if (NameFromLock (lock, progdir, sizeof (progdir)))
        {
          /* Try common filename variations */
          const char *filenames[]
              = { "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5",
                  "Songlengths.txt", "songlengths.txt", NULL };

          int i;
          for (i = 0; filenames[i]; i++)
            {
              strncpy (filepath, progdir, filepath_size - 1);
              filepath[filepath_size - 1] = '\0';
              AddPart (filepath, filenames[i], filepath_size);

              BPTR file = Open (filepath, MODE_OLDFILE);
              if (file)
                {
                  Close (file);
                  DPRINTF ("Found songlengths file: %s\n", filepath);
                  found = TRUE;
                  break;
                }
            }
        }
    }

  if (!found)
    {
      /* Also try current directory */
      const char *filenames[]
          = { "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5", NULL };

      int i;
      for (i = 0; filenames[i]; i++)
        {
          BPTR file = Open (filenames[i], MODE_OLDFILE);
          if (file)
            {
              Close (file);
              strcpy (filepath, filenames[i]);
              DPRINTF ("Found songlengths file in current dir: %s\n",
                       filepath);
              found = TRUE;
              break;
            }
        }
    }

  return found;
}

/* Compare two MD5 hashes */
static BOOL
MD5Compare (const UBYTE hash1[MD5_HASH_SIZE], const UBYTE hash2[MD5_HASH_SIZE])
{
  return (memcmp (hash1, hash2, MD5_HASH_SIZE) == 0);
}

/* Environment variable functions (same as main_mui.c) */
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

static void
UpdateCurrentSongCache (struct SIDPlayerData *data)
{
  if (!data->current_entry)
    {
      current_song_cache.cache_valid = FALSE;
      return;
    }

  DPRINTF ("=== UpdateCurrentSongCache ===\n");
  DPRINTF ("Reading from entry at: %p\n", data->current_entry);

  /* Disable interrupts while reading to prevent corruption */
  Forbid ();

  current_song_cache.cached_subsongs = data->current_entry->subsongs;
  current_song_cache.cached_current = data->current_entry->current_subsong;
  current_song_cache.cached_duration = data->current_entry->duration;

  if (data->current_entry->title)
    {
      strncpy (current_song_cache.cached_title, data->current_entry->title,
               sizeof (current_song_cache.cached_title) - 1);
      current_song_cache
          .cached_title[sizeof (current_song_cache.cached_title) - 1]
          = '\0';
    }
  else
    {
      current_song_cache.cached_title[0] = '\0';
    }

  if (data->current_entry->filename)
    {
      strncpy (current_song_cache.cached_filename,
               data->current_entry->filename,
               sizeof (current_song_cache.cached_filename) - 1);
      current_song_cache
          .cached_filename[sizeof (current_song_cache.cached_filename) - 1]
          = '\0';
    }
  else
    {
      current_song_cache.cached_filename[0] = '\0';
    }

  current_song_cache.cache_valid = TRUE;

  Permit ();

  DPRINTF ("Cached values: subsongs=%d, current=%d, duration=%d\n",
           current_song_cache.cached_subsongs,
           current_song_cache.cached_current,
           (int)current_song_cache.cached_duration);
  DPRINTF ("Cached title: '%s'\n", current_song_cache.cached_title);
}

static BOOL
LoadSongLengthsWithProgress (struct SIDPlayerData *data, CONST_STRPTR filename)
{
  BPTR file;
  char line[1024];
  SongLengthEntry *entry;
  ULONG count = 0;
  ULONG line_count = 0;
  ULONG processed_lines = 0;

  file = Open (filename, MODE_OLDFILE);
  if (!file)
    {
      UpdateStatus (data, "Cannot open songlengths file");
      return FALSE;
    }

  /* First pass: count total lines for progress */
  UpdateStatus (data, "Analyzing songlengths database...");
  while (FGets (file, line, sizeof (line)))
    {
      line_count++;
      if ((line_count % 1000) == 0)
        {
          char progress_msg[256];
          sprintf (progress_msg, "Analyzing... %lu lines",
                   (unsigned long)line_count);
          UpdateStatus (data, progress_msg);

          /* Process MUI events to keep interface responsive */
          ULONG signals;
          DoMethod (data->app, MUIM_Application_Input, &signals);
        }
    }

  /* Reset to beginning */
  Seek (file, 0, OFFSET_BEGINNING);

  /* Free existing database */
  FreeSongLengthDB (data);

  UpdateStatus (data, "Loading song lengths database...");

  while (FGets (file, line, sizeof (line)))
    {
      char *md5_str, *length_str, *pos;
      UBYTE md5[MD5_HASH_SIZE];
      char *newline;

      processed_lines++;

      /* Update progress every 100 lines */
      if ((processed_lines % 100) == 0)
        {
          char progress_msg[256];
          ULONG percent = (processed_lines * 100) / line_count;
          sprintf (progress_msg, "Loading database... %lu%% (%lu/%lu entries)",
                   (unsigned long)percent, (unsigned long)processed_lines,
                   (unsigned long)line_count);
          UpdateStatus (data, progress_msg);

          /* Process MUI events to keep interface responsive */
          ULONG signals;
          DoMethod (data->app, MUIM_Application_Input, &signals);
        }

      /* Remove trailing newline */
      newline = strchr (line, '\n');
      if (newline)
        *newline = '\0';
      newline = strchr (line, '\r');
      if (newline)
        *newline = '\0';

      /* Skip empty lines and comments */
      if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
        {
          continue;
        }

      /* Find the '=' separator */
      pos = strchr (line, '=');
      if (!pos)
        {
          continue;
        }

      *pos = '\0';
      md5_str = line;
      length_str = pos + 1;

      /* Validate MD5 string length */
      if (strlen (md5_str) != 32)
        {
          continue;
        }

      /* Parse MD5 */
      if (!HexStringToMD5 (md5_str, md5))
        {
          continue;
        }

      /* Parse lengths */
      ULONG lengths[256];
      UWORD num_lengths = 0;

      char length_copy[512];
      strncpy (length_copy, length_str, sizeof (length_copy) - 1);
      length_copy[sizeof (length_copy) - 1] = '\0';

      char *token = strtok (length_copy, " \t");

      while (token && num_lengths < 256)
        {
          ULONG duration = ParseTimeString (token);
          if (duration > 0)
            {
              lengths[num_lengths] = duration + 1; /* +1 second buffer */
              num_lengths++;
            }
          token = strtok (NULL, " \t");
        }

      if (num_lengths == 0)
        {
          continue;
        }

      /* Create database entry */
      entry = AllocMem (sizeof (SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
      if (!entry)
        {
          continue;
        }

      CopyMem (md5, entry->md5, MD5_HASH_SIZE);
      entry->num_subsongs = num_lengths;
      entry->lengths = AllocMem (sizeof (ULONG) * num_lengths, MEMF_PUBLIC);

      if (!entry->lengths)
        {
          FreeMem (entry, sizeof (SongLengthEntry));
          continue;
        }

      CopyMem (lengths, entry->lengths, sizeof (ULONG) * num_lengths);

      /* Add to database */
      entry->next = data->songlength_db;
      data->songlength_db = entry;
      count++;
    }

  Close (file);

  char status_msg[256];
  sprintf (status_msg, "Loaded %u song length entries (+1 sec buffer)",
           (unsigned int)count);
  UpdateStatus (data, status_msg);

  DPRINTF (
      "Total entries loaded: %u (all durations include +1 second buffer)\n",
      (unsigned int)count);
  return TRUE;
}
/* Auto-load songlengths on startup */
static void
AutoLoadSongLengths (struct SIDPlayerData *data)
{
  char filepath[512];

  DPRINTF ("=== AutoLoadSongLengths START ===\n");

  if (CheckSongLengthsFile (filepath, sizeof (filepath)))
    {
      DPRINTF ("Auto-loading songlengths from: %s\n", filepath);

      UpdateStatus (data, "Auto-loading songlengths database...");

      /* Disable the Load Songlengths button temporarily */
      set (data->btn_load_songlengths, MUIA_Disabled, TRUE);
      set (data->btn_load_songlengths, MUIA_Text_Contents, "Loading...");

      if (LoadSongLengthsWithProgress (data, filepath))
        {
          /* Update any existing playlist entries */
          if (data->playlist_count > 0)
            {
              UpdateStatus (data, "Updating playlist with song lengths...");

              PlaylistEntry *entry = data->playlist_head;
              ULONG updated = 0;
              ULONG entry_num = 0;

              while (entry)
                {
                  entry_num++;

                  /* Update progress every 10 entries */
                  if ((entry_num % 10) == 0)
                    {
                      char progress_msg[256];
                      sprintf (progress_msg, "Updating playlist... %lu/%lu",
                               (unsigned long)entry_num,
                               (unsigned long)data->playlist_count);
                      UpdateStatus (data, progress_msg);

                      /* Process MUI events */
                      ULONG signals;
                      DoMethod (data->app, MUIM_Application_Input, &signals);
                    }

                  /* Check if we have this SID in database */
                  SongLengthEntry *db_entry = data->songlength_db;

                  while (db_entry)
                    {
                      if (MD5Compare (db_entry->md5, entry->md5))
                        {
                          UWORD old_subsongs = entry->subsongs;

                          if (db_entry->num_subsongs > old_subsongs
                              && db_entry->num_subsongs <= 256)
                            {
                              entry->subsongs = db_entry->num_subsongs;
                              updated++;
                              DPRINTF ("Updated %s: %d -> %d subsongs\n",
                                       FilePart (entry->filename),
                                       old_subsongs, entry->subsongs);
                            }

                          /* Update duration for current subsong */
                          entry->duration = FindSongLength (
                              data, entry->md5, entry->current_subsong);
                          break;
                        }
                      db_entry = db_entry->next;
                    }

                  entry = entry->next;
                }

              /* Update display */
              UpdatePlaylistDisplay (data);
              if (data->current_entry)
                {
                  UpdateCurrentSongCache (data);
                  UpdateCurrentSongDisplay (data);
                }

              char final_msg[256];
              sprintf (final_msg,
                       "Database loaded. Updated %lu of %lu playlist entries",
                       (unsigned long)updated,
                       (unsigned long)data->playlist_count);
              UpdateStatus (data, final_msg);
            }
          else
            {
              UpdateStatus (data, "Songlengths database loaded successfully");
            }
        }

      /* Re-enable the button */
      set (data->btn_load_songlengths, MUIA_Disabled, FALSE);
      set (data->btn_load_songlengths, MUIA_Text_Contents, "Load Songlengths");
    }
  else
    {
      DPRINTF ("No songlengths file found in program directory\n");
      UpdateStatus (data, "Ready - No songlengths database found");
    }

  DPRINTF ("=== AutoLoadSongLengths END ===\n");
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

/* Load SID directory preference */
static void
LoadSidDirectoryPref (struct SIDPlayerData *data)
{
  STRPTR env_dir;

  env_dir = ReadEnvVar (ENV_ULTIMATE64_SID_DIR);
  if (env_dir)
    {
      strncpy (data->last_sid_dir, env_dir, sizeof (data->last_sid_dir) - 1);
      data->last_sid_dir[sizeof (data->last_sid_dir) - 1] = '\0';
      FreeMem (env_dir, strlen (env_dir) + 1);
      DPRINTF ("Loaded SID directory: %s\n", data->last_sid_dir);
    }
  else
    {
      strcpy (data->last_sid_dir, "");
    }
}

/* Save SID directory preference */
static void
SaveSidDirectoryPref (struct SIDPlayerData *data)
{
  if (strlen (data->last_sid_dir) > 0)
    {
      WriteEnvVar (ENV_ULTIMATE64_SID_DIR, data->last_sid_dir, TRUE);
      DPRINTF ("Saved SID directory: %s\n", data->last_sid_dir);
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

/* Update status text */
static void
UpdateStatus (struct SIDPlayerData *data, CONST_STRPTR text)
{
  set (data->txt_status, MUIA_Text_Contents, text);
}

/* Load configuration from environment */
static void
LoadConfig (struct SIDPlayerData *data)
{
  STRPTR env_host, env_password;

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

  /* Load SID directory preference */
  LoadSidDirectoryPref (data);
}

/* Save configuration to environment */
static void
SaveConfig (struct SIDPlayerData *data)
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
  UpdateStatus (data, (CONST_STRPTR) "Configuration saved");
}

/* Free playlist */
static void
FreePlaylist (struct SIDPlayerData *data)
{
  PlaylistEntry *entry = data->playlist_head;
  PlaylistEntry *next;

  /* Clear the MUI list first to free any stored strings */
  if (data->playlist_list)
    {
      set (data->playlist_list, MUIA_List_Quiet, TRUE);
      DoMethod (data->playlist_list, MUIM_List_Clear);
      set (data->playlist_list, MUIA_List_Quiet, FALSE);
    }

  while (entry)
    {
      next = entry->next;

      if (entry->filename)
        {
          FreeMem (entry->filename, strlen (entry->filename) + 1);
        }
      if (entry->title)
        {
          FreeMem (entry->title, strlen (entry->title) + 1);
        }

      FreeMem (entry, sizeof (PlaylistEntry));
      entry = next;
    }

  data->playlist_head = NULL;
  data->current_entry = NULL;
  data->playlist_count = 0;
  data->current_index = 0;
}

/* Free song length database */
static void
FreeSongLengthDB (struct SIDPlayerData *data)
{
  SongLengthEntry *entry = data->songlength_db;
  SongLengthEntry *next;

  while (entry)
    {
      next = entry->next;

      if (entry->lengths)
        {
          FreeMem (entry->lengths, sizeof (ULONG) * entry->num_subsongs);
        }

      FreeMem (entry, sizeof (SongLengthEntry));
      entry = next;
    }

  data->songlength_db = NULL;
}

/* Parse SID file header to get number of subsongs */
static UWORD
ParseSIDSubsongs (const UBYTE *data, ULONG size)
{
  if (size < 0x7E || memcmp (data, "PSID", 4) != 0)
    {
      return 1; // Default to 1 subsong
    }

  UWORD songs = (data[14] << 8) | data[15];
  return (songs > 0) ? songs : 1;
}

/* Extract title from SID file */
static STRPTR
ExtractSIDTitle (const UBYTE *data, ULONG size)
{
  if (size < 0x7E || memcmp (data, "PSID", 4) != 0)
    {
      return NULL;
    }

  // Title is at offset 0x16, 32 bytes
  char title[33];
  CopyMem ((APTR)(data + 0x16), title, 32);
  title[32] = '\0';

  // Remove trailing spaces and nulls
  int i;
  for (i = 31; i >= 0 && (title[i] == ' ' || title[i] == '\0'); i--)
    {
      title[i] = '\0';
    }

  if (strlen (title) == 0)
    {
      return NULL;
    }

  STRPTR result = AllocMem (strlen (title) + 1, MEMF_PUBLIC);
  if (result)
    {
      strcpy (result, title);
    }

  return result;
}

/* Find song length in database */
static ULONG
FindSongLength (struct SIDPlayerData *data, const UBYTE md5[MD5_HASH_SIZE],
                UWORD subsong)
{
  SongLengthEntry *entry = data->songlength_db;

  while (entry)
    {
      if (MD5Compare (entry->md5, md5))
        {
          if (subsong < entry->num_subsongs)
            {
              /* Add 1 second buffer to prevent premature song ending */
              return entry->lengths[subsong] + 1;
            }
          break;
        }
      entry = entry->next;
    }

  /* Add 1 second to default length too */
  return DEFAULT_SONG_LENGTH + 1;
}

/* Add playlist entry - FIXED to use songlength database for subsong count */
static BOOL
AddPlaylistEntry (struct SIDPlayerData *data, CONST_STRPTR filename)
{
  UBYTE *file_data;
  ULONG file_size;
  PlaylistEntry *entry;
  PlaylistEntry *last;

  /* Load file to calculate MD5 and parse header */
  file_data = LoadFile (filename, &file_size);
  if (!file_data)
    {
      return FALSE;
    }

  /* Create new entry */
  entry = AllocMem (sizeof (PlaylistEntry), MEMF_PUBLIC | MEMF_CLEAR);
  if (!entry)
    {
      FreeMem (file_data, file_size);
      return FALSE;
    }

  /* Copy filename */
  entry->filename = AllocMem (strlen (filename) + 1, MEMF_PUBLIC);
  if (!entry->filename)
    {
      FreeMem (entry, sizeof (PlaylistEntry));
      FreeMem (file_data, file_size);
      return FALSE;
    }
  strcpy (entry->filename, filename);

  /* Calculate MD5 */
  CalculateMD5 (file_data, file_size, entry->md5);

  /* Parse SID header for basic subsong count */
  UWORD header_subsongs = ParseSIDSubsongs (file_data, file_size);

  /* CRITICAL FIX: Initialize with header value first */
  entry->subsongs = header_subsongs;
  entry->current_subsong = 0; /* Always start with subsong 0 (first subsong) */

  DPRINTF ("AddPlaylistEntry: Initial values - subsongs=%d, current=%d\n",
           entry->subsongs, entry->current_subsong);

  /* Check if we have this SID in our songlength database */
  if (data->songlength_db)
    {
      SongLengthEntry *db_entry = data->songlength_db;

      while (db_entry)
        {
          if (MD5Compare (db_entry->md5, entry->md5))
            {
              DPRINTF ("Found %s in database: %d subsongs (header said %d)\n",
                       FilePart (filename), db_entry->num_subsongs,
                       header_subsongs);

              /* Use database value if it's larger and reasonable */
              if (db_entry->num_subsongs > header_subsongs
                  && db_entry->num_subsongs <= 256)
                {
                  entry->subsongs = db_entry->num_subsongs;
                }
              break;
            }
          db_entry = db_entry->next;
        }
    }

  /* Extract title */
  entry->title = ExtractSIDTitle (file_data, file_size);
  if (!entry->title)
    {
      /* Use filename as title */
      STRPTR basename = FilePart (filename);
      entry->title = AllocMem (strlen (basename) + 1, MEMF_PUBLIC);
      if (entry->title)
        {
          strcpy (entry->title, basename);
        }
    }

  /* Find song length for the first subsong (subsong 0) */
  entry->duration = FindSongLength (data, entry->md5, 0);
  if (entry->duration == 0)
    {
      entry->duration = DEFAULT_SONG_LENGTH;
    }

  DPRINTF ("Final entry values: subsongs=%d, current=%d, duration=%d\n",
           entry->subsongs, entry->current_subsong, (int)entry->duration);

  FreeMem (file_data, file_size);

  /* Add to playlist */
  if (!data->playlist_head)
    {
      data->playlist_head = entry;
    }
  else
    {
      last = data->playlist_head;
      while (last->next)
        {
          last = last->next;
        }
      last->next = entry;
    }

  data->playlist_count++;
  return TRUE;
}

/* Parse time string (MM:SS or SS) */
static ULONG
ParseTimeString (const char *time_str)
{
  char *colon_pos;
  char *dot_pos;
  ULONG minutes = 0;
  ULONG seconds = 0;
  char temp_str[32];

  if (!time_str || strlen (time_str) == 0)
    {
      return 0;
    }

  /* Make a copy to work with */
  strncpy (temp_str, time_str, sizeof (temp_str) - 1);
  temp_str[sizeof (temp_str) - 1] = '\0';

  /* Find colon separator */
  colon_pos = strchr (temp_str, ':');
  if (!colon_pos)
    {
      /* No colon, treat as seconds only */
      return (ULONG)atoi (temp_str);
    }

  /* Split at colon */
  *colon_pos = '\0';
  minutes = (ULONG)atoi (temp_str);

  /* Parse seconds part (may have decimal) */
  char *seconds_str = colon_pos + 1;
  dot_pos = strchr (seconds_str, '.');
  if (dot_pos)
    {
      *dot_pos = '\0'; /* Ignore milliseconds for now */
    }

  seconds = (ULONG)atoi (seconds_str);

  /* Convert to total seconds */
  return (minutes * 60) + seconds;
}

static void
UpdatePlaylistDisplay (struct SIDPlayerData *data)
{
  PlaylistEntry *entry;
  ULONG i;

  DPRINTF ("=== UpdatePlaylistDisplay FIXED START ===\n");

  /* Clear list first */
  set (data->playlist_list, MUIA_List_Quiet, TRUE);
  DoMethod (data->playlist_list, MUIM_List_Clear);

  if (data->playlist_count == 0)
    {
      set (data->playlist_list, MUIA_List_Quiet, FALSE);
      DPRINTF ("Playlist is empty\n");
      return;
    }

  /* Add entries one by one to the list */
  entry = data->playlist_head;
  for (i = 0; i < data->playlist_count && entry; i++)
    {
      char *basename = FilePart (entry->filename);

      DPRINTF ("=== PROCESSING ENTRY %d ===\n", (int)i);
      DPRINTF ("Entry address: %p\n", entry);
      DPRINTF ("Filename: %s\n", basename);

      /* Read values safely into local int variables */
      int entry_subsongs = (int)entry->subsongs;
      int entry_current = (int)entry->current_subsong;
      int entry_duration = (int)entry->duration;

      DPRINTF ("Raw entry values: subsongs=%d, current=%d, duration=%d\n",
               entry_subsongs, entry_current, entry_duration);

      /* Validate values */
      if (entry_subsongs <= 0)
        {
          entry_subsongs = 1;
          DPRINTF ("WARNING: Fixed subsongs to 1\n");
        }
      if (entry_current < 0 || entry_current >= entry_subsongs)
        {
          entry_current = 0;
          DPRINTF ("WARNING: Fixed current subsong to 0\n");
        }

      /* Remove .sid extension for cleaner display */
      char clean_name[256];
      strncpy (clean_name, basename, sizeof (clean_name) - 1);
      clean_name[sizeof (clean_name) - 1] = '\0';
      char *dot = strrchr (clean_name, '.');
      if (dot && stricmp (dot, ".sid") == 0)
        {
          *dot = '\0';
        }

      /* Use title if available, otherwise use cleaned filename */
      char *display_name;
      if (entry->title && strlen (entry->title) > 0)
        {
          display_name = entry->title;
        }
      else
        {
          display_name = clean_name;
        }

      /* Allocate display string on heap */
      STRPTR list_string = AllocMem (512, MEMF_PUBLIC | MEMF_CLEAR);
      if (!list_string)
        {
          DPRINTF ("ERROR: Failed to allocate display string\n");
          entry = entry->next;
          continue;
        }

      /* Build display string manually without sprintf */
      strcpy (list_string, display_name);

      if (entry_subsongs > 1)
        {
          /* Add subsong info: " [1/12]" */
          strcat (list_string, " [");

          /* Add current subsong number (1-based) */
          int display_current = entry_current + 1;
          if (display_current < 10)
            {
              char c = '0' + display_current;
              strncat (list_string, &c, 1);
            }
          else if (display_current < 100)
            {
              char temp[3];
              temp[0] = '0' + (display_current / 10);
              temp[1] = '0' + (display_current % 10);
              temp[2] = '\0';
              strcat (list_string, temp);
            }
          else
            {
              strcat (list_string, "99+"); /* Fallback for very high numbers */
            }

          strcat (list_string, "/");

          /* Add total subsongs */
          if (entry_subsongs < 10)
            {
              char c = '0' + entry_subsongs;
              strncat (list_string, &c, 1);
            }
          else if (entry_subsongs < 100)
            {
              char temp[3];
              temp[0] = '0' + (entry_subsongs / 10);
              temp[1] = '0' + (entry_subsongs % 10);
              temp[2] = '\0';
              strcat (list_string, temp);
            }
          else
            {
              strcat (list_string, "99+"); /* Fallback */
            }

          strcat (list_string, "] ");

          /* Get duration for current subsong */
          ULONG current_duration
              = FindSongLength (data, entry->md5, entry_current);
          if (current_duration == 0)
            {
              current_duration = (ULONG)entry_duration;
              if (current_duration == 0)
                {
                  current_duration = DEFAULT_SONG_LENGTH;
                }
            }

          /* Add time manually */
          int minutes = (int)(current_duration / 60);
          int seconds = (int)(current_duration % 60);

          /* Add minutes */
          if (minutes < 10)
            {
              char c = '0' + minutes;
              strncat (list_string, &c, 1);
            }
          else
            {
              char temp[3];
              temp[0] = '0' + (minutes / 10);
              temp[1] = '0' + (minutes % 10);
              temp[2] = '\0';
              strcat (list_string, temp);
            }

          strcat (list_string, ":");

          /* Add seconds (always 2 digits) */
          char temp[3];
          temp[0] = '0' + (seconds / 10);
          temp[1] = '0' + (seconds % 10);
          temp[2] = '\0';
          strcat (list_string, temp);
        }
      else
        {
          /* Single subsong: show just time */
          ULONG duration = (ULONG)entry_duration;
          if (duration == 0)
            {
              duration = FindSongLength (data, entry->md5, 0);
              if (duration == 0)
                {
                  duration = DEFAULT_SONG_LENGTH;
                }
            }

          strcat (list_string, " - ");

          /* Add time manually */
          int minutes = (int)(duration / 60);
          int seconds = (int)(duration % 60);

          /* Add minutes */
          if (minutes < 10)
            {
              char c = '0' + minutes;
              strncat (list_string, &c, 1);
            }
          else
            {
              char temp[3];
              temp[0] = '0' + (minutes / 10);
              temp[1] = '0' + (minutes % 10);
              temp[2] = '\0';
              strcat (list_string, temp);
            }

          strcat (list_string, ":");

          /* Add seconds (always 2 digits) */
          char temp[3];
          temp[0] = '0' + (seconds / 10);
          temp[1] = '0' + (seconds % 10);
          temp[2] = '\0';
          strcat (list_string, temp);
        }

      DPRINTF ("Final display string: '%s'\n", list_string);

      /* Add to MUI list */
      DoMethod (data->playlist_list, MUIM_List_InsertSingle, list_string,
                MUIV_List_Insert_Bottom);

      DPRINTF ("=== END ENTRY %d ===\n", (int)i);
      entry = entry->next;
    }

  set (data->playlist_list, MUIA_List_Quiet, FALSE);

  /* Highlight current entry */
  if (data->current_entry)
    {
      set (data->playlist_list, MUIA_List_Active, data->current_index);
    }

  DPRINTF ("=== UpdatePlaylistDisplay FIXED END ===\n");
}

/* Load song lengths database */
/* Load song lengths database - FIXED to add 1 second to each duration */
static BOOL
LoadSongLengths (struct SIDPlayerData *data, CONST_STRPTR filename)
{
  BPTR file;
  char line[1024];
  SongLengthEntry *entry;
  ULONG count = 0;

  file = Open (filename, MODE_OLDFILE);
  if (!file)
    {
      UpdateStatus (data, "Cannot open songlengths file");
      return FALSE;
    }

  /* Free existing database */
  FreeSongLengthDB (data);

  UpdateStatus (data, "Loading song lengths database...");

  while (FGets (file, line, sizeof (line)))
    {
      char *md5_str, *length_str, *pos;
      UBYTE md5[MD5_HASH_SIZE];
      char *newline;

      /* Remove trailing newline */
      newline = strchr (line, '\n');
      if (newline)
        *newline = '\0';
      newline = strchr (line, '\r');
      if (newline)
        *newline = '\0';

      /* Skip empty lines and comments */
      if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
        {
          continue;
        }

      /* Find the '=' separator */
      pos = strchr (line, '=');
      if (!pos)
        {
          continue;
        }

      *pos = '\0';
      md5_str = line;
      length_str = pos + 1;

      /* Validate MD5 string length */
      if (strlen (md5_str) != 32)
        {
          DPRINTF ("Invalid MD5 length: %s (%d chars)\n", md5_str,
                   (int)strlen (md5_str));
          continue;
        }

      /* Parse MD5 */
      if (!HexStringToMD5 (md5_str, md5))
        {
          DPRINTF ("Invalid MD5 format: %s\n", md5_str);
          continue;
        }

      /* Parse lengths (format: "MM:SS MM:SS MM:SS.mmm...") */
      ULONG lengths[256]; /* Max subsongs */
      UWORD num_lengths = 0;

      /* Make a copy of length_str for strtok */
      char length_copy[512];
      strncpy (length_copy, length_str, sizeof (length_copy) - 1);
      length_copy[sizeof (length_copy) - 1] = '\0';

      char *token = strtok (length_copy, " \t");

      while (token && num_lengths < 256)
        {
          ULONG duration = ParseTimeString (token);
          if (duration > 0)
            {
              /* Add 1 second buffer to each song length */
              lengths[num_lengths] = duration + 1;
              num_lengths++;
            }
          token = strtok (NULL, " \t");
        }

      if (num_lengths == 0)
        {
          DPRINTF ("No valid durations found for MD5: %s\n", md5_str);
          continue;
        }

      /* Create database entry */
      entry = AllocMem (sizeof (SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
      if (!entry)
        {
          continue;
        }

      CopyMem (md5, entry->md5, MD5_HASH_SIZE);
      entry->num_subsongs = num_lengths;
      entry->lengths = AllocMem (sizeof (ULONG) * num_lengths, MEMF_PUBLIC);

      if (!entry->lengths)
        {
          FreeMem (entry, sizeof (SongLengthEntry));
          continue;
        }

      CopyMem (lengths, entry->lengths, sizeof (ULONG) * num_lengths);

      /* Add to database */
      entry->next = data->songlength_db;
      data->songlength_db = entry;
      count++;

      /* Debug output for validation */
      if (count <= 5)
        {
          char md5_debug[33];
          MD5ToHexString (entry->md5, md5_debug);
          DPRINTF ("Loaded: %s = %d subsongs (each +1 sec)", md5_debug,
                   num_lengths);
          if (strstr (md5_debug, "99fe6cdd155e30c198a3bb84977ef894"))
            {
              DPRINTF (" (RAMBO)");
            }
          DPRINTF ("\n");
        }
    }

  Close (file);

  char status_msg[256];
  sprintf (status_msg, "Loaded %u song length entries (+1 sec buffer)",
           (unsigned int)count);
  UpdateStatus (data, status_msg);

  DPRINTF (
      "Total entries loaded: %u (all durations include +1 second buffer)\n",
      (unsigned int)count);
  return TRUE;
}

static void
UpdateCurrentSongDisplay (struct SIDPlayerData *data)
{
  char song_str[256];

  DPRINTF ("=== UpdateCurrentSongDisplay FIXED TIME START ===\n");

  if (!data->current_entry || !current_song_cache.cache_valid)
    {
      set (data->txt_current_song, MUIA_Text_Contents, "No song loaded");
      set (data->txt_current_time, MUIA_Text_Contents, "0:00");
      set (data->txt_total_time, MUIA_Text_Contents, "0:00");
      set (data->txt_subsong_info, MUIA_Text_Contents, "");
      set (data->progress_gauge, MUIA_Gauge_Current, 0);
      return;
    }

  /* Use cached values */
  int subsongs = (int)current_song_cache.cached_subsongs;
  int current = (int)current_song_cache.cached_current;

  DPRINTF ("Values: subsongs=%d, current=%d\n", subsongs, current);
  DPRINTF ("Times: current_time=%lu, total_time=%lu\n",
           (unsigned long)data->current_time, (unsigned long)data->total_time);

  /* Song title */
  if (current_song_cache.cached_title[0] != '\0')
    {
      strcpy (song_str, current_song_cache.cached_title);
    }
  else
    {
      char *basename = FilePart (current_song_cache.cached_filename);
      strcpy (song_str, basename);
      char *dot = strrchr (song_str, '.');
      if (dot && stricmp (dot, ".sid") == 0)
        {
          *dot = '\0';
        }
    }
  set (data->txt_current_song, MUIA_Text_Contents, song_str);

  /* CRITICAL FIX: Build time strings manually to avoid sprintf issues */

  /* Current time string */
  static char current_time_str[16];
  current_time_str[0] = '\0';

  int current_minutes = (int)(data->current_time / 60);
  int current_seconds = (int)(data->current_time % 60);

  /* Build current time manually */
  if (current_minutes < 10)
    {
      char c = '0' + current_minutes;
      strncat (current_time_str, &c, 1);
    }
  else
    {
      char temp[3];
      temp[0] = '0' + (current_minutes / 10);
      temp[1] = '0' + (current_minutes % 10);
      temp[2] = '\0';
      strcat (current_time_str, temp);
    }

  strcat (current_time_str, ":");

  /* Add seconds (always 2 digits) */
  char temp[3];
  temp[0] = '0' + (current_seconds / 10);
  temp[1] = '0' + (current_seconds % 10);
  temp[2] = '\0';
  strcat (current_time_str, temp);

  DPRINTF ("Built current time string: '%s'\n", current_time_str);
  set (data->txt_current_time, MUIA_Text_Contents, current_time_str);

  /* Total time string */
  static char total_time_str[16];
  total_time_str[0] = '\0';

  int total_minutes = (int)(data->total_time / 60);
  int total_seconds = (int)(data->total_time % 60);

  /* Build total time manually */
  if (total_minutes < 10)
    {
      char c = '0' + total_minutes;
      strncat (total_time_str, &c, 1);
    }
  else
    {
      char temp2[3];
      temp2[0] = '0' + (total_minutes / 10);
      temp2[1] = '0' + (total_minutes % 10);
      temp2[2] = '\0';
      strcat (total_time_str, temp2);
    }

  strcat (total_time_str, ":");

  /* Add seconds (always 2 digits) */
  char temp3[3];
  temp3[0] = '0' + (total_seconds / 10);
  temp3[1] = '0' + (total_seconds % 10);
  temp3[2] = '\0';
  strcat (total_time_str, temp3);

  DPRINTF ("Built total time string: '%s'\n", total_time_str);
  set (data->txt_total_time, MUIA_Text_Contents, total_time_str);

  /* Subsong info - build manually */
  if (subsongs > 1)
    {
      /* Use global string to avoid stack issues */
      global_subsong_str[0] = '\0';

      strcat (global_subsong_str, "Subsong ");

      /* Add current subsong number (1-based) */
      int display_current = current + 1;
      if (display_current < 10)
        {
          char c = '0' + display_current;
          strncat (global_subsong_str, &c, 1);
        }
      else if (display_current < 100)
        {
          char temp4[3];
          temp4[0] = '0' + (display_current / 10);
          temp4[1] = '0' + (display_current % 10);
          temp4[2] = '\0';
          strcat (global_subsong_str, temp4);
        }

      strcat (global_subsong_str, " of ");

      /* Add total subsongs */
      if (subsongs < 10)
        {
          char c = '0' + subsongs;
          strncat (global_subsong_str, &c, 1);
        }
      else if (subsongs < 100)
        {
          char temp5[3];
          temp5[0] = '0' + (subsongs / 10);
          temp5[1] = '0' + (subsongs % 10);
          temp5[2] = '\0';
          strcat (global_subsong_str, temp5);
        }

      DPRINTF ("Built subsong string: '%s'\n", global_subsong_str);
      set (data->txt_subsong_info, MUIA_Text_Contents, global_subsong_str);
    }
  else
    {
      set (data->txt_subsong_info, MUIA_Text_Contents, "");
    }

  /* Progress gauge */
  if (data->total_time > 0)
    {
      ULONG progress = (data->current_time * 100) / data->total_time;
      if (progress > 100)
        progress = 100;
      set (data->progress_gauge, MUIA_Gauge_Current, progress);
      DPRINTF ("Progress: %lu%%\n", (unsigned long)progress);
    }
  else
    {
      set (data->progress_gauge, MUIA_Gauge_Current, 0);
      DPRINTF ("No total time, progress = 0\n");
    }

  DPRINTF ("=== UpdateCurrentSongDisplay FIXED TIME END ===\n");
}

static BOOL
PlayCurrentSong (struct SIDPlayerData *data)
{
  UBYTE *file_data;
  ULONG file_size;
  STRPTR error_details = NULL;
  LONG result;

  if (!data->connection || !data->current_entry)
    {
      return FALSE;
    }

  DPRINTF ("=== PlayCurrentSong FIXED START ===\n");

  /* Update cache BEFORE doing anything else */
  UpdateCurrentSongCache (data);

  /* Use cached values for safety */
  UWORD current_subsong = current_song_cache.cached_current;
  UWORD total_subsongs = current_song_cache.cached_subsongs;

  /* Ultimate64 expects subsong numbers starting from 1, not 0 */
  UBYTE ultimate64_subsong = current_subsong + 1;

  DPRINTF ("PlayCurrentSong: Playing subsong %d (0-based) = %d (Ultimate64 "
           "1-based)\n",
           current_subsong, ultimate64_subsong);

  /* Load file */
  file_data = LoadFile (data->current_entry->filename, &file_size);
  if (!file_data)
    {
      UpdateStatus (data, "Failed to load SID file");
      return FALSE;
    }

  /* Play SID with specific subsong (1-based for Ultimate64) */
  result = U64_PlaySID (data->connection, file_data, file_size,
                        ultimate64_subsong, &error_details);

  FreeMem (file_data, file_size);

  if (result != U64_OK)
    {
      char error_msg[512];
      sprintf (error_msg, "SID playback failed: %s",
               error_details ? error_details : U64_GetErrorString (result));
      UpdateStatus (data, error_msg);

      if (error_details)
        {
          FreeMem (error_details, strlen (error_details) + 1);
        }
      return FALSE;
    }

  if (error_details)
    {
      FreeMem (error_details, strlen (error_details) + 1);
    }

  /* Set proper total time for current subsong (already includes +1 second) */
  data->state = PLAYER_PLAYING;
  data->current_time = 0;

  /* Get duration for the specific subsong being played */
  data->total_time
      = FindSongLength (data, data->current_entry->md5, current_subsong);

  DPRINTF ("Set total_time to %lu seconds for subsong %d (includes +1 second "
           "buffer)\n",
           (unsigned long)data->total_time, current_subsong);

  data->timer_counter = 0;

  /* Update display using cached values */
  UpdateCurrentSongDisplay (data);

  /* CRITICAL FIX: Build status message using cached values and manual string
   * building */
  char *basename = FilePart (current_song_cache.cached_filename);

  /* Use global string to avoid stack corruption */
  static char status_msg[256];
  strcpy (status_msg, "Playing ");
  strcat (status_msg, basename);
  strcat (status_msg, " [");

  /* Add current subsong (1-based) manually */
  int display_current = (int)current_subsong + 1;
  if (display_current < 10)
    {
      char c = '0' + display_current;
      strncat (status_msg, &c, 1);
    }
  else if (display_current < 100)
    {
      char temp[3];
      temp[0] = '0' + (display_current / 10);
      temp[1] = '0' + (display_current % 10);
      temp[2] = '\0';
      strcat (status_msg, temp);
    }

  strcat (status_msg, "/");

  /* Add total subsongs manually */
  int display_total = (int)total_subsongs;
  if (display_total < 10)
    {
      char c = '0' + display_total;
      strncat (status_msg, &c, 1);
    }
  else if (display_total < 100)
    {
      char temp[3];
      temp[0] = '0' + (display_total / 10);
      temp[1] = '0' + (display_total % 10);
      temp[2] = '\0';
      strcat (status_msg, temp);
    }

  strcat (status_msg, "]");

  DPRINTF ("Status message built: '%s'\n", status_msg);
  UpdateStatus (data, status_msg);

  DPRINTF ("=== PlayCurrentSong FIXED END ===\n");
  return TRUE;
}

/* Go to next song */
static void
NextSong (struct SIDPlayerData *data)
{
  if (!data->current_entry)
    {
      return;
    }

  DPRINTF ("=== NextSong START ===\n");

  /* Update cache first */
  UpdateCurrentSongCache (data);

  /* Check if we have more subsongs */
  if (data->current_entry->current_subsong + 1
      < current_song_cache.cached_subsongs)
    {
      DPRINTF ("Moving to next subsong: %d -> %d\n",
               data->current_entry->current_subsong,
               data->current_entry->current_subsong + 1);

      data->current_entry->current_subsong++;

      /* Update duration for new subsong */
      data->current_entry->duration
          = FindSongLength (data, data->current_entry->md5,
                            data->current_entry->current_subsong);
      if (data->current_entry->duration == 0)
        {
          data->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

      DPRINTF ("New subsong duration: %lu seconds\n",
               (unsigned long)data->current_entry->duration);

      /* Update cache with new values */
      UpdateCurrentSongCache (data);

      UpdatePlaylistDisplay (data);
      if (data->state == PLAYER_PLAYING)
        {
          PlayCurrentSong (data);
        }
      else
        {
          UpdateCurrentSongDisplay (data);
        }
      return;
    }

  /* Reset current subsong */
  data->current_entry->current_subsong = 0;

  /* Move to next entry */
  if (data->shuffle_mode)
    {
      /* Random selection */
      if (data->playlist_count > 1)
        {
          ULONG random_index;
          PlaylistEntry *entry;
          ULONG i;

          do
            {
              random_index = rand () % data->playlist_count;
            }
          while (random_index == data->current_index
                 && data->playlist_count > 1);

          entry = data->playlist_head;
          for (i = 0; i < random_index && entry; i++)
            {
              entry = entry->next;
            }

          data->current_entry = entry;
          data->current_index = random_index;

          /* Update playlist selection visually */
          set (data->playlist_list, MUIA_List_Active, random_index);
          DPRINTF ("Shuffle: moved to random index %lu\n",
                   (unsigned long)random_index);
        }
    }
  else
    {
      /* Sequential */
      if (data->current_entry->next)
        {
          data->current_entry = data->current_entry->next;
          data->current_index++;

          /* Update playlist selection visually */
          set (data->playlist_list, MUIA_List_Active, data->current_index);
          DPRINTF ("Sequential: moved to index %lu\n",
                   (unsigned long)data->current_index);
        }
      else if (data->repeat_mode)
        {
          data->current_entry = data->playlist_head;
          data->current_index = 0;

          /* Update playlist selection visually */
          set (data->playlist_list, MUIA_List_Active, 0);
          DPRINTF ("Repeat: back to first song\n");
        }
      else
        {
          /* End of playlist */
          data->state = PLAYER_STOPPED;
          UpdateStatus (data, "Playlist finished");
          UpdateCurrentSongDisplay (data);
          DPRINTF ("End of playlist reached\n");
          return;
        }
    }

  /* Update duration for new song */
  data->current_entry->duration = FindSongLength (
      data, data->current_entry->md5, data->current_entry->current_subsong);
  if (data->current_entry->duration == 0)
    {
      data->current_entry->duration = DEFAULT_SONG_LENGTH;
    }

  /* Update cache with new song */
  UpdateCurrentSongCache (data);

  UpdatePlaylistDisplay (data);

  if (data->state == PLAYER_PLAYING)
    {
      PlayCurrentSong (data);
    }
  else
    {
      UpdateCurrentSongDisplay (data);
    }

  DPRINTF ("=== NextSong END ===\n");
}

static void
PrevSong (struct SIDPlayerData *data)
{
  if (!data->current_entry)
    {
      return;
    }

  DPRINTF ("=== PrevSong START ===\n");

  /* If we're more than 3 seconds into the song, restart current song */
  if (data->current_time > 3)
    {
      DPRINTF ("Restarting current song (time > 3 seconds)\n");
      if (data->state == PLAYER_PLAYING)
        {
          PlayCurrentSong (data);
        }
      return;
    }

  /* Check if we can go to previous subsong */
  if (data->current_entry->current_subsong > 0)
    {
      DPRINTF ("Moving to previous subsong\n");
      data->current_entry->current_subsong--;

      /* Update duration for previous subsong */
      data->current_entry->duration
          = FindSongLength (data, data->current_entry->md5,
                            data->current_entry->current_subsong);
      if (data->current_entry->duration == 0)
        {
          data->current_entry->duration = DEFAULT_SONG_LENGTH;
        }

      UpdateCurrentSongCache (data);
      UpdatePlaylistDisplay (data);

      if (data->state == PLAYER_PLAYING)
        {
          PlayCurrentSong (data);
        }
      else
        {
          UpdateCurrentSongDisplay (data);
        }
      return;
    }

  /* Move to previous entry */
  if (data->current_index > 0)
    {
      PlaylistEntry *entry = data->playlist_head;
      ULONG i;

      for (i = 0; i < data->current_index - 1 && entry; i++)
        {
          entry = entry->next;
        }

      if (entry)
        {
          data->current_entry = entry;
          data->current_index--;

          /* Set to last subsong */
          data->current_entry->current_subsong
              = data->current_entry->subsongs - 1;

          /* Update duration */
          data->current_entry->duration
              = FindSongLength (data, data->current_entry->md5,
                                data->current_entry->current_subsong);
          if (data->current_entry->duration == 0)
            {
              data->current_entry->duration = DEFAULT_SONG_LENGTH;
            }

          /* Update playlist selection visually */
          set (data->playlist_list, MUIA_List_Active, data->current_index);

          UpdateCurrentSongCache (data);
          UpdatePlaylistDisplay (data);

          if (data->state == PLAYER_PLAYING)
            {
              PlayCurrentSong (data);
            }
          else
            {
              UpdateCurrentSongDisplay (data);
            }

          DPRINTF ("Moved to previous song at index %lu\n",
                   (unsigned long)data->current_index);
        }
    }
  else
    {
      DPRINTF ("Already at first song\n");
    }

  DPRINTF ("=== PrevSong END ===\n");
}

/* Create configuration window */
static Object *
CreateConfigWindow (struct SIDPlayerData *data)
{
  Object *window, *ok_button, *cancel_button;
  Object *host_string, *password_string;

  window = WindowObject, MUIA_Window_Title, "Ultimate64 Configuration",
  MUIA_Window_ID, MAKE_ID ('C', 'F', 'G', 'W'), MUIA_Window_Width, 400,
  MUIA_Window_Height, 150, MUIA_Window_CloseGadget, FALSE,

  WindowContents, VGroup, Child, VGroup, MUIA_Frame, MUIV_Frame_Group,
  MUIA_FrameTitle, "Connection Settings",

  Child, HGroup, Child, Label ("Host:"), Child, host_string = StringObject,
  MUIA_String_Contents, (CONST_STRPTR)data->host, MUIA_String_MaxLen, 255, End,
  End,

  Child, HGroup, Child, Label ("Password:"), Child,
  password_string = StringObject, MUIA_String_Contents,
  (CONST_STRPTR)data->password, MUIA_String_MaxLen, 255, MUIA_String_Secret,
  TRUE, End, End,

  Child, VSpace (10),

  Child, TextObject, MUIA_Text_Contents,
  "Leave password empty if not required.\nSettings will be saved to ENV: and "
  "ENVARC:",
  MUIA_Text_PreParse, "\33c", MUIA_Font, MUIV_Font_Tiny, End, End,

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
OpenConfigWindow (struct SIDPlayerData *data)
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

      /* Open the window */
      set (data->config_window, MUIA_Window_Open, TRUE);
    }
}

/* Close configuration window */
static void
CloseConfigWindow (struct SIDPlayerData *data)
{
  if (data->config_window)
    {
      set (data->config_window, MUIA_Window_Open, FALSE);
    }
}

/* Apply configuration changes */
static void
ApplyConfigChanges (struct SIDPlayerData *data)
{
  STRPTR host_str, password_str;

  if (!data->config_window)
    {
      return;
    }

  /* Get values from string gadgets */
  get (data->config_host_string, MUIA_String_Contents, &host_str);
  get (data->config_password_string, MUIA_String_Contents, &password_str);

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

  /* Save configuration */
  SaveConfig (data);

  /* Close window */
  CloseConfigWindow (data);
}

/* Clear all configuration */
static void
ClearConfig (struct SIDPlayerData *data)
{
  /* Clear connection settings */
  DeleteVar (ENV_ULTIMATE64_HOST, GVF_GLOBAL_ONLY);
  DeleteVar (ENV_ULTIMATE64_PASSWORD, GVF_GLOBAL_ONLY);
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_HOST);
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_PASSWORD);

  /* Clear SID directory preference */
  ClearSidDirectoryPref (data);

  /* Reset to defaults */
  strcpy (data->host, "192.168.1.64");
  data->password[0] = '\0';

  UpdateStatus (data, "All configuration cleared");
}

/* Create menu bar */
static Object *
CreateMenuBar (struct SIDPlayerData *data)
{
  Object *menu;

  /* Suppress unused parameter warning */
  (void)data;

  menu = MUI_MakeObject (MUIO_MenustripNM, MainMenu, 0);
  return menu;
}

/* Show about dialog */
static void
ShowAboutDialog (struct SIDPlayerData *data)
{
  Object *about_window, *ok_button;
  BOOL running = TRUE;
  ULONG signals;
  ULONG about_id = 999; /* Unique ID for about dialog */

  about_window = WindowObject, MUIA_Window_Title,
  "About Ultimate64 SID Player", MUIA_Window_Width, 300, MUIA_Window_Height,
  150, MUIA_Window_CloseGadget, FALSE, MUIA_Window_DepthGadget, FALSE,
  MUIA_Window_SizeGadget, FALSE, MUIA_Window_DragBar, TRUE,

  WindowContents, VGroup, Child, VSpace (10), Child, TextObject,
  MUIA_Text_Contents, "Ultimate64 SID Player v1.0", MUIA_Text_PreParse,
  "\33c\33b", MUIA_Font, MUIV_Font_Big, End, Child, VSpace (5), Child,
  TextObject, MUIA_Text_Contents, "Playlist SID Player for Ultimate64",
  MUIA_Text_PreParse, "\33c", End, Child, VSpace (5), Child, TextObject,
  MUIA_Text_Contents, "2025 Marcin Spoczynski", MUIA_Text_PreParse, "\33c",
  MUIA_Font, MUIV_Font_Tiny, End, Child, VSpace (10), Child, HGroup, Child,
  HSpace (0), Child, ok_button = SimpleButton ("OK"), Child, HSpace (0), End,
  Child, VSpace (10), End, End;

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

/* Connect to Ultimate64 */
static void
DoConnect (struct SIDPlayerData *data)
{
  if (data->connection)
    {
      /* Disconnect */
      if (data->state == PLAYER_PLAYING)
        {
          data->state = PLAYER_STOPPED;
        }

      U64_Disconnect (data->connection);
      data->connection = NULL;

      set (data->txt_connection_status, MUIA_Text_Contents, "Disconnected");
      set (data->btn_connect, MUIA_Text_Contents, "Connect");
      UpdateStatus (data, "Disconnected");

      /* Disable controls */
      set (data->btn_play, MUIA_Disabled, TRUE);
      set (data->btn_stop, MUIA_Disabled, TRUE);
      set (data->btn_next, MUIA_Disabled, TRUE);
      set (data->btn_prev, MUIA_Disabled, TRUE);
    }
  else
    {
      /* Connect */
      if (strlen (data->host) == 0)
        {
          UpdateStatus (data,
                        "Please configure host address in Settings menu");
          return;
        }

      data->connection = U64_Connect (
          (CONST_STRPTR)data->host,
          strlen (data->password) > 0 ? (CONST_STRPTR)data->password : NULL);

      if (data->connection)
        {
          U64DeviceInfo info;
          char status[256];

          sprintf (status, "Connected to %s", data->host);
          set (data->txt_connection_status, MUIA_Text_Contents, "Connected");
          set (data->btn_connect, MUIA_Text_Contents, "Disconnect");
          UpdateStatus (data, status);

          /* Try to get device info */
          if (U64_GetDeviceInfo (data->connection, &info) == U64_OK)
            {
              sprintf (status, "Device: %s (firmware %s)",
                       info.product_name ? (char *)info.product_name
                                         : "Ultimate",
                       info.firmware_version ? (char *)info.firmware_version
                                             : "unknown");
              UpdateStatus (data, status);
              U64_FreeDeviceInfo (&info);
            }

          /* Enable controls */
          set (data->btn_play, MUIA_Disabled, FALSE);
          set (data->btn_stop, MUIA_Disabled, FALSE);
          set (data->btn_next, MUIA_Disabled, FALSE);
          set (data->btn_prev, MUIA_Disabled, FALSE);
        }
      else
        {
          set (data->txt_connection_status, MUIA_Text_Contents,
               "Disconnected");
          UpdateStatus (data, "Connection failed");
        }
    }
}

static void
DoAddFiles (struct SIDPlayerData *data)
{
  struct FileRequester *req;

  /* Use AllocAslRequestTags instead of manual TagItem array */
  if (strlen (data->last_sid_dir) > 0)
    {
      /* Check if directory still exists */
      BPTR lock = Lock (data->last_sid_dir, ACCESS_READ);
      if (lock)
        {
          UnLock (lock);
          DPRINTF ("Using remembered SID directory: %s\n", data->last_sid_dir);

          /* Use remembered directory */
          req = AllocAslRequestTags (
              ASL_FileRequest, ASLFR_TitleText, "Select SID files to add",
              ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern, "#?.sid",
              ASLFR_DoMultiSelect, TRUE, ASLFR_InitialDrawer,
              data->last_sid_dir, TAG_DONE);
        }
      else
        {
          DPRINTF ("Remembered SID directory no longer exists: %s\n",
                   data->last_sid_dir);
          data->last_sid_dir[0] = '\0'; /* Clear invalid directory */

          /* Use default (no initial directory) */
          req = AllocAslRequestTags (
              ASL_FileRequest, ASLFR_TitleText, "Select SID files to add",
              ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern, "#?.sid",
              ASLFR_DoMultiSelect, TRUE, TAG_DONE);
        }
    }
  else
    {
      /* No remembered directory - use default */
      req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                                 "Select SID files to add", ASLFR_DoPatterns,
                                 TRUE, ASLFR_InitialPattern, "#?.sid",
                                 ASLFR_DoMultiSelect, TRUE, TAG_DONE);
    }

  if (req && AslRequest (req, NULL))
    {
      struct WBArg *args = req->rf_ArgList;
      LONG num_args = req->rf_NumArgs;
      LONG i;
      char filename[512];
      ULONG added = 0;

      /* Remember the directory for next time */
      if (req->rf_Dir && strlen (req->rf_Dir) > 0)
        {
          strncpy (data->last_sid_dir, req->rf_Dir,
                   sizeof (data->last_sid_dir) - 1);
          data->last_sid_dir[sizeof (data->last_sid_dir) - 1] = '\0';
          SaveSidDirectoryPref (data);
          DPRINTF ("Remembered new SID directory: %s\n", data->last_sid_dir);
        }

      for (i = 0; i < num_args; i++)
        {
          NameFromLock (args[i].wa_Lock, filename, sizeof (filename));
          AddPart (filename, args[i].wa_Name, sizeof (filename));

          if (AddPlaylistEntry (data, filename))
            {
              added++;
            }
        }

      if (added > 0)
        {
          UpdatePlaylistDisplay (data);

          char status_msg[256];
          sprintf (status_msg, "Added %u files to playlist",
                   (unsigned int)added);
          UpdateStatus (data, status_msg);

          /* If no current entry, select first one */
          if (!data->current_entry && data->playlist_head)
            {
              data->current_entry = data->playlist_head;
              data->current_index = 0;

              /* Update duration for current subsong */
              data->current_entry->duration
                  = FindSongLength (data, data->current_entry->md5,
                                    data->current_entry->current_subsong);
              if (data->current_entry->duration == 0)
                {
                  data->current_entry->duration = DEFAULT_SONG_LENGTH;
                }

              UpdateCurrentSongDisplay (data);
            }
        }
    }

  if (req)
    {
      FreeAslRequest (req);
    }
}

static void
ClearSidDirectoryPref (struct SIDPlayerData *data)
{
  /* Delete environment variable */
  DeleteVar (ENV_ULTIMATE64_SID_DIR, GVF_GLOBAL_ONLY);

  /* Delete persistent file */
  DeleteFile ("ENVARC:" ENV_ULTIMATE64_SID_DIR);

  /* Clear memory */
  data->last_sid_dir[0] = '\0';

  DPRINTF ("SID directory preference cleared\n");
}

/* Remove selected file from playlist */
static void
DoRemoveFile (struct SIDPlayerData *data)
{
  LONG active;

  get (data->playlist_list, MUIA_List_Active, &active);

  if (active == MUIV_List_Active_Off || active < 0)
    {
      UpdateStatus (data, "No file selected");
      return;
    }

  /* Find entry to remove */
  PlaylistEntry *entry = data->playlist_head;
  PlaylistEntry *prev = NULL;
  ULONG i;

  for (i = 0; i < (ULONG)active && entry; i++)
    {
      prev = entry;
      entry = entry->next;
    }

  if (!entry)
    {
      return;
    }

  /* Stop playback if removing current song */
  if (entry == data->current_entry)
    {
      if (data->state == PLAYER_PLAYING)
        {
          data->state = PLAYER_STOPPED;
        }
      data->current_entry = entry->next;
      if (!data->current_entry && data->playlist_head != entry)
        {
          data->current_entry = data->playlist_head;
          data->current_index = 0;
        }
    }

  /* Remove from list */
  if (prev)
    {
      prev->next = entry->next;
    }
  else
    {
      data->playlist_head = entry->next;
    }

  /* Adjust current index */
  if (data->current_index > (ULONG)active)
    {
      data->current_index--;
    }

  /* Free entry */
  if (entry->filename)
    {
      FreeMem (entry->filename, strlen (entry->filename) + 1);
    }
  if (entry->title)
    {
      FreeMem (entry->title, strlen (entry->title) + 1);
    }
  FreeMem (entry, sizeof (PlaylistEntry));

  data->playlist_count--;

  UpdatePlaylistDisplay (data);
  UpdateCurrentSongDisplay (data);
  UpdateStatus (data, "File removed from playlist");
}

/* Clear playlist */
static void
DoClearPlaylist (struct SIDPlayerData *data)
{
  /* Stop playback */
  if (data->state == PLAYER_PLAYING)
    {
      data->state = PLAYER_STOPPED;
    }

  FreePlaylist (data);
  UpdatePlaylistDisplay (data);
  UpdateCurrentSongDisplay (data);
  UpdateStatus (data, "Playlist cleared");
}

/* Play */
static void
DoPlay (struct SIDPlayerData *data)
{
  LONG selected_index;

  /* Check if we have a playlist */
  if (data->playlist_count == 0)
    {
      UpdateStatus (data, "No songs in playlist");
      return;
    }

  /* Get currently selected item in playlist */
  get (data->playlist_list, MUIA_List_Active, &selected_index);

  DPRINTF ("=== DoPlay START ===\n");
  DPRINTF ("Current entry: %p (index %lu)\n", data->current_entry,
           (unsigned long)data->current_index);
  DPRINTF ("Selected index: %ld\n", selected_index);

  /* If a song is selected in the playlist, use that */
  if (selected_index != MUIV_List_Active_Off && selected_index >= 0
      && selected_index < (LONG)data->playlist_count)
    {

      DPRINTF ("Using selected playlist entry: %ld\n", selected_index);

      /* Find the selected entry */
      PlaylistEntry *entry = data->playlist_head;
      ULONG i;

      for (i = 0; i < (ULONG)selected_index && entry; i++)
        {
          entry = entry->next;
        }

      if (entry)
        {
          /* Update current entry to the selected one */
          data->current_entry = entry;
          data->current_index = (ULONG)selected_index;

          DPRINTF ("Set current entry to: %s (index %lu)\n",
                   FilePart (entry->filename), (unsigned long)selected_index);

          /* Update cache with new selection */
          UpdateCurrentSongCache (data);

          /* Start playing the selected song */
          if (PlayCurrentSong (data))
            {
              /* Update playlist display to show current selection */
              UpdatePlaylistDisplay (data);
              DPRINTF ("Successfully started playing selected song\n");
            }
          else
            {
              DPRINTF ("Failed to start playing selected song\n");
            }
        }
      else
        {
          DPRINTF ("ERROR: Could not find selected entry\n");
          UpdateStatus (data, "Error: Could not find selected song");
        }
    }
  /* If no song is selected but we have a current entry, play it */
  else if (data->current_entry)
    {
      DPRINTF ("No selection, playing current entry: %s\n",
               FilePart (data->current_entry->filename));

      /* Update cache and play current song */
      UpdateCurrentSongCache (data);
      PlayCurrentSong (data);
    }
  /* If no current entry, default to first song */
  else if (data->playlist_head)
    {
      DPRINTF ("No current entry, defaulting to first song\n");

      data->current_entry = data->playlist_head;
      data->current_index = 0;

      /* Select first song in playlist visually */
      set (data->playlist_list, MUIA_List_Active, 0);

      /* Update cache and play */
      UpdateCurrentSongCache (data);
      PlayCurrentSong (data);
      UpdatePlaylistDisplay (data);
    }
  else
    {
      UpdateStatus (data, "No songs in playlist");
    }

  DPRINTF ("=== DoPlay END ===\n");
}
/* Stop */
static void
DoStop (struct SIDPlayerData *data)
{
  if (data->state == PLAYER_PLAYING)
    {
      data->state = PLAYER_STOPPED;
      data->current_time = 0;
      UpdateCurrentSongDisplay (data);
      UpdateStatus (data, "Stopped");
    }
}

/* Load song lengths file - FIXED to update existing playlist entries */
/* Load song lengths file - FIXED to properly update existing playlist entries
 */
/* Load song lengths file - FIXED to properly update existing playlist entries
 */
static void
DoLoadSongLengths (struct SIDPlayerData *data)
{
  struct FileRequester *req;

  req = AllocAslRequestTags (ASL_FileRequest, ASLFR_TitleText,
                             "Select Songlengths.md5 file", ASLFR_DoPatterns,
                             TRUE, ASLFR_InitialPattern, "#?.md5", TAG_DONE);

  if (req && AslRequest (req, NULL))
    {
      char filename[512];
      strcpy (filename, req->rf_Dir);
      AddPart (filename, req->rf_File, sizeof (filename));

      DPRINTF ("=== Manual LoadSongLengths START ===\n");

      /* Disable button during loading */
      set (data->btn_load_songlengths, MUIA_Disabled, TRUE);
      set (data->btn_load_songlengths, MUIA_Text_Contents, "Loading...");

      if (LoadSongLengthsWithProgress (data, filename))
        {
          /* Update existing playlist entries with progress feedback */
          if (data->playlist_count > 0)
            {
              PlaylistEntry *entry = data->playlist_head;
              ULONG updated = 0;
              ULONG entry_num = 0;

              UpdateStatus (data, "Updating playlist entries...");

              while (entry)
                {
                  entry_num++;

                  /* Update progress */
                  if ((entry_num % 5) == 0)
                    {
                      char progress_msg[256];
                      sprintf (progress_msg, "Updating playlist... %lu/%lu",
                               (unsigned long)entry_num,
                               (unsigned long)data->playlist_count);
                      UpdateStatus (data, progress_msg);

                      /* Process MUI events */
                      ULONG signals;
                      DoMethod (data->app, MUIM_Application_Input, &signals);
                    }

                  /* Update subsong count from database */
                  SongLengthEntry *db_entry = data->songlength_db;

                  while (db_entry)
                    {
                      if (MD5Compare (db_entry->md5, entry->md5))
                        {
                          if (db_entry->num_subsongs > entry->subsongs
                              && db_entry->num_subsongs <= 256)
                            {
                              entry->subsongs = db_entry->num_subsongs;
                              updated++;
                            }
                          entry->duration = FindSongLength (
                              data, entry->md5, entry->current_subsong);
                          break;
                        }
                      db_entry = db_entry->next;
                    }

                  entry = entry->next;
                }

              /* Update displays */
              UpdatePlaylistDisplay (data);
              if (data->current_entry)
                {
                  UpdateCurrentSongCache (data);
                  UpdateCurrentSongDisplay (data);
                }

              char final_msg[256];
              sprintf (final_msg,
                       "Database loaded. Updated %lu of %lu playlist entries",
                       (unsigned long)updated,
                       (unsigned long)data->playlist_count);
              UpdateStatus (data, final_msg);
            }
        }

      /* Re-enable button */
      set (data->btn_load_songlengths, MUIA_Disabled, FALSE);
      set (data->btn_load_songlengths, MUIA_Text_Contents, "Load Songlengths");
    }

  if (req)
    {
      FreeAslRequest (req);
    }
}

/* Handle playlist double-click */
static void
DoPlaylistDoubleClick (struct SIDPlayerData *data)
{
  LONG active;

  get (data->playlist_list, MUIA_List_Active, &active);

  DPRINTF ("=== DoPlaylistDoubleClick START ===\n");
  DPRINTF ("Double-clicked index: %ld\n", active);

  if (active == MUIV_List_Active_Off || active < 0)
    {
      DPRINTF ("No valid selection for double-click\n");
      return;
    }

  /* Find the entry */
  PlaylistEntry *entry = data->playlist_head;
  ULONG i;

  for (i = 0; i < (ULONG)active && entry; i++)
    {
      entry = entry->next;
    }

  if (entry)
    {
      DPRINTF ("Double-clicked on: %s\n", FilePart (entry->filename));

      data->current_entry = entry;
      data->current_index = (ULONG)active;

      /* Update cache immediately */
      UpdateCurrentSongCache (data);

      /* If playing, start new song immediately */
      if (data->state == PLAYER_PLAYING)
        {
          DPRINTF ("Currently playing - switching to double-clicked song\n");
          PlayCurrentSong (data);
        }
      else
        {
          DPRINTF ("Not currently playing - just updating display\n");
          UpdateCurrentSongDisplay (data);
        }

      /* Update playlist display to show new current song */
      UpdatePlaylistDisplay (data);
    }
  else
    {
      DPRINTF ("ERROR: Could not find double-clicked entry\n");
    }

  DPRINTF ("=== DoPlaylistDoubleClick END ===\n");
}

/* Simple timer update - called every few seconds */
static void
UpdateTimer (struct SIDPlayerData *data)
{
  if (data->state == PLAYER_PLAYING)
    {
      data->timer_counter++;

      /* Update every 10 cycles (approximately 1 second) */
      if (data->timer_counter >= 10)
        {
          data->timer_counter = 0;
          data->current_time++;
          UpdateCurrentSongDisplay (data);

          /* Check if current subsong is finished */
          if (data->current_time >= data->total_time)
            {
              /* Check if we have more subsongs in this SID */
              if (data->current_entry
                  && data->current_entry->current_subsong + 1
                         < data->current_entry->subsongs)
                {

                  DPRINTF ("Auto-advancing to next subsong: %d -> %d\n",
                           data->current_entry->current_subsong,
                           data->current_entry->current_subsong + 1);

                  /* Move to next subsong */
                  data->current_entry->current_subsong++;

                  /* Update duration for new subsong */
                  data->current_entry->duration
                      = FindSongLength (data, data->current_entry->md5,
                                        data->current_entry->current_subsong);
                  if (data->current_entry->duration == 0)
                    {
                      data->current_entry->duration = DEFAULT_SONG_LENGTH;
                    }

                  /* Play the new subsong */
                  PlayCurrentSong (data);
                  UpdatePlaylistDisplay (data);
                }
              else
                {
                  /* No more subsongs, go to next entry */
                  NextSong (data);
                }
            }
        }
    }
}

/* Main function */
int
main (int argc, char *argv[])
{
  struct SIDPlayerData data;
  BOOL running = TRUE;
  ULONG signals;
  int retval = 0;

  /* Suppress unused parameter warnings */
  (void)argc;
  (void)argv;

  /* Initialize random seed */
  srand (time (NULL));

  /* Clear data structure */
  memset (&data, 0, sizeof (data));

  /* Open MUI master library */
  MUIMasterBase = OpenLibrary (MUIMASTER_NAME, 0);
  if (!MUIMasterBase)
    {
      DPRINTF ("Failed to open muimaster.library v%d\n", MUIMASTER_VMIN);
      return 20;
    }

  /* Initialize Ultimate64 library */
  if (!U64_InitLibrary ())
    {
      DPRINTF ("Failed to initialize Ultimate64 library\n");
      CloseLibrary (MUIMasterBase);
      return 20;
    }

  /* Load configuration */
  LoadConfig (&data);

  /* Create MUI application */
  data.app = ApplicationObject, MUIA_Application_Title,
  "Ultimate64 SID Player", MUIA_Application_Version, version,
  MUIA_Application_Copyright, "2025", MUIA_Application_Author,
  "Marcin Spoczynski", MUIA_Application_Description,
  "SID Player with Playlist for Ultimate64", MUIA_Application_Base,
  "U64SIDPLAYER", MUIA_Application_Menustrip, CreateMenuBar (&data),

  SubWindow, data.window = WindowObject, MUIA_Window_Title,
  "Ultimate64 SID Player", MUIA_Window_ID, MAKE_ID ('S', 'I', 'D', 'P'),
  MUIA_Window_Width, 600, MUIA_Window_Height, 500,

  WindowContents, VGroup,

  /* Connection status */
      Child, HGroup, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle,
  "Connection", Child, data.btn_connect = SimpleButton ("Connect"), Child,
  data.btn_load_songlengths = SimpleButton ("Load Songlengths"), Child,
  HSpace (0), Child, Label ("Status:"), Child,
  data.txt_connection_status = TextObject, MUIA_Text_Contents, "Disconnected",
  MUIA_Frame, MUIV_Frame_Text, End, End,

  /* Current song info */
      Child, VGroup, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle,
  "Now Playing", Child, data.txt_current_song = TextObject, MUIA_Text_Contents,
  "No song loaded", MUIA_Text_PreParse, "\33c\33b", MUIA_Font, MUIV_Font_Big,
  End, Child, data.txt_subsong_info = TextObject, MUIA_Text_Contents, "",
  MUIA_Text_PreParse, "\33c", End, Child, HGroup, Child,
  data.txt_current_time = TextObject, MUIA_Text_Contents, "0:00", End, Child,
  data.progress_gauge = GaugeObject, MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max,
  100, MUIA_Gauge_Current, 0, End, Child, data.txt_total_time = TextObject,
  MUIA_Text_Contents, "0:00", End, End, End,

  /* Player controls */
      Child, HGroup, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle, "Controls",
  Child, data.btn_prev = SimpleButton ("<<"), Child,
  data.btn_play = SimpleButton ("Play"), Child,
  data.btn_stop = SimpleButton ("Stop"), Child,
  data.btn_next = SimpleButton (">>"), Child, HSpace (10), Child,
  data.btn_shuffle = MUI_NewObject (MUIC_Image, MUIA_Image_Spec,
                                    MUII_CheckMark, MUIA_InputMode,
                                    MUIV_InputMode_Toggle, MUIA_Image_FreeVert,
                                    TRUE, MUIA_Selected, FALSE, TAG_DONE),
  Child, Label ("Shuffle"), Child,
  data.btn_repeat = MUI_NewObject (MUIC_Image, MUIA_Image_Spec, MUII_CheckMark,
                                   MUIA_InputMode, MUIV_InputMode_Toggle,
                                   MUIA_Image_FreeVert, TRUE, MUIA_Selected,
                                   FALSE, TAG_DONE),
  Child, Label ("Repeat"), Child, HSpace (0), End,

  /* Playlist */
      Child, VGroup, MUIA_Frame, MUIV_Frame_Group, MUIA_FrameTitle, "Playlist",
  Child, HGroup, Child, data.btn_add_file = SimpleButton ("Add Files"), Child,
  data.btn_remove_file = SimpleButton ("Remove"), Child,
  data.btn_clear_playlist = SimpleButton ("Clear All"), Child, HSpace (0), End,
  Child, data.playlist_list = ListviewObject, MUIA_Listview_List,
  MUI_NewObject (MUIC_List, MUIA_List_Format, "", TAG_DONE),
  MUIA_Listview_MultiSelect, MUIV_Listview_MultiSelect_None, MUIA_Weight, 100,
  End, End,

  /* Status bar */
      Child, data.txt_status = TextObject, MUIA_Frame, MUIV_Frame_Text,
  MUIA_Text_Contents, "Ready", End,

  End, End, End;

  if (!data.app)
    {
      DPRINTF ("Failed to create MUI application\n");
      retval = 20;
      goto cleanup;
    }

  /* Initially disable player controls */
  set (data.btn_play, MUIA_Disabled, TRUE);
  set (data.btn_stop, MUIA_Disabled, TRUE);
  set (data.btn_next, MUIA_Disabled, TRUE);
  set (data.btn_prev, MUIA_Disabled, TRUE);

  /* Setup notifications */
  DoMethod (data.window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, data.app,
            2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

  DoMethod (data.btn_connect, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_CONNECT);

  DoMethod (data.btn_load_songlengths, MUIM_Notify, MUIA_Pressed, FALSE,
            data.app, 2, MUIM_Application_ReturnID, ID_LOAD_SONGLENGTHS);

  DoMethod (data.btn_add_file, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_ADD_FILE);

  DoMethod (data.btn_remove_file, MUIM_Notify, MUIA_Pressed, FALSE, data.app,
            2, MUIM_Application_ReturnID, ID_REMOVE_FILE);

  DoMethod (data.btn_clear_playlist, MUIM_Notify, MUIA_Pressed, FALSE,
            data.app, 2, MUIM_Application_ReturnID, ID_CLEAR_PLAYLIST);

  DoMethod (data.btn_play, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PLAY);

  DoMethod (data.btn_stop, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_STOP);

  DoMethod (data.btn_next, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_NEXT);

  DoMethod (data.btn_prev, MUIM_Notify, MUIA_Pressed, FALSE, data.app, 2,
            MUIM_Application_ReturnID, ID_PREV);

  DoMethod (data.btn_shuffle, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
            data.app, 2, MUIM_Application_ReturnID, ID_SHUFFLE);

  DoMethod (data.btn_repeat, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
            data.app, 2, MUIM_Application_ReturnID, ID_REPEAT);

  DoMethod (data.playlist_list, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            data.app, 2, MUIM_Application_ReturnID, ID_PLAYLIST_DCLICK);

  /* Open window */
  set (data.window, MUIA_Window_Open, TRUE);
  // Auto-load songlengths database if available
  AutoLoadSongLengths (&data);

  /* Main event loop */
  while (running)
    {
      ULONG id = DoMethod (data.app, MUIM_Application_Input, &signals);

      /* Simple timer update */
      UpdateTimer (&data);

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
          DoConnect (&data);
          break;

        case ID_LOAD_SONGLENGTHS:
          DoLoadSongLengths (&data);
          break;

        case ID_ADD_FILE:
          DoAddFiles (&data);
          break;

        case ID_REMOVE_FILE:
          DoRemoveFile (&data);
          break;

        case ID_CLEAR_PLAYLIST:
          DoClearPlaylist (&data);
          break;

        case ID_PLAY:
          DoPlay (&data);
          break;

        case ID_STOP:
          DoStop (&data);
          break;

        case ID_NEXT:
          NextSong (&data);
          break;

        case ID_PREV:
          PrevSong (&data);
          break;

        case ID_SHUFFLE:
          get (data.btn_shuffle, MUIA_Selected, &data.shuffle_mode);
          UpdateStatus (&data, data.shuffle_mode ? "Shuffle enabled"
                                                 : "Shuffle disabled");
          break;

        case ID_REPEAT:
          get (data.btn_repeat, MUIA_Selected, &data.repeat_mode);
          UpdateStatus (&data, data.repeat_mode ? "Repeat enabled"
                                                : "Repeat disabled");
          break;

        case ID_PLAYLIST_DCLICK:
          DoPlaylistDoubleClick (&data);
          break;
        }

      if (running && signals)
        {
          /* Add a small delay to prevent busy waiting */
          signals |= SIGBREAKF_CTRL_C;
          if (Wait (signals) & SIGBREAKF_CTRL_C)
            {
              running = FALSE;
            }
        }
    }

cleanup:
  /* Disconnect */
  if (data.connection)
    {
      U64_Disconnect (data.connection);
    }

  /* Free playlist and database */
  FreePlaylist (&data);
  FreeSongLengthDB (&data);

  /* Close configuration window if open */
  if (data.config_window)
    {
      set (data.config_window, MUIA_Window_Open, FALSE);
    }

  /* Close window and dispose application */
  if (data.app)
    {
      set (data.window, MUIA_Window_Open, FALSE);
      MUI_DisposeObject (data.app);
    }

  /* Cleanup library */
  U64_CleanupLibrary ();
  CloseLibrary (MUIMasterBase);

  return retval;
}