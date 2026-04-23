/* Assembly64 client — search + file listing + binary download.
 *
 * Wraps the REST API at https://hackerswithstyle.se/leet/ .
 * All HTTP traffic attaches a "client-id: u64manager" header as required
 * by the service. JSON responses are parsed with the existing lightweight
 * U64_Json* helpers from lib/ultimate64_json.c — no external deps added.
 *
 * This header only exposes the handful of functions the UI tab needs.
 */

#ifndef U64_MUI_ASSEMBLY64_H
#define U64_MUI_ASSEMBLY64_H

#include <exec/types.h>

typedef struct AsmItem {
    char  name[128];
    char  group[64];
    char  updated[16];   /* server-side date-added, e.g. "2026-04-01" */
    UWORD year;
    UWORD category;      /* Assembly64 numeric category id */
    UBYTE rating;        /* 0..10 */
    char  id[16];        /* decimal item id, kept as string */
    /* Display caches — filled at parse time so the MUI display hook can
     * point straight at them (it's called on every redraw). */
    char  year_str[8];
    char  cat_str[32];
    struct AsmItem *next;
} AsmItem;

typedef struct AsmFile {
    char  path[128];     /* original filename (e.g. "demo-a.d64") */
    ULONG size;
    ULONG id;            /* fileId used in the download URL */
    char  size_str[16];  /* pre-rendered "N KB" / "N B" */
    struct AsmFile *next;
} AsmFile;

/* Human-readable name for an Assembly64 category id, from the categories
 * map at /search/categories. Returns "" for unknown ids (still safe to
 * pass to strlen / sprintf). */
CONST_STRPTR Asm_CategoryName(UWORD id);

/* Free a list returned by Asm_Search / Asm_ListFiles. NULL is safe. */
void Asm_FreeItems(AsmItem *head);
void Asm_FreeFiles(AsmFile *head);

/* Run a paginated search. `query` is the AQL string (goes straight into
 * ?query=, URL-encoded by this function). `offset` + `limit` are the path
 * parameters. Returns U64_OK on success; *out_list holds a linked list the
 * caller frees via Asm_FreeItems. */
LONG Asm_Search(CONST_STRPTR query, ULONG offset, ULONG limit,
                AsmItem **out_list, ULONG *out_count);

/* List every file in one item. Caller Asm_FreeFiles()s the result.
 * categoryId is ULONG (32-bit) — passing UWORD here used to get mangled
 * when sprintf'd via "%u", yielding garbage URLs like /entries/ID/0. */
LONG Asm_ListFiles(CONST_STRPTR itemId, ULONG categoryId,
                   AsmFile **out_list, ULONG *out_count);

/* Download one file to a temp path. out_path is written with the resulting
 * local filename (inside T:). suggested_name is used for the extension only;
 * collisions with other downloads are avoided with a suffix. */
LONG Asm_DownloadFile(CONST_STRPTR itemId, ULONG categoryId, ULONG fileId,
                      CONST_STRPTR suggested_name,
                      char *out_path, ULONG out_path_size);

/* Debug logger. Compile-time gated — set ASM_DEBUG to 1 to trace HTTP
 * activity into the shell's Output() stream; default 0 compiles to nothing. */
#ifndef ASM_DEBUG
#define ASM_DEBUG 0
#endif
#if ASM_DEBUG
void Asm_Log(CONST_STRPTR fmt, ...);
#define ASM_LOG(...)  Asm_Log((CONST_STRPTR)__VA_ARGS__)
#else
#define ASM_LOG(...)  ((void)0)
#endif

#endif
