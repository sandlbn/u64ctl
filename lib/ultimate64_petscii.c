/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * PETSCII conversion implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <string.h>

#include "ultimate64_amiga.h"

/* PETSCII to Unicode mapping table */
static const UBYTE petscii_to_ascii[256] = {
  /* 0x00-0x1F: Control codes */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,

  /* 0x20-0x3F: Space, punctuation, numbers */
  ' ', '!', '"', '#', '$', '%', '&', '\'', '(', ')', '*', '+', ',', '-', '.',
  '/', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=',
  '>', '?',

  /* 0x40-0x5F: @, PETSCII uppercase A-Z, special chars */
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', '\\', ']',
  '^', '_',

  /* 0x60-0x7F: Special chars */
  '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}',
  '~', 0,

  /* 0x80-0xBF: Graphics characters - map to spaces/symbols for display */
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ',

  /* 0xC0-0xDF: PETSCII lowercase letters and symbols */
  ' ', /* 0xC0 */
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o',                                                   /* 0xC1-0xCF */
  'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', /* 0xD0-0xDA */
  ' ', ' ', ' ', ' ', ' ',                               /* 0xDB-0xDF */

  /* 0xE0-0xFF: More graphics and special characters */
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  ' ', ' '
};

/* ASCII to PETSCII mapping for common characters */
static const UBYTE ascii_to_petscii[128] = {
  /* 0x00-0x1F: Control codes */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0D, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, /* \n (0x0A) -> RETURN (0x0D) */
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
  0x1D, 0x1E, 0x1F,

  /* 0x20-0x3F: Space, punctuation, numbers */
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, /* !"#$%&' */
  0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, /* ()*+,-./ */
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, /* 01234567 */
  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, /* 89:;<=>? */

  /* 0x40-0x5F: @, uppercase A-Z (these should map to PETSCII
     uppercase) */
  0x40, /* @ -> @ */
  0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
  0x47, /* A-G -> A-G (PETSCII uppercase) */
  0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
  0x4F, /* H-O -> H-O (PETSCII uppercase) */
  0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,
  0x57,                         /* P-W -> P-W (PETSCII uppercase) */
  0x58, 0x59, 0x5A,             /* X-Z -> X-Z (PETSCII uppercase) */
  0x5B, 0x5C, 0x5D, 0x5E, 0x5F, /* [\]^_ */

  /* 0x60-0x7F: lowercase a-z (map to PETSCII lowercase which is 0xC1-0xDA) */
  0x60,                                     /* ` */
  0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, /* a-g -> PETSCII lowercase */
  0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE,
  0xCF, /* h-o -> PETSCII lowercase */
  0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
  0xD7,                        /* p-w -> PETSCII lowercase */
  0xD8, 0xD9, 0xDA,            /* x-z -> PETSCII lowercase */
  0x7B, 0x7C, 0x7D, 0x7E, 0x7F /* {|}~DEL */
};

/* Convert PETSCII to string */
UBYTE *
U64_StringToPETSCII (CONST_STRPTR str, ULONG *out_len)
{
  ULONG len;
  UBYTE *petscii;
  ULONG i;
  UBYTE c;

  if (!str)
    {
      if (out_len)
        *out_len = 0;
      return NULL;
    }

  len = strlen (str);
  if (out_len)
    {
      *out_len = len;
    }

  /* Allocate buffer for PETSCII string */
  petscii = AllocMem (len + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!petscii)
    {
      if (out_len)
        *out_len = 0;
      return NULL;
    }

  /* Convert each character */
  for (i = 0; i < len; i++)
    {
      c = (UBYTE)str[i];

      if (c < 128)
        {
          petscii[i] = ascii_to_petscii[c];
        }
      else
        {
          /* Non-ASCII character, use space */
          petscii[i] = 0x20; /* Space */
        }

      /* Debug output for troubleshooting */
    }

  return petscii;
}

/* Free PETSCII buffer */
void
U64_FreePETSCII (UBYTE *petscii)
{
  if (petscii)
    {
      ULONG len = strlen ((char *)petscii);
      FreeMem (petscii, len + 1);
    }
}

/* Free string buffer allocated by U64_PETSCIIToString */
void
U64_FreeString (STRPTR str)
{
  if (str)
    {
      ULONG len = strlen (str);
      FreeMem (str, len + 1);
    }
}