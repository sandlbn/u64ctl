/* Assembly64 REST client — see assembly64.h for the public surface.
 *
 * Design notes:
 *
 * - Every GET sends `client-id: u64manager\r\n` as per Assembly64's rules.
 * - JSON is parsed by the same lightweight helpers the rest of this repo
 *   uses for Ultimate64 device responses (ultimate64_json.c). Array parsing
 *   is done by brace-scanning: for each top-level `{...}` we record its
 *   byte range and run a scoped U64_JsonFindKey on that substring.
 * - URL encoding is minimal: we escape just the characters AQL callers
 *   are likely to hit in practice — space, quote, '#', '&', '+'. Good
 *   enough for typed queries; avoids pulling in a full URLEncoder.
 */

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/scale.h>

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <graphics/displayinfo.h>
#include <intuition/classusr.h>
#include <intuition/screens.h>

#include <clib/alib_protos.h>   /* DoMethod */

#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"
#include "assembly64.h"

#define ASM_BASE "http://hackerswithstyle.se/leet"
#define ASM_HEADERS "client-id: u64manager\r\n"

/* datatypes.library is declared by proto/datatypes.h as an extern
 * Library pointer; we provide the storage and open it on demand. */

#if ASM_DEBUG
/* See assembly64.h for rationale — sprintf into a buffer then PutStr.
 * Goes straight to the Shell's Output() handle; silent on Workbench launch.
 * Compiled out entirely unless ASM_DEBUG is defined non-zero. */
void
Asm_Log(CONST_STRPTR fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + 6, sizeof(buf) - 7, (const char *)fmt, ap);
    va_end(ap);
    buf[0]='['; buf[1]='A'; buf[2]='S'; buf[3]='M'; buf[4]=']'; buf[5]=' ';
    ULONG n = strlen(buf);
    if (n + 1 < sizeof(buf)) { buf[n]='\n'; buf[n+1]='\0'; }
    if (Output()) PutStr((STRPTR)buf);
}
#endif

/* ------------------------------------------------------------------ */
/* URL encode + builder                                                */
/* ------------------------------------------------------------------ */

static void
url_encode(CONST_STRPTR in, char *out, ULONG out_size)
{
    ULONG w = 0;
    for (ULONG i = 0; in[i] && w + 3 < out_size; i++) {
        UBYTE c = (UBYTE)in[i];
        /* Escape what commonly breaks query strings. Leave AQL's ':' and
         * '*' alone so `name:*elite*` stays readable on the wire.
         * `<`, `=`, `>` must be escaped — nginx rejects raw `>=` as
         * malformed and returns HTTP 400 for `rating:>=7`-style filters. */
        if (c == ' ' || c == '"' || c == '#' || c == '&' || c == '+'
            || c == '%' || c == '<' || c == '=' || c == '>'
            || c < 0x20 || c >= 0x7F) {
            static const char hex[] = "0123456789ABCDEF";
            out[w++] = '%';
            out[w++] = hex[(c >> 4) & 0xF];
            out[w++] = hex[c & 0xF];
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = '\0';
}

/* ------------------------------------------------------------------ */
/* JSON array walker                                                   */
/* ------------------------------------------------------------------ */

/* Find the byte range of the n-th top-level object inside `buf`.
 * Top-level means: curly-brace depth transitions from 0 to 1 then back to 0.
 * Correctly skips braces inside string literals.
 *
 * Returns TRUE and fills *obj_start (index of '{') and *obj_len (length
 * including the closing '}') for the next object starting at/after *scan.
 * On completion leaves *scan past the closing '}'. FALSE = no more objects.
 */
static BOOL
next_json_object(const char *buf, ULONG buf_len, ULONG *scan,
                 ULONG *obj_start, ULONG *obj_len)
{
    ULONG i = *scan;
    int depth = 0;
    BOOL in_string = FALSE;
    BOOL escaped = FALSE;
    ULONG start = 0;

    while (i < buf_len) {
        char c = buf[i];
        if (in_string) {
            if (escaped) escaped = FALSE;
            else if (c == '\\') escaped = TRUE;
            else if (c == '"') in_string = FALSE;
        } else {
            if (c == '"') in_string = TRUE;
            else if (c == '{') {
                if (depth == 0) start = i;
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    *obj_start = start;
                    *obj_len = (i - start) + 1;
                    *scan = i + 1;
                    return TRUE;
                }
            }
        }
        i++;
    }
    *scan = i;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Text sanitisation for display                                       */
/* ------------------------------------------------------------------ */

/* Copy src into dst, dropping control bytes (0x00-0x1F, 0x7F), replacing
 * any non-ASCII byte (>= 0x80) with '?', and truncating the visible text
 * to `max_chars` with an ellipsis when cut.
 *
 * The server gives us UTF-8 that MUI's Amiga font can't render — accents,
 * box-drawing, emoji etc. come out as garbage glyphs or kill the row.
 * Collapsing to plain printable ASCII keeps the list readable; the
 * truncation cap keeps one long group/handle from pushing other columns
 * off-screen. Caller-controlled `max_chars` <= dst_size - 4 (room for
 * "..." + NUL).
 */
static void
sanitize_field(char *dst, ULONG dst_size, const char *src, ULONG max_chars)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    /* Clamp max_chars into what dst can hold with room for ellipsis + NUL. */
    if (max_chars > dst_size - 4) max_chars = dst_size - 4;

    ULONG w = 0;
    BOOL truncated = FALSE;
    for (ULONG i = 0; src[i]; i++) {
        UBYTE c = (UBYTE)src[i];
        /* Drop C0/DEL controls (but keep tab if ever present — unlikely). */
        if (c < 0x20 || c == 0x7F) continue;
        /* Non-ASCII byte → '?'. This handles lone UTF-8 continuation
         * bytes and anything > 127 without needing a real decoder. */
        if (c >= 0x80) c = '?';
        if (w >= max_chars) { truncated = TRUE; break; }
        dst[w++] = (char)c;
    }
    /* Trim any trailing spaces left after stripping. */
    while (w > 0 && dst[w - 1] == ' ') w--;
    if (truncated) {
        dst[w++] = '.'; dst[w++] = '.'; dst[w++] = '.';
    }
    dst[w] = '\0';
}

/* ------------------------------------------------------------------ */
/* Category id → human label                                           */
/* ------------------------------------------------------------------ */

/* Label is always "<repo> <kind>" so the user can see at a glance which
 * source a result came from (CSDB vs c64.com vs HVSC vs OneLoad64 …). */
CONST_STRPTR
Asm_CategoryName(UWORD id)
{
    switch (id) {
    case 0:  return (CONST_STRPTR)"CSDB Games";
    case 1:  return (CONST_STRPTR)"CSDB Demos";
    case 2:  return (CONST_STRPTR)"CSDB C128";
    case 3:  return (CONST_STRPTR)"CSDB Graphics";
    case 4:  return (CONST_STRPTR)"CSDB Music";
    case 5:  return (CONST_STRPTR)"CSDB Mags";
    case 6:  return (CONST_STRPTR)"CSDB BBS";
    case 7:  return (CONST_STRPTR)"CSDB Misc";
    case 8:  return (CONST_STRPTR)"CSDB Tools";
    case 9:  return (CONST_STRPTR)"CSDB Charts";
    case 10: return (CONST_STRPTR)"CSDB Easyflash";
    case 11: return (CONST_STRPTR)"c64.org Intros";
    case 12: return (CONST_STRPTR)"c64tapes.org Tapes";
    case 14: return (CONST_STRPTR)"c64.com Demos";
    case 15: return (CONST_STRPTR)"c64.com Games";
    case 16: return (CONST_STRPTR)"Gamebase64";
    case 17: return (CONST_STRPTR)"SEUCK";
    case 18: return (CONST_STRPTR)"HVSC Music";
    case 19: return (CONST_STRPTR)"HVSC Games";
    case 20: return (CONST_STRPTR)"HVSC Demos";
    case 21: return (CONST_STRPTR)"HVSC Artist";
    case 22: return (CONST_STRPTR)"Mayhem CRT";
    case 23: return (CONST_STRPTR)"Preservers Disk";
    case 24: return (CONST_STRPTR)"Preservers Tape";
    case 25: return (CONST_STRPTR)"CSDB REU";
    case 33: return (CONST_STRPTR)"OneLoad64 Games";
    case 35: return (CONST_STRPTR)"Ultimate Tape Arc.";
    case 36: return (CONST_STRPTR)"Commodore Games";
    case 37: return (CONST_STRPTR)"Commodore Demos";
    case 38: return (CONST_STRPTR)"Commodore Graphics";
    case 39: return (CONST_STRPTR)"Commodore Music";
    case 40: return (CONST_STRPTR)"Commodore Apps";
    default: return (CONST_STRPTR)"";
    }
}

/* ------------------------------------------------------------------ */
/* Asm_FreeItems / Asm_FreeFiles                                       */
/* ------------------------------------------------------------------ */

void
Asm_FreeItems(AsmItem *head)
{
    while (head) {
        AsmItem *next = head->next;
        FreeVec(head);
        head = next;
    }
}

void
Asm_FreeFiles(AsmFile *head)
{
    while (head) {
        AsmFile *next = head->next;
        FreeVec(head);
        head = next;
    }
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

LONG
Asm_Search(CONST_STRPTR query, ULONG offset, ULONG limit,
           AsmItem **out_list, ULONG *out_count)
{
    if (!out_list || !out_count) return U64_ERR_INVALID;
    *out_list = NULL;
    *out_count = 0;

    char encoded[768];
    url_encode(query ? query : (CONST_STRPTR)"", encoded, sizeof(encoded));

    char url[1024];
    sprintf(url, "%s/search/aql/%lu/%lu?query=%s",
            ASM_BASE, (unsigned long)offset, (unsigned long)limit, encoded);

    ASM_LOG("GET %s", url);

    UBYTE *body = NULL;
    ULONG body_size = 0;
    UWORD status = 0;
    LONG result = U64_HttpGetURL((CONST_STRPTR)url,
                                 (CONST_STRPTR)ASM_HEADERS,
                                 &body, &body_size, &status);
    ASM_LOG("HTTP rc=%ld status=%u bytes=%lu",
            (long)result, (unsigned)status, (unsigned long)body_size);
    if (result != U64_OK) {
        if (body) {
            ASM_LOG("error body: %.120s", (char *)body);
            FreeVec(body);
        }
        *out_list = NULL; *out_count = 0;
        /* Surface the HTTP status so callers can distinguish e.g. 463 (AQL
         * syntax) from 5xx (server hiccup). Encoded as a negative number
         * = -(1000 + status) to avoid collision with U64_ERR_* values. */
        return status ? -(LONG)(1000 + status) : result;
    }
    if (!body || body_size == 0) {
        ASM_LOG("empty body — treating as zero-result");
        if (body) FreeVec(body);
        *out_list = NULL; *out_count = 0;
        return U64_OK;
    }
    ASM_LOG("body head: %.80s", (char *)body);

    /* Walk each top-level object in the returned array. */
    AsmItem *head = NULL;
    AsmItem *tail = NULL;
    ULONG scan = 0;
    ULONG obj_start, obj_len;
    ULONG count = 0;

    while (next_json_object((char *)body, body_size, &scan, &obj_start, &obj_len)) {
        AsmItem *item = AllocVec(sizeof(AsmItem), MEMF_PUBLIC | MEMF_CLEAR);
        if (!item) break;

        JsonParser p;
        U64_JsonInit(&p, (CONST_STRPTR)(body + obj_start));
        /* Constrain parsing to THIS object; JsonInit uses strlen() which
         * would otherwise walk into the next JSON object and match the
         * first `name`/`id` it finds, producing mangled rows. */
        p.length = obj_len;
        char str[160]; LONG num;

        p.position = 0;
        if (U64_JsonFindKey(&p, "name") && U64_JsonGetString(&p, str, sizeof(str))) {
            /* Cap name to ~45 chars so a chatty title doesn't push the
             * Group/Added/Year/Category columns off-screen. */
            sanitize_field(item->name, sizeof(item->name), str, 45);
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "id") && U64_JsonGetString(&p, str, sizeof(str))) {
            /* id is numeric-ASCII but sanitise anyway. */
            sanitize_field(item->id, sizeof(item->id), str, sizeof(item->id) - 4);
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "group") && U64_JsonGetString(&p, str, sizeof(str))) {
            /* Groups can be multi-name lists joined by commas; cap hard. */
            sanitize_field(item->group, sizeof(item->group), str, 24);
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "category") && U64_JsonGetNumber(&p, &num)) {
            item->category = (UWORD)num;
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "year") && U64_JsonGetNumber(&p, &num)) {
            item->year = (UWORD)num;
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "rating") && U64_JsonGetNumber(&p, &num)) {
            item->rating = (UBYTE)num;
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "updated") && U64_JsonGetString(&p, str, sizeof(str))) {
            /* ISO date — but sanitise defensively in case of weird data. */
            sanitize_field(item->updated, sizeof(item->updated), str,
                           sizeof(item->updated) - 4);
        }

        /* Pre-render the display-cache fields so the MUI display hook has
         * no work to do except point at them. */
        if (item->year > 0)
            sprintf(item->year_str, "%u", (unsigned)item->year);
        else
            item->year_str[0] = '\0';
        {
            CONST_STRPTR cat = Asm_CategoryName(item->category);
            strncpy(item->cat_str, (char *)cat, sizeof(item->cat_str) - 1);
            item->cat_str[sizeof(item->cat_str) - 1] = '\0';
        }

        item->next = NULL;
        if (!head) head = tail = item;
        else       { tail->next = item; tail = item; }
        count++;
    }

    FreeVec(body);

    /* Deduplicate by (name, group, year) — Assembly64 aggregates multiple
     * source repos so the same release often appears with distinct ids (and
     * different categories). Keeping only the first occurrence stops rows
     * from repeating AND avoids the HTTP 500 users hit when clicking a
     * duplicate whose category is bound to a different repo. */
    {
        ULONG removed = 0;
        AsmItem **pp = &head;
        ULONG recount = 0;
        while (*pp) {
            AsmItem *cur = *pp;
            BOOL dup = FALSE;
            for (AsmItem *s = head; s != cur; s = s->next) {
                if (s->year == cur->year
                    && strcmp(s->name,  cur->name)  == 0
                    && strcmp(s->group, cur->group) == 0) {
                    dup = TRUE; break;
                }
            }
            if (dup) {
                *pp = cur->next;
                FreeVec(cur);
                removed++;
            } else {
                pp = &cur->next;
                recount++;
            }
        }
        count = recount;
        if (removed) ASM_LOG("deduped %lu cross-repo duplicates", (unsigned long)removed);
    }

    *out_list = head;
    *out_count = count;
    ASM_LOG("parsed %lu items", (unsigned long)count);
    return U64_OK;
}

/* ------------------------------------------------------------------ */
/* List files in an entry                                              */
/* ------------------------------------------------------------------ */

LONG
Asm_ListFiles(CONST_STRPTR itemId, ULONG categoryId,
              AsmFile **out_list, ULONG *out_count)
{
    if (!itemId || !out_list || !out_count) return U64_ERR_INVALID;
    *out_list = NULL;
    *out_count = 0;

    /* Use %lu (unsigned long) to dodge any m68k varargs quirk where %u on
     * a 16-bit source reads the wrong stack half and produces /0 URLs. */
    char url[512];
    sprintf(url, "%s/search/entries/%s/%lu",
            ASM_BASE, (char *)itemId, (unsigned long)categoryId);
    ASM_LOG("GET %s", url);

    UBYTE *body = NULL;
    ULONG body_size = 0;
    UWORD status = 0;
    LONG result = U64_HttpGetURL((CONST_STRPTR)url,
                                 (CONST_STRPTR)ASM_HEADERS,
                                 &body, &body_size, &status);
    ASM_LOG("files HTTP rc=%ld status=%u bytes=%lu",
            (long)result, (unsigned)status, (unsigned long)body_size);
    if (result != U64_OK) {
        if (body) {
            ASM_LOG("error body: %.120s", (char *)body);
            FreeVec(body);
        }
        return status ? -(LONG)(1000 + status) : result;
    }
    if (!body || body_size == 0) { if (body) FreeVec(body); return U64_ERR_GENERAL; }
    ASM_LOG("files body head: %.120s", (char *)body);

    /* Shape: {"contentEntry":[ {...}, {...} ]} — the array lives under the
     * contentEntry key. Easiest path: locate the key text, then start our
     * brace-walker at that offset. */
    char *arr = strstr((char *)body, "\"contentEntry\"");
    if (!arr) {
        ASM_LOG("no contentEntry key in response");
        FreeVec(body);
        return U64_ERR_GENERAL;
    }
    ULONG scan = (ULONG)((UBYTE *)arr - body);
    ULONG body_len = body_size;

    AsmFile *head = NULL;
    AsmFile *tail = NULL;
    ULONG count = 0;
    ULONG obj_start, obj_len;

    while (next_json_object((char *)body, body_len, &scan, &obj_start, &obj_len)) {
        AsmFile *f = AllocVec(sizeof(AsmFile), MEMF_PUBLIC | MEMF_CLEAR);
        if (!f) break;

        JsonParser p;
        U64_JsonInit(&p, (CONST_STRPTR)(body + obj_start));
        p.length = obj_len;      /* constrain to this object only */
        char str[160]; LONG num;

        p.position = 0;
        if (U64_JsonFindKey(&p, "path") && U64_JsonGetString(&p, str, sizeof(str))) {
            /* Server filenames are ASCII in practice, but sanitize defensively
             * and cap the display length so a long path doesn't stretch the
             * File column and clip the Size. */
            sanitize_field(f->path, sizeof(f->path), str, 50);
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "id") && U64_JsonGetNumber(&p, &num)) {
            f->id = (ULONG)num;
        }
        p.position = 0;
        if (U64_JsonFindKey(&p, "size") && U64_JsonGetNumber(&p, &num)) {
            f->size = (ULONG)num;
        }

        /* Pre-render a size string for the display hook. */
        if (f->size >= 1024)
            sprintf(f->size_str, "%lu KB", (unsigned long)(f->size / 1024));
        else
            sprintf(f->size_str, "%lu B",  (unsigned long)f->size);

        f->next = NULL;
        if (!head) head = tail = f;
        else       { tail->next = f; tail = f; }
        count++;
    }

    FreeVec(body);
    *out_list = head;
    *out_count = count;
    ASM_LOG("files parsed=%lu", (unsigned long)count);
    return U64_OK;
}

/* ------------------------------------------------------------------ */
/* Download                                                            */
/* ------------------------------------------------------------------ */

static void
derive_extension(CONST_STRPTR name, char *out, ULONG out_size)
{
    out[0] = '\0';
    if (!name) return;
    const char *dot = strrchr((char *)name, '.');
    if (!dot) return;
    strncpy(out, dot, out_size - 1);
    out[out_size - 1] = '\0';
}

/* Optional progress callback for long downloads. Registered from the
 * UI so the status line can tick as bytes arrive — without it the
 * user can't tell a slow 200KB .d64 download on AmigaOS emulation
 * (often 30+ seconds) from a real hang. */
static void (*g_asm_progress_cb)(ULONG, APTR) = NULL;
static APTR  g_asm_progress_ud = NULL;

void
Asm_SetProgressCallback(void (*cb)(ULONG bytes, APTR userdata),
                        APTR userdata)
{
    g_asm_progress_cb = cb;
    g_asm_progress_ud = userdata;
}

LONG
Asm_DownloadFile(CONST_STRPTR itemId, ULONG categoryId, ULONG fileId,
                 CONST_STRPTR suggested_name,
                 char *out_path, ULONG out_path_size)
{
    if (!itemId || !out_path) return U64_ERR_INVALID;

    /* Build a unique-ish temp path under T:. Collisions are possible but
     * unlikely for interactive use; a monotonically-increasing counter
     * guarantees uniqueness within one session. */
    static ULONG counter = 0;
    counter++;

    char ext[16];
    derive_extension(suggested_name, ext, sizeof(ext));
    if (ext[0] == '\0') strcpy(ext, ".bin");

    snprintf(out_path, out_path_size, "T:u64asm_%lu%s",
             (unsigned long)counter, ext);

    char url[512];
    sprintf(url, "%s/search/bin/%s/%lu/%lu",
            ASM_BASE, (char *)itemId,
            (unsigned long)categoryId, (unsigned long)fileId);

    return U64_DownloadToFileEx((CONST_STRPTR)url, (CONST_STRPTR)out_path,
                                (CONST_STRPTR)ASM_HEADERS,
                                g_asm_progress_cb, g_asm_progress_ud);
}

/* ------------------------------------------------------------------ */
/* CSDB screenshot lookup                                              */
/* ------------------------------------------------------------------ */

/* Category ids that came from the CSDB feed. For anything else the
 * Assembly64 item id does NOT match a CSDB release id and the lookup
 * would return someone else's release (or 404). */
static BOOL
category_is_csdb(ULONG categoryId)
{
    /* 0..10 are CSDB games/demos/c128/graphics/music/mags/bbs/misc/tools/
     * charts/easyflash, 25 is CSDB REU. All others (11 c64.org intros,
     * 14-15 c64.com, 16 Gamebase, 17 SEUCK, 18-21 HVSC, 22 Mayhem,
     * 23-24 Preservers, 33 OneLoad64, 36-40 Commodore, etc.) are not
     * CSDB and don't share the id space. */
    return (categoryId <= 10) || (categoryId == 25);
}

/* ------------------------------------------------------------------ */
/* Minimal IFF ILBM writer — uncompressed, palette-based                */
/* ------------------------------------------------------------------ */

/* Big-endian write helpers. m68k is already big-endian so a straight
 * Write() of the storage bytes is correct; we keep explicit byte-swap
 * code anyway for clarity and future-proofing. */
static LONG write_be32(BPTR fh, ULONG v)
{
    UBYTE b[4];
    b[0]=(UBYTE)(v>>24); b[1]=(UBYTE)(v>>16);
    b[2]=(UBYTE)(v>>8);  b[3]=(UBYTE)v;
    return Write(fh, b, 4);
}
static LONG write_be16(BPTR fh, UWORD v)
{
    UBYTE b[2];
    b[0]=(UBYTE)(v>>8); b[1]=(UBYTE)v;
    return Write(fh, b, 2);
}

/* Serialize `bm` as an uncompressed IFF ILBM file. Palette is
 * `cregs[ncolors]` (r,g,b bytes per entry). Returns 0 on success,
 * non-zero on any Write() short-count. */
static LONG
write_ilbm(BPTR fh, struct BitMap *bm, UWORD w, UWORD h, UBYTE depth,
           struct ColorRegister *cregs, UWORD ncolors)
{
    ULONG row_bytes = bm->BytesPerRow;   /* already word-aligned by AllocBitMap */
    ULONG body_size = row_bytes * (ULONG)depth * (ULONG)h;
    ULONG cmap_size = 3UL * (ULONG)ncolors;
    ULONG cmap_pad  = (cmap_size & 1) ? 1 : 0;
    ULONG body_pad  = (body_size & 1) ? 1 : 0;
    /* FORM size excludes the first 8 bytes ("FORM"+size). */
    ULONG form_size = 4 /* "ILBM" */
                    + 8 + 20 /* BMHD */
                    + 8 + cmap_size + cmap_pad  /* CMAP */
                    + 8 + body_size + body_pad; /* BODY */

    if (Write(fh, "FORM", 4) != 4) return 1;
    if (write_be32(fh, form_size) != 4) return 1;
    if (Write(fh, "ILBM", 4) != 4) return 1;

    /* BMHD — 20 fixed bytes per the IFF ILBM spec. */
    if (Write(fh, "BMHD", 4) != 4) return 1;
    if (write_be32(fh, 20) != 4) return 1;
    if (write_be16(fh, w) != 2) return 1;
    if (write_be16(fh, h) != 2) return 1;
    if (write_be16(fh, 0) != 2) return 1; /* x */
    if (write_be16(fh, 0) != 2) return 1; /* y */
    UBYTE bmhd_tail[12];
    bmhd_tail[0]=depth; bmhd_tail[1]=0;    /* nPlanes, masking */
    bmhd_tail[2]=0;     bmhd_tail[3]=0;    /* compression=0 (none), pad */
    bmhd_tail[4]=0;     bmhd_tail[5]=0;    /* transparent colour */
    bmhd_tail[6]=1;     bmhd_tail[7]=1;    /* xAspect, yAspect */
    bmhd_tail[8]=(UBYTE)(w>>8); bmhd_tail[9]=(UBYTE)w;   /* pageWidth  */
    bmhd_tail[10]=(UBYTE)(h>>8); bmhd_tail[11]=(UBYTE)h; /* pageHeight */
    if (Write(fh, bmhd_tail, 12) != 12) return 1;

    /* CMAP — one (r,g,b) triplet per palette entry. */
    if (Write(fh, "CMAP", 4) != 4) return 1;
    if (write_be32(fh, cmap_size) != 4) return 1;
    for (UWORD i = 0; i < ncolors; i++) {
        UBYTE rgb[3];
        rgb[0] = cregs[i].red;
        rgb[1] = cregs[i].green;
        rgb[2] = cregs[i].blue;
        if (Write(fh, rgb, 3) != 3) return 1;
    }
    if (cmap_pad) { UBYTE z = 0; Write(fh, &z, 1); }

    /* BODY — uncompressed, row-interleaved per plane. */
    if (Write(fh, "BODY", 4) != 4) return 1;
    if (write_be32(fh, body_size) != 4) return 1;
    for (UWORD row = 0; row < h; row++) {
        for (UBYTE pl = 0; pl < depth; pl++) {
            UBYTE *pd = (UBYTE *)bm->Planes[pl] + row * bm->BytesPerRow;
            if (Write(fh, pd, row_bytes) != (LONG)row_bytes) return 1;
        }
    }
    if (body_pad) { UBYTE z = 0; Write(fh, &z, 1); }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Picture-datatype-based scale-to-fit                                 */
/* ------------------------------------------------------------------ */

/* Specific failure codes so the caller can surface which step went
 * wrong — this helps distinguish "no datatypes" from "truecolor source
 * without palette" without building a debug binary. Positive rc == ok. */
#define SCALE_ERR_LIB         1   /* OpenLibrary("datatypes.library",39) failed */
#define SCALE_ERR_SCREEN      2   /* LockPubScreen(NULL) failed                  */
#define SCALE_ERR_DTOBJ       3   /* NewDTObject failed (unknown format etc.)   */
#define SCALE_ERR_NO_BMH      4   /* PDTA_BitMapHeader not available            */
#define SCALE_ERR_NO_BITMAP   5   /* No planar BitMap from the DT object        */
#define SCALE_ERR_NO_PALETTE  6   /* Source is truecolor / no palette           */
#define SCALE_ERR_ALLOC       7   /* AllocBitMap failed                         */
#define SCALE_ERR_OPEN        8   /* Open(out_path, MODE_NEWFILE) failed        */
#define SCALE_ERR_WRITE       9   /* write_ilbm short-wrote                     */

/* Load `in_path` via picture.datatype, scale the raw bitmap to at most
 * `max_dim` on its longer side (aspect-preserving), and write the
 * result as uncompressed IFF ILBM to `out_path`. Returns U64_OK on
 * success, otherwise one of the SCALE_ERR_* codes above (positive).
 *
 * Strategy: lock the public screen so picture.datatype has a render
 * context to remap truecolor sources (PNG, JPEG) down to a planar
 * palettised bitmap. Without a screen, modern datatypes hand back a
 * chunky/RGB buffer that BitMapScale can't touch. */
static LONG
scale_image_file(CONST_STRPTR in_path, CONST_STRPTR out_path, UWORD max_dim)
{
    if (!in_path || !out_path || max_dim == 0) return U64_ERR_INVALID;

    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    if (!DataTypesBase) return SCALE_ERR_LIB;

    LONG rc = SCALE_ERR_DTOBJ;
    /* Load without screen remap. The pattern that works (per
     * NetSurf's amiga/bitmap.c and the picture.datatype autodoc):
     *   1. NewDTObject with PDTA_Remap=FALSE — keeps the source
     *      palette so PDTA_BitMap and PDTA_ColorRegisters line up 1:1.
     *   2. DTM_FRAMEBOX — REQUIRED before DTM_PROCLAYOUT, otherwise
     *      the bitmap never gets realised and PDTA_BitMap stays NULL.
     *   3. DTM_PROCLAYOUT with Initial=1 — actually materialises the
     *      source bitmap.
     *   4. GetDTAttrs for PDTA_BitMap + PDTA_ColorRegisters. */
    Object *dt = NewDTObject((APTR)in_path,
                             DTA_SourceType, DTST_FILE,
                             DTA_GroupID,    GID_PICTURE,
                             PDTA_Remap,     FALSE,
                             TAG_DONE);
    if (dt) {
        struct FrameInfo fi_in, fi_out;
        memset(&fi_in, 0, sizeof(fi_in));
        memset(&fi_out, 0, sizeof(fi_out));
        DoMethod(dt, DTM_FRAMEBOX, NULL, (ULONG)&fi_in, (ULONG)&fi_out,
                 sizeof(fi_out), 0);
        DoMethod(dt, DTM_PROCLAYOUT, NULL, 1L);

        struct BitMapHeader   *bmh    = NULL;
        struct BitMap         *srcBm  = NULL;
        struct ColorRegister  *cregs  = NULL;
        ULONG                  ncolors = 0;

        GetDTAttrs(dt,
                   PDTA_BitMapHeader,   (ULONG)&bmh,
                   PDTA_BitMap,         (ULONG)&srcBm,
                   PDTA_ColorRegisters, (ULONG)&cregs,
                   PDTA_NumColors,      (ULONG)&ncolors,
                   TAG_DONE);

        ASM_LOG("DT loaded: bmh=%p src=%p cregs=%p ncolors=%lu depth=%u",
                (void *)bmh, (void *)srcBm, (void *)cregs,
                (unsigned long)ncolors,
                (unsigned)(bmh ? bmh->bmh_Depth : 0));

        if (!bmh)                        rc = SCALE_ERR_NO_BMH;
        else if (!srcBm)                 rc = SCALE_ERR_NO_BITMAP;
        else if (!cregs || ncolors == 0) rc = SCALE_ERR_NO_PALETTE;
        else if (bmh->bmh_Width == 0 || bmh->bmh_Height == 0)
                                         rc = SCALE_ERR_NO_BITMAP;
        else {
            UWORD ow = bmh->bmh_Width, oh = bmh->bmh_Height;
            UBYTE depth = srcBm->Depth;
            if (depth == 0) depth = bmh->bmh_Depth;
            UWORD nw, nh;

            /* Integer-divisor downscale. BitMapScale is nearest-
             * neighbour; non-integer ratios produce an uneven grid.
             * Integer ratios give a clean pixel-halved look. */
            UWORD dw = (UWORD)((ow + max_dim - 1) / max_dim);
            UWORD dh = (UWORD)((oh + max_dim - 1) / max_dim);
            UWORD divisor = dw > dh ? dw : dh;
            if (divisor < 1) divisor = 1;
            nw = (UWORD)(ow / divisor);
            nh = (UWORD)(oh / divisor);
            if (nw == 0) nw = 1;
            if (nh == 0) nh = 1;
            ASM_LOG("scale %ux%u -> %ux%u depth=%u divisor=%u",
                    (unsigned)ow, (unsigned)oh,
                    (unsigned)nw, (unsigned)nh,
                    (unsigned)depth, (unsigned)divisor);

            struct BitMap *dstBm = AllocBitMap(nw, nh, depth,
                                               BMF_CLEAR, NULL);
            if (!dstBm) rc = SCALE_ERR_ALLOC;
            else {
                struct BitScaleArgs bsa;
                memset(&bsa, 0, sizeof(bsa));
                bsa.bsa_SrcX = 0;   bsa.bsa_SrcY = 0;
                bsa.bsa_SrcWidth  = ow; bsa.bsa_SrcHeight = oh;
                /* Reduced ratio: /2 downscale => Src=2, Dest=1. */
                bsa.bsa_XSrcFactor = divisor; bsa.bsa_XDestFactor = 1;
                bsa.bsa_YSrcFactor = divisor; bsa.bsa_YDestFactor = 1;
                bsa.bsa_DestX = 0;  bsa.bsa_DestY = 0;
                bsa.bsa_SrcBitMap  = srcBm;
                bsa.bsa_DestBitMap = dstBm;
                BitMapScale(&bsa);

                BPTR fh = Open((STRPTR)out_path, MODE_NEWFILE);
                if (!fh) rc = SCALE_ERR_OPEN;
                else {
                    /* PDTA_BitMap indices match PDTA_ColorRegisters
                     * entries 1:1 — write CMAP straight from cregs. */
                    struct ColorRegister *pal = cregs;
                    UWORD npal = (UWORD)ncolors;
                    if (write_ilbm(fh, dstBm, nw, nh, depth,
                                   pal, npal) == 0) {
                        rc = U64_OK;
                    } else rc = SCALE_ERR_WRITE;
                    Close(fh);
                    if (rc != U64_OK) DeleteFile((STRPTR)out_path);
                }
                FreeBitMap(dstBm);
            }
        }
        DisposeDTObject(dt);
    }

    CloseLibrary(DataTypesBase);
    DataTypesBase = NULL;
    return rc;
}

LONG
Asm_FetchScreenshot(CONST_STRPTR itemId, ULONG categoryId,
                    char *out_path, ULONG out_path_size)
{
    if (!itemId || !out_path || out_path_size < 32) return U64_ERR_INVALID;
    out_path[0] = '\0';

    if (!category_is_csdb(categoryId)) return U64_ERR_NOTFOUND;

    /* 1. Fetch the release metadata from CSDB. */
    char xml_url[256];
    sprintf(xml_url, "http://csdb.dk/webservice/?type=release&depth=1&id=%s",
            (char *)itemId);

    UBYTE *xml = NULL;
    ULONG xml_size = 0;
    UWORD xml_status = 0;
    LONG rc = U64_HttpGetURL((CONST_STRPTR)xml_url, NULL,
                             &xml, &xml_size, &xml_status);
    if (rc != U64_OK || !xml || xml_size == 0) {
        if (xml) FreeVec(xml);
        /* Network failure, not "no screenshot" — caller distinguishes by
         * this specific code. */
        return U64_ERR_NETWORK;
    }

    /* 2. Extract <ScreenShot>URL</ScreenShot> — a plain substring scan is
     * enough, CSDB's XML is simple and the tag is unique. */
    const char *open_tag = "<ScreenShot>";
    const char *close_tag = "</ScreenShot>";
    char *start = strstr((char *)xml, open_tag);
    if (!start) { FreeVec(xml); return U64_ERR_NOTFOUND; }
    start += strlen(open_tag);
    char *end = strstr(start, close_tag);
    if (!end || end <= start) { FreeVec(xml); return U64_ERR_NOTFOUND; }

    char img_url[512];
    ULONG url_len = (ULONG)(end - start);
    if (url_len >= sizeof(img_url)) url_len = sizeof(img_url) - 1;
    CopyMem(start, img_url, url_len);
    img_url[url_len] = '\0';
    FreeVec(xml);

    /* Strip any whitespace the XML might have around the URL. */
    while (img_url[0] == ' ' || img_url[0] == '\t' || img_url[0] == '\r' || img_url[0] == '\n') {
        memmove(img_url, img_url + 1, strlen(img_url));
    }
    ULONG il = strlen(img_url);
    while (il > 0 && (img_url[il-1] == ' ' || img_url[il-1] == '\t'
                      || img_url[il-1] == '\r' || img_url[il-1] == '\n')) {
        img_url[--il] = '\0';
    }

    /* If CSDB returns an https URL we can't fetch it — record that we
     * saw a URL but couldn't retrieve it so the caller can show the URL
     * in its status message for manual follow-up. */
    if (strncmp(img_url, "http://", 7) != 0) {
        /* Hand the unreachable URL back in out_path prefixed with "!" so
         * the caller can surface it without mistaking it for a filename. */
        snprintf(out_path, out_path_size, "!%s", img_url);
        return U64_ERR_INVALID;
    }

    /* 3. Derive local extension from the URL (.png / .gif / .jpg). */
    const char *dot = strrchr(img_url, '.');
    char ext[8]; ext[0] = '\0';
    if (dot && strlen(dot) < sizeof(ext)) {
        ULONG i;
        for (i = 0; dot[i] && i < sizeof(ext) - 1; i++) {
            char c = dot[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.')
                ext[i] = c;
            else
                break;
        }
        ext[i] = '\0';
    }
    if (ext[0] == '\0') strcpy(ext, ".img");

    /* Rotate the filename every fetch so the Dtpic widget sees a new
     * MUIA_Dtpic_Name string and actually reloads the image — otherwise
     * it caches by filename and the preview sticks on the first entry
     * you viewed. Delete the previous temp to keep T: tidy. */
    static ULONG ss_counter = 0;
    static char  prev_ss_path[96] = { 0 };
    ss_counter++;
    snprintf(out_path, out_path_size, "T:u64asm_preview_%lu%s",
             (unsigned long)ss_counter, ext);

    LONG dl_rc = U64_DownloadToFileEx((CONST_STRPTR)img_url,
                                      (CONST_STRPTR)out_path,
                                      NULL, NULL, NULL);

    /* Scale the freshly downloaded image down to a display-friendly size.
     * CSDB screenshots are typically C64-native 384x272 which still
     * overflows a 320x200 WB or sits awkwardly in the tab. We aim for
     * max 300px on the longer side, preserving aspect. The scaled copy
     * goes to a separate .iff filename so Dtpic sees a brand-new path
     * (it caches by name). If scaling fails — old datatypes, missing
     * format handler, etc. — we keep the unscaled file AND tack the
     * scale error code onto the output filename so the caller can show
     * which step failed; the caller strips it before handing the path
     * to Dtpic. */
    if (dl_rc == U64_OK) {
        char scaled_path[96];
        snprintf(scaled_path, sizeof(scaled_path),
                 "T:u64asm_preview_%lu.iff", (unsigned long)ss_counter);
        LONG scale_rc = scale_image_file((CONST_STRPTR)out_path,
                                         (CONST_STRPTR)scaled_path, 300);
        if (scale_rc == U64_OK) {
            DeleteFile((STRPTR)out_path);
            strncpy(out_path, scaled_path, out_path_size - 1);
            out_path[out_path_size - 1] = '\0';
        } else {
            /* Leave the unscaled original as out_path but leave a
             * breadcrumb under a fixed name so the user can see which
             * step of scaling failed without a debug build. */
            BPTR erh = Open((STRPTR)"T:u64asm_preview_scale_err",
                            MODE_NEWFILE);
            if (erh) {
                char line[32];
                snprintf(line, sizeof(line), "scale rc=%ld\n", (long)scale_rc);
                Write(erh, line, strlen(line));
                Close(erh);
            }
        }
    }

    if (prev_ss_path[0]) DeleteFile((STRPTR)prev_ss_path);
    strncpy(prev_ss_path, out_path, sizeof(prev_ss_path) - 1);
    prev_ss_path[sizeof(prev_ss_path) - 1] = '\0';

    return dl_rc;
}
