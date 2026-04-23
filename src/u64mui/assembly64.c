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

#include <proto/dos.h>
#include <proto/exec.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"
#include "assembly64.h"

#define ASM_BASE "http://hackerswithstyle.se/leet"
#define ASM_HEADERS "client-id: u64manager\r\n"

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
        /* Escape only what commonly breaks query strings; leave AQL's
         * ':' alone so `name:elite` stays readable on the wire. */
        if (c == ' ' || c == '"' || c == '#' || c == '&' || c == '+'
            || c == '%' || c < 0x20 || c >= 0x7F) {
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
                                (CONST_STRPTR)ASM_HEADERS, NULL, NULL);
}
