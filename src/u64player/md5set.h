/* Small bucketed MD5 hash set backed by a newline-delimited hex text file.
 *
 * Used for two persistent player features:
 *   - "heard" tracker   (set of SIDs the user has ever played)
 *   - "favourites"      (set of SIDs the user hearted)
 *
 */

#ifndef U64_MD5SET_H
#define U64_MD5SET_H

#include <exec/types.h>

#define MD5SET_BUCKETS 256          /* 8 bits of MD5[0], 1 KB of pointers */

typedef struct MD5SetEntry
{
    UBYTE md5[16];                  /* raw 16 bytes, not hex */
    struct MD5SetEntry *next;
} MD5SetEntry;

struct MD5Set
{
    MD5SetEntry *buckets[MD5SET_BUCKETS];
    ULONG count;
    BOOL  dirty;                    /* TRUE if mutated since last save */
};

/* Allocate an empty set. Caller frees with MD5Set_Free. */
struct MD5Set *MD5Set_Create(void);

/* Destroy a set; NULL is safe. */
void MD5Set_Free(struct MD5Set *set);

/* TRUE if md5 is present in set. NULL set = FALSE. */
BOOL MD5Set_Contains(struct MD5Set *set, const UBYTE md5[16]);

/* Insert md5 if not already present. Returns TRUE when it's newly inserted
 * (in which case dirty is flipped on), FALSE if it was already there. */
BOOL MD5Set_Insert(struct MD5Set *set, const UBYTE md5[16]);

/* Remove md5. Returns TRUE if it was present. */
BOOL MD5Set_Remove(struct MD5Set *set, const UBYTE md5[16]);

/* Load the hex text file at path into set. Missing file = empty set.
 * Callable on a non-empty set (accumulates). */
BOOL MD5Set_Load(struct MD5Set *set, CONST_STRPTR path);

/* Persist set to path, one hex MD5 per line. Does nothing if !set->dirty.
 * Returns TRUE on success (or if nothing to write). */
BOOL MD5Set_Save(struct MD5Set *set, CONST_STRPTR path);

#endif
