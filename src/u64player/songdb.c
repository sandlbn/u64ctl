/* Ultimate64 SID Player - song-length database
 *
 * HVSC's Songlengths.md5 is a ~2 MB text file with ~60 000 entries of the form
 *
 *     <32-char-md5-hex>=MM:SS MM:SS ... (one per subsong)
 *
 * We parse it once into a bucketed hash table and write a binary cache
 * ("<source>.cache") next to it. Subsequent runs validate the cache against
 * the source file's size + DateStamp and, on a match, skip parsing entirely.
 *
 * Lookups go through SongDB_Find() for O(~15) bucket-chain scans instead of
 * O(60 000) over a single linked list.
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/mui.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "player.h"
#include "file_utils.h"

/* ------------------------------------------------------------------ */
/* Event pump helper                                                   */
/* ------------------------------------------------------------------ */

/* Pump pending MUI events without blocking. If the user triggered
 * MUIV_Application_ReturnID_Quit we set obj->quit_requested and return TRUE
 * so the caller can bail out of its long-running loop; main() honours the
 * flag on return. */
static BOOL
SongDB_PumpEvents(struct ObjApp *obj)
{
    ULONG signals;
    ULONG id = DoMethod(obj->App, MUIM_Application_Input, &signals);
    if (id == MUIV_Application_ReturnID_Quit) {
        obj->quit_requested = TRUE;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Hash table helpers                                                  */
/* ------------------------------------------------------------------ */

static UWORD
SongDB_BucketIndex(const UBYTE md5[MD5_HASH_SIZE])
{
    /* 12 bits from MD5[0..1] — uniform, so no skew. */
    return (UWORD)(((md5[0] << 4) | (md5[1] >> 4)) & 0x0FFF);
}

static struct SongLengthDB *
SongDB_Create(void)
{
    return AllocVec(sizeof(struct SongLengthDB), MEMF_PUBLIC | MEMF_CLEAR);
}

/* Inserts entry at head of its bucket chain. Ownership transfers to the db. */
static void
SongDB_InsertEntry(struct SongLengthDB *db, SongLengthEntry *entry)
{
    UWORD bucket = SongDB_BucketIndex(entry->md5);
    entry->next = db->buckets[bucket];
    db->buckets[bucket] = entry;
    db->entry_count++;
}

SongLengthEntry *
SongDB_Find(struct SongLengthDB *db, const UBYTE md5[MD5_HASH_SIZE])
{
    if (!db) return NULL;
    SongLengthEntry *e = db->buckets[SongDB_BucketIndex(md5)];
    while (e) {
        if (MD5Compare(e->md5, md5)) return e;
        e = e->next;
    }
    return NULL;
}

static void
SongDB_FreeAll(struct SongLengthDB *db)
{
    if (!db) return;
    for (ULONG i = 0; i < SONGDB_BUCKETS; i++) {
        SongLengthEntry *e = db->buckets[i];
        while (e) {
            SongLengthEntry *next = e->next;
            if (e->lengths) FreeVec(e->lengths);
            FreeVec(e);
            e = next;
        }
        db->buckets[i] = NULL;
    }
    FreeVec(db);
}

/* ------------------------------------------------------------------ */
/* Source file discovery                                               */
/* ------------------------------------------------------------------ */

BOOL CheckSongLengthsFile(char *filepath, ULONG filepath_size)
{
    BPTR lock;
    char progdir[256];
    BOOL found = FALSE;

    lock = GetProgramDir();
    if (lock) {
        if (NameFromLock(lock, progdir, sizeof(progdir))) {
            const char *filenames[] = {
                "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5",
                "Songlengths.txt", "songlengths.txt", NULL
            };
            for (int i = 0; filenames[i]; i++) {
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
        const char *filenames[] = {
            "Songlengths.md5", "songlengths.md5", "SONGLENGTHS.MD5", NULL
        };
        for (int i = 0; filenames[i]; i++) {
            BPTR file = Open(filenames[i], MODE_OLDFILE);
            if (file) {
                Close(file);
                strncpy(filepath, filenames[i], filepath_size - 1);
                filepath[filepath_size - 1] = '\0';
                U64_DEBUG("Found songlengths file in current dir: %s", filepath);
                found = TRUE;
                break;
            }
        }
    }

    return found;
}

/* ------------------------------------------------------------------ */
/* Binary cache                                                        */
/* ------------------------------------------------------------------ */

#define SONGDB_CACHE_MAGIC "U64SDBv1"      /* 8 bytes, no terminator needed */
#define SONGDB_CACHE_VERSION 1

struct SongDBCacheHeader {
    char   magic[8];
    ULONG  version;
    ULONG  source_size;
    struct DateStamp source_mtime;   /* 3 × LONG = 12 bytes */
    ULONG  entry_count;
};

/* Derive "<source>.cache" path. Writes to out[out_size]. */
static void
SongDB_CachePath(char *out, ULONG out_size, CONST_STRPTR source_path)
{
    strncpy(out, source_path, out_size - 1);
    out[out_size - 1] = '\0';
    size_t remain = out_size - strlen(out) - 1;
    if (remain >= 7) strcat(out, ".cache");
}

/* Fetch source file's size and DateStamp. Returns TRUE on success. */
static BOOL
SongDB_SourceStat(CONST_STRPTR source_path, ULONG *out_size,
                  struct DateStamp *out_mtime)
{
    BPTR lock = Lock(source_path, ACCESS_READ);
    if (!lock) return FALSE;

    struct FileInfoBlock *fib
        = AllocVec(sizeof(struct FileInfoBlock), MEMF_PUBLIC | MEMF_CLEAR);
    if (!fib) {
        UnLock(lock);
        return FALSE;
    }

    BOOL ok = FALSE;
    if (Examine(lock, fib)) {
        *out_size = (ULONG)fib->fib_Size;
        *out_mtime = fib->fib_Date;
        ok = TRUE;
    }

    FreeVec(fib);
    UnLock(lock);
    return ok;
}

/* Compares src_size/src_mtime against the cache header. */
static BOOL
SongDB_CacheFresh(const struct SongDBCacheHeader *hdr, ULONG src_size,
                  const struct DateStamp *src_mtime)
{
    if (memcmp(hdr->magic, SONGDB_CACHE_MAGIC, 8) != 0) return FALSE;
    if (hdr->version != SONGDB_CACHE_VERSION) return FALSE;
    if (hdr->source_size != src_size) return FALSE;
    if (hdr->source_mtime.ds_Days   != src_mtime->ds_Days)   return FALSE;
    if (hdr->source_mtime.ds_Minute != src_mtime->ds_Minute) return FALSE;
    if (hdr->source_mtime.ds_Tick   != src_mtime->ds_Tick)   return FALSE;
    return TRUE;
}

/* Populate a fresh DB from the cache file. Returns NULL on any failure. */
static struct SongLengthDB *
SongDB_ReadCache(CONST_STRPTR cache_path, ULONG src_size,
                 const struct DateStamp *src_mtime)
{
    BPTR file = Open(cache_path, MODE_OLDFILE);
    if (!file) return NULL;

    struct SongDBCacheHeader hdr;
    if (Read(file, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        Close(file);
        return NULL;
    }

    if (!SongDB_CacheFresh(&hdr, src_size, src_mtime)) {
        U64_DEBUG("songdb cache is stale — discarding");
        Close(file);
        return NULL;
    }

    struct SongLengthDB *db = SongDB_Create();
    if (!db) {
        Close(file);
        return NULL;
    }

    for (ULONG i = 0; i < hdr.entry_count; i++) {
        UBYTE md5[MD5_HASH_SIZE];
        UWORD num_subsongs;
        UWORD pad;

        if (Read(file, md5, MD5_HASH_SIZE) != MD5_HASH_SIZE) goto corrupt;
        if (Read(file, &num_subsongs, 2) != 2) goto corrupt;
        if (Read(file, &pad, 2) != 2) goto corrupt;
        if (num_subsongs == 0 || num_subsongs > 256) goto corrupt;

        SongLengthEntry *e
            = AllocVec(sizeof(SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!e) goto corrupt;
        CopyMem(md5, e->md5, MD5_HASH_SIZE);
        e->num_subsongs = num_subsongs;
        e->lengths = AllocVec(sizeof(ULONG) * num_subsongs, MEMF_PUBLIC);
        if (!e->lengths) {
            FreeVec(e);
            goto corrupt;
        }
        LONG want = (LONG)(sizeof(ULONG) * num_subsongs);
        if (Read(file, e->lengths, want) != want) {
            FreeVec(e->lengths);
            FreeVec(e);
            goto corrupt;
        }
        SongDB_InsertEntry(db, e);
    }

    Close(file);
    U64_DEBUG("songdb cache loaded: %lu entries", (unsigned long)db->entry_count);
    return db;

corrupt:
    U64_DEBUG("songdb cache appears corrupt — falling back to source");
    SongDB_FreeAll(db);
    Close(file);
    return NULL;
}

/* Serialise db to cache_path. Best-effort; failures are logged but ignored. */
static void
SongDB_WriteCache(const struct SongLengthDB *db, CONST_STRPTR cache_path,
                  ULONG src_size, const struct DateStamp *src_mtime)
{
    if (!db) return;

    BPTR file = Open(cache_path, MODE_NEWFILE);
    if (!file) {
        U64_DEBUG("could not open cache for write: %s", cache_path);
        return;
    }

    struct SongDBCacheHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    CopyMem((APTR)SONGDB_CACHE_MAGIC, hdr.magic, 8);
    hdr.version = SONGDB_CACHE_VERSION;
    hdr.source_size = src_size;
    hdr.source_mtime = *src_mtime;
    hdr.entry_count = db->entry_count;

    if (Write(file, &hdr, sizeof(hdr)) != sizeof(hdr)) goto fail;

    UWORD pad = 0;
    for (ULONG i = 0; i < SONGDB_BUCKETS; i++) {
        for (SongLengthEntry *e = db->buckets[i]; e; e = e->next) {
            if (Write(file, e->md5, MD5_HASH_SIZE) != MD5_HASH_SIZE) goto fail;
            if (Write(file, &e->num_subsongs, 2) != 2) goto fail;
            if (Write(file, &pad, 2) != 2) goto fail;
            LONG want = (LONG)(sizeof(ULONG) * e->num_subsongs);
            if (Write(file, e->lengths, want) != want) goto fail;
        }
    }

    Close(file);
    U64_DEBUG("songdb cache written: %s (%lu entries)",
              cache_path, (unsigned long)db->entry_count);
    return;

fail:
    U64_DEBUG("cache write failed, removing partial file");
    Close(file);
    DeleteFile(cache_path);
}

/* ------------------------------------------------------------------ */
/* Text parser (single pass, byte-progress)                            */
/* ------------------------------------------------------------------ */

static struct SongLengthDB *
SongDB_ParseText(struct ObjApp *obj, CONST_STRPTR filename)
{
    BPTR file = Open(filename, MODE_OLDFILE);
    if (!file) {
        APP_UpdateStatus("Cannot open songlengths file");
        return NULL;
    }

    /* Get total file size for progress — single Seek-to-end, Seek-back. */
    Seek(file, 0, OFFSET_END);
    LONG total_bytes = Seek(file, 0, OFFSET_BEGINNING);

    struct SongLengthDB *db = SongDB_Create();
    if (!db) {
        Close(file);
        return NULL;
    }

    APP_UpdateStatus("Loading song lengths database...");

    char line[1024];
    ULONG processed_lines = 0;
    char progress_msg[128];

    while (FGets(file, line, sizeof(line))) {
        processed_lines++;

        /* Progress every 500 lines (cheap; keeps UI responsive). While we're
         * pumping events we MUST check for Quit — if we discard it here the
         * main loop will have nothing to wake on and the process will hang
         * after the parse finishes. */
        if ((processed_lines % 500) == 0) {
            LONG here = Seek(file, 0, OFFSET_CURRENT);
            ULONG percent = (total_bytes > 0)
                ? (ULONG)(((LONG)((ULONG)here) * 100) / total_bytes)
                : 0;
            sprintf(progress_msg, "Loading database... %lu%% (%lu entries)",
                    (unsigned long)percent,
                    (unsigned long)db->entry_count);
            APP_UpdateStatus(progress_msg);

            if (SongDB_PumpEvents(obj)) {
                U64_DEBUG("songdb parse aborted — quit requested");
                Close(file);
                SongDB_FreeAll(db);
                return NULL;
            }
        }

        /* Strip CR/LF */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *md5_str = line;
        char *length_str = eq + 1;

        if (strlen(md5_str) != 32) continue;

        UBYTE md5[MD5_HASH_SIZE];
        if (!HexStringToMD5(md5_str, md5)) continue;

        /* Parse space/tab-delimited list of MM:SS durations. */
        ULONG lengths[256];
        UWORD num_lengths = 0;

        char length_copy[512];
        strncpy(length_copy, length_str, sizeof(length_copy) - 1);
        length_copy[sizeof(length_copy) - 1] = '\0';

        char *token = strtok(length_copy, " \t");
        while (token && num_lengths < 256) {
            ULONG duration = ParseTimeString(token);
            if (duration > 0) {
                lengths[num_lengths++] = duration + 1; /* +1s buffer */
            }
            token = strtok(NULL, " \t");
        }

        if (num_lengths == 0) continue;

        SongLengthEntry *entry
            = AllocVec(sizeof(SongLengthEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (!entry) continue;

        CopyMem(md5, entry->md5, MD5_HASH_SIZE);
        entry->num_subsongs = num_lengths;
        entry->lengths = AllocVec(sizeof(ULONG) * num_lengths, MEMF_PUBLIC);
        if (!entry->lengths) {
            FreeVec(entry);
            continue;
        }
        CopyMem(lengths, entry->lengths, sizeof(ULONG) * num_lengths);
        SongDB_InsertEntry(db, entry);
    }

    Close(file);

    sprintf(progress_msg, "Loaded %lu song length entries (+1s buffer)",
            (unsigned long)db->entry_count);
    APP_UpdateStatus(progress_msg);
    U64_DEBUG("songdb parsed %lu entries", (unsigned long)db->entry_count);
    return db;
}

/* ------------------------------------------------------------------ */
/* Public loader                                                       */
/* ------------------------------------------------------------------ */

/* Replace any existing DB with a freshly-parsed one from `filename`, then
 * write the binary cache. Warm startups bypass this via AutoLoadSongLengths.
 */
BOOL
LoadSongLengthsWithProgress(struct ObjApp *obj, CONST_STRPTR filename)
{
    FreeSongLengthDB(obj);

    struct SongLengthDB *db = SongDB_ParseText(obj, filename);
    if (!db) return FALSE;

    obj->songlength_db = db;

    /* Best-effort cache refresh — parse errors above already populated db. */
    ULONG src_size;
    struct DateStamp src_mtime;
    if (SongDB_SourceStat(filename, &src_size, &src_mtime)) {
        char cache_path[512];
        SongDB_CachePath(cache_path, sizeof(cache_path), filename);
        SongDB_WriteCache(db, cache_path, src_size, &src_mtime);
    }

    return TRUE;
}

void AutoLoadSongLengths(struct ObjApp *obj)
{
    char filepath[512];

    U64_DEBUG("=== AutoLoadSongLengths START ===");

    if (!CheckSongLengthsFile(filepath, sizeof(filepath))) {
        U64_DEBUG("No songlengths file found in program directory");
        APP_UpdateStatus("Ready - No songlengths database found");
        U64_DEBUG("=== AutoLoadSongLengths END ===");
        return;
    }

    set(obj->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
    set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Loading...");

    /* Try binary cache first. */
    ULONG src_size = 0;
    struct DateStamp src_mtime;
    BOOL have_stat = SongDB_SourceStat(filepath, &src_size, &src_mtime);
    struct SongLengthDB *db = NULL;

    if (have_stat) {
        char cache_path[512];
        SongDB_CachePath(cache_path, sizeof(cache_path), filepath);

        APP_UpdateStatus("Checking song-length cache...");
        db = SongDB_ReadCache(cache_path, src_size, &src_mtime);
        if (db) {
            APP_UpdateStatus("Song-length cache loaded");
        }
    }

    if (!db) {
        APP_UpdateStatus("Parsing songlengths database (first run)...");
        db = SongDB_ParseText(obj, filepath);
        if (db && have_stat) {
            char cache_path[512];
            SongDB_CachePath(cache_path, sizeof(cache_path), filepath);
            SongDB_WriteCache(db, cache_path, src_size, &src_mtime);
        }
    }

    FreeSongLengthDB(obj);
    obj->songlength_db = db;

    if (db && obj->playlist_count > 0) {
        /* Refresh existing playlist entries with whatever we just loaded. */
        APP_UpdateStatus("Updating playlist with song lengths...");
        PlaylistEntry *entry = obj->playlist_head;
        ULONG updated = 0;
        ULONG entry_num = 0;

        while (entry) {
            entry_num++;
            if ((entry_num % 25) == 0) {
                char progress_msg[128];
                sprintf(progress_msg, "Updating playlist... %u/%u",
                        (unsigned int)entry_num,
                        (unsigned int)obj->playlist_count);
                APP_UpdateStatus(progress_msg);
                if (SongDB_PumpEvents(obj)) break;
            }

            SongLengthEntry *db_entry = SongDB_Find(db, entry->md5);
            if (db_entry) {
                UWORD old_subsongs = entry->subsongs;
                if (db_entry->num_subsongs > old_subsongs
                    && db_entry->num_subsongs <= 256) {
                    entry->subsongs = db_entry->num_subsongs;
                    updated++;
                }
                entry->duration
                    = FindSongLength(obj, entry->md5, entry->current_subsong);
            }
            entry = entry->next;
        }

        APP_UpdatePlaylistDisplay();
        if (obj->current_entry) {
            APP_UpdateCurrentSongCache();
            APP_UpdateCurrentSongDisplay();
        }

        char final_msg[128];
        sprintf(final_msg,
                "Database loaded. Updated %u of %u playlist entries",
                (unsigned int)updated, (unsigned int)obj->playlist_count);
        APP_UpdateStatus(final_msg);
    } else if (db) {
        APP_UpdateStatus("Songlengths database ready");
    }

    set(obj->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
    set(obj->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");

    U64_DEBUG("=== AutoLoadSongLengths END ===");
}

/* ------------------------------------------------------------------ */
/* Download (unchanged behaviour; cache is refreshed by Load*)         */
/* ------------------------------------------------------------------ */

static void DownloadProgressCallback(ULONG bytes_downloaded, APTR userdata)
{
    (void)userdata;
    static char progress_msg[256];
    U64_DEBUG("Progress callback: %lu bytes", (unsigned long)bytes_downloaded);

    ULONG kb_downloaded = bytes_downloaded / 1024;
    sprintf(progress_msg, "Downloaded %lu KB from HVSC server",
        (unsigned long)kb_downloaded);
    APP_UpdateStatus(progress_msg);

    if (objApp && objApp->App) {
        /* Pump events; record a quit request if the user clicked close while
         * the download is in progress. Download itself can't be aborted
         * mid-flight, but recording the flag means we'll shut down cleanly
         * afterwards instead of hanging. */
        (void)SongDB_PumpEvents(objApp);
    }
}

BOOL APP_DownloadSongLengths(void)
{
    char local_filename[256];
    char progdir[256];
    BPTR lock;
    LONG result;

    if (!objApp) return FALSE;

    APP_UpdateStatus("Connecting to HVSC server...");

    set(objApp->BTN_LoadSongLengths, MUIA_Disabled, TRUE);
    set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Downloading...");

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
        switch (result) {
            case U64_ERR_NETWORK:  sprintf(error_msg, "Network error - check internet connection"); break;
            case U64_ERR_ACCESS:   sprintf(error_msg, "File access error - check disk space and permissions"); break;
            case U64_ERR_NOTFOUND: sprintf(error_msg, "File not found on HVSC server"); break;
            case U64_ERR_TIMEOUT:  sprintf(error_msg, "Download timeout - server may be busy"); break;
            default: sprintf(error_msg, "Download failed: %s", U64_GetErrorString(result)); break;
        }
        APP_UpdateStatus(error_msg);
        U64_DEBUG("All download attempts failed");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    APP_UpdateStatus("Download complete, verifying file...");

    BPTR test_file = Open(local_filename, MODE_OLDFILE);
    if (!test_file) {
        APP_UpdateStatus("Error: Downloaded file not accessible");
        set(objApp->BTN_LoadSongLengths, MUIA_Disabled, FALSE);
        set(objApp->BTN_LoadSongLengths, MUIA_Text_Contents, "Download Songlengths");
        return FALSE;
    }

    Seek(test_file, 0, OFFSET_END);
    LONG file_size = Seek(test_file, 0, OFFSET_BEGINNING);
    Close(test_file);

    if (file_size < 10000) {
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

    if (LoadSongLengthsWithProgress(objApp, local_filename)) {
        /* Refresh any existing playlist entries. */
        if (objApp->playlist_count > 0) {
            PlaylistEntry *entry = objApp->playlist_head;
            ULONG updated = 0;
            ULONG entry_num = 0;

            APP_UpdateStatus("Updating playlist with HVSC song lengths...");
            while (entry) {
                entry_num++;
                if ((entry_num % 25) == 0) {
                    char progress_msg[128];
                    sprintf(progress_msg, "Updating playlist... %u/%u",
                            (unsigned int)entry_num,
                            (unsigned int)objApp->playlist_count);
                    APP_UpdateStatus(progress_msg);
                    if (SongDB_PumpEvents(objApp)) break;
                }

                SongLengthEntry *db_entry
                    = SongDB_Find(objApp->songlength_db, entry->md5);
                if (db_entry) {
                    if (db_entry->num_subsongs > entry->subsongs
                        && db_entry->num_subsongs <= 256) {
                        entry->subsongs = db_entry->num_subsongs;
                        updated++;
                    }
                    entry->duration = FindSongLength(objApp, entry->md5,
                                                     entry->current_subsong);
                }
                entry = entry->next;
            }

            APP_UpdatePlaylistDisplay();
            if (objApp->current_entry) {
                APP_UpdateCurrentSongCache();
                APP_UpdateCurrentSongDisplay();
            }

            char final_msg[128];
            sprintf(final_msg,
                    "HVSC database loaded! Updated %u of %u playlist entries",
                    (unsigned int)updated,
                    (unsigned int)objApp->playlist_count);
            APP_UpdateStatus(final_msg);
        } else {
            APP_UpdateStatus("HVSC Songlengths database downloaded and loaded!");
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

/* ------------------------------------------------------------------ */
/* Public lookup / free                                                */
/* ------------------------------------------------------------------ */

ULONG FindSongLength(struct ObjApp *obj, const UBYTE md5[MD5_HASH_SIZE], UWORD subsong)
{
    SongLengthEntry *e = SongDB_Find(obj ? obj->songlength_db : NULL, md5);
    if (!e) return DEFAULT_SONG_LENGTH;
    if (subsong < e->num_subsongs) return e->lengths[subsong];
    return DEFAULT_SONG_LENGTH;
}

void FreeSongLengthDB(struct ObjApp *obj)
{
    if (!obj) return;
    SongDB_FreeAll(obj->songlength_db);
    obj->songlength_db = NULL;
}
