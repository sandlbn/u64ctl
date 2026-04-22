#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/types.h>

#include <proto/dos.h>
#include <proto/exec.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "player.h"      /* for MD5Compare, HexStringToMD5, MD5ToHexString */
#include "md5set.h"

static UWORD
md5set_bucket(const UBYTE md5[16])
{
    return (UWORD)md5[0];           /* 0..255 */
}

struct MD5Set *
MD5Set_Create(void)
{
    return AllocVec(sizeof(struct MD5Set), MEMF_PUBLIC | MEMF_CLEAR);
}

void
MD5Set_Free(struct MD5Set *set)
{
    if (!set) return;
    for (int i = 0; i < MD5SET_BUCKETS; i++) {
        MD5SetEntry *e = set->buckets[i];
        while (e) {
            MD5SetEntry *next = e->next;
            FreeVec(e);
            e = next;
        }
    }
    FreeVec(set);
}

BOOL
MD5Set_Contains(struct MD5Set *set, const UBYTE md5[16])
{
    if (!set) return FALSE;
    for (MD5SetEntry *e = set->buckets[md5set_bucket(md5)]; e; e = e->next) {
        if (MD5Compare(e->md5, md5)) return TRUE;
    }
    return FALSE;
}

BOOL
MD5Set_Insert(struct MD5Set *set, const UBYTE md5[16])
{
    if (!set) return FALSE;
    UWORD b = md5set_bucket(md5);
    for (MD5SetEntry *e = set->buckets[b]; e; e = e->next) {
        if (MD5Compare(e->md5, md5)) return FALSE;
    }
    MD5SetEntry *fresh = AllocVec(sizeof(MD5SetEntry), MEMF_PUBLIC);
    if (!fresh) return FALSE;
    CopyMem((APTR)md5, fresh->md5, 16);
    fresh->next = set->buckets[b];
    set->buckets[b] = fresh;
    set->count++;
    set->dirty = TRUE;
    return TRUE;
}

BOOL
MD5Set_Remove(struct MD5Set *set, const UBYTE md5[16])
{
    if (!set) return FALSE;
    UWORD b = md5set_bucket(md5);
    MD5SetEntry **pp = &set->buckets[b];
    while (*pp) {
        if (MD5Compare((*pp)->md5, md5)) {
            MD5SetEntry *victim = *pp;
            *pp = victim->next;
            FreeVec(victim);
            set->count--;
            set->dirty = TRUE;
            return TRUE;
        }
        pp = &(*pp)->next;
    }
    return FALSE;
}

BOOL
MD5Set_Load(struct MD5Set *set, CONST_STRPTR path)
{
    if (!set || !path) return FALSE;

    BPTR file = Open(path, MODE_OLDFILE);
    if (!file) return FALSE;            /* missing file is not an error */

    char line[64];
    while (FGets(file, line, sizeof(line))) {
        /* Strip trailing whitespace */
        char *p = line + strlen(line);
        while (p > line && (p[-1] == '\n' || p[-1] == '\r'
                            || p[-1] == ' '  || p[-1] == '\t')) {
            *--p = '\0';
        }
        if (strlen(line) != 32) continue;

        UBYTE md5[16];
        if (!HexStringToMD5(line, md5)) continue;

        /* Insert without flipping dirty (we just loaded from disk). */
        UWORD b = md5set_bucket(md5);
        BOOL exists = FALSE;
        for (MD5SetEntry *e = set->buckets[b]; e; e = e->next) {
            if (MD5Compare(e->md5, md5)) { exists = TRUE; break; }
        }
        if (exists) continue;

        MD5SetEntry *fresh = AllocVec(sizeof(MD5SetEntry), MEMF_PUBLIC);
        if (!fresh) break;
        CopyMem(md5, fresh->md5, 16);
        fresh->next = set->buckets[b];
        set->buckets[b] = fresh;
        set->count++;
    }

    Close(file);
    set->dirty = FALSE;
    return TRUE;
}

BOOL
MD5Set_Save(struct MD5Set *set, CONST_STRPTR path)
{
    if (!set || !path) return FALSE;
    if (!set->dirty) return TRUE;       /* nothing changed */

    BPTR file = Open(path, MODE_NEWFILE);
    if (!file) return FALSE;

    char hex[33];
    for (int i = 0; i < MD5SET_BUCKETS; i++) {
        for (MD5SetEntry *e = set->buckets[i]; e; e = e->next) {
            MD5ToHexString(e->md5, hex);
            FPuts(file, hex);
            FPuts(file, "\n");
        }
    }
    Close(file);
    set->dirty = FALSE;
    return TRUE;
}
