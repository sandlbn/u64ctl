#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>

#include <string.h>

#include "string_utils.h"

STRPTR
U64_ConvertEscapeSequences(CONST_STRPTR input)
{
  STRPTR output;
  ULONG input_len;
  ULONG i, j;

  if (!input)
    return NULL;

  input_len = strlen(input);

  output = AllocVec(input_len + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!output)
    return NULL;

  for (i = 0, j = 0; i < input_len; i++)
    {
      if (input[i] == '\\' && i + 1 < input_len)
        {
          switch (input[i + 1])
            {
            case 'n':  output[j++] = '\n'; i++; break;
            case 'r':  output[j++] = '\r'; i++; break;
            case 't':  output[j++] = '\t'; i++; break;
            case '\\': output[j++] = '\\'; i++; break;
            case '"':  output[j++] = '"';  i++; break;
            case '0':  output[j++] = '\0'; i++; break;
            default:
              output[j++] = input[i];
              break;
            }
        }
      else
        {
          output[j++] = input[i];
        }
    }

  output[j] = '\0';
  return output;
}

STRPTR
U64_SafeStrDup(CONST_STRPTR str)
{
  STRPTR result;
  ULONG len;

  if (!str)
    return NULL;

  len = strlen(str) + 1;
  result = AllocVec(len, MEMF_PUBLIC | MEMF_CLEAR);
  if (result)
    strcpy(result, str);

  return result;
}

void
U64_SafeStrFree(STRPTR str)
{
  if (str)
    FreeVec(str);
}

STRPTR
U64_EscapeString(CONST_STRPTR str)
{
  ULONG len, escaped_len;
  STRPTR escaped;
  ULONG i, j;

  if (!str)
    return NULL;

  len = strlen(str);
  escaped_len = len * 2 + 1;

  escaped = AllocVec(escaped_len, MEMF_PUBLIC | MEMF_CLEAR);
  if (!escaped)
    return NULL;

  for (i = 0, j = 0; i < len && j < escaped_len - 2; i++)
    {
      if (str[i] == '"' || str[i] == '\\' || str[i] == '\n' || str[i] == '\r')
        {
          escaped[j++] = '\\';
          if (str[i] == '\n')
            escaped[j++] = 'n';
          else if (str[i] == '\r')
            escaped[j++] = 'r';
          else
            escaped[j++] = str[i];
        }
      else
        {
          escaped[j++] = str[i];
        }
    }
  escaped[j] = '\0';

  return escaped;
}

STRPTR
U64_UnescapeString(CONST_STRPTR str)
{
  ULONG len;
  STRPTR unescaped;
  ULONG i, j;

  if (!str)
    return NULL;

  len = strlen(str);
  unescaped = AllocVec(len + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!unescaped)
    return NULL;

  for (i = 0, j = 0; i < len; i++)
    {
      if (str[i] == '\\' && i + 1 < len)
        {
          i++;
          if (str[i] == 'n')
            unescaped[j++] = '\n';
          else if (str[i] == 'r')
            unescaped[j++] = '\r';
          else
            unescaped[j++] = str[i];
        }
      else
        {
          unescaped[j++] = str[i];
        }
    }
  unescaped[j] = '\0';

  return unescaped;
}
