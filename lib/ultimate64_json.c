/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * JSON parsing implementation with enhanced error handling and debugging
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Skip whitespace in JSON */
static void
JsonSkipWhitespace (JsonParser *parser)
{
  if (!parser || !parser->json)
    {
      return;
    }

  while (parser->position < parser->length)
    {
      char c = parser->json[parser->position];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
          break;
        }
      parser->position++;
    }
}

/* Initialize JSON parser */
BOOL
U64_JsonInit (JsonParser *parser, CONST_STRPTR json)
{
  if (!parser || !json)
    {
      return FALSE;
    }

  parser->json = json;
  parser->length = strlen (json);
  parser->position = 0;

  return TRUE;
}

/* Find key in JSON object */
BOOL
U64_JsonFindKey (JsonParser *parser, CONST_STRPTR key)
{
  ULONG key_len;
  ULONG start_pos;
  BOOL in_string = FALSE;
  BOOL escaped = FALSE;
  int brace_depth = 0;
  BOOL found_opening_brace = FALSE;

  if (!parser || !key)
    {
      U64_DEBUG ("Invalid parameters to JsonFindKey");
      return FALSE;
    }

  key_len = strlen (key);
  start_pos = parser->position;

  U64_DEBUG ("Searching for key '%s' starting at position %lu", key,
             (unsigned long)start_pos);

  /* First, make sure we're inside a JSON object */
  ULONG search_pos = parser->position;
  while (search_pos < parser->length)
    {
      if (parser->json[search_pos] == '{')
        {
          found_opening_brace = TRUE;
          break;
        }
      else if (!isspace (parser->json[search_pos]))
        {
          break;
        }
      search_pos++;
    }

  if (!found_opening_brace && parser->position == 0)
    {
      /* If we're at the start and haven't found a brace, look from current
       * position */
      U64_DEBUG ("No opening brace found, searching from current position");
    }

  /* Search for the key */
  while (parser->position < parser->length)
    {
      char c = parser->json[parser->position];

      /* Handle string state */
      if (escaped)
        {
          escaped = FALSE;
        }
      else if (c == '\\')
        {
          escaped = TRUE;
        }
      else if (c == '"')
        {
          if (!in_string)
            {
              /* Start of string - check if it's our key */
              ULONG string_start = parser->position + 1;

              U64_DEBUG ("Found string start at position %lu",
                         (unsigned long)parser->position);

              if (string_start + key_len < parser->length)
                {
                  if (strncmp (&parser->json[string_start], key, key_len) == 0
                      && parser->json[string_start + key_len] == '"')
                    {

                      U64_DEBUG ("Found potential key match at position %lu",
                                 (unsigned long)string_start);

                      /* Found the key, now find the colon */
                      parser->position = string_start + key_len
                                         + 1; /* Skip to after closing quote */
                      JsonSkipWhitespace (parser);

                      if (parser->position < parser->length
                          && parser->json[parser->position] == ':')
                        {
                          parser->position++; /* Skip colon */
                          JsonSkipWhitespace (parser);
                          U64_DEBUG ("Successfully found key '%s' with value "
                                     "starting at position %lu",
                                     key, (unsigned long)parser->position);
                          return TRUE;
                        }
                      else
                        {
                          U64_DEBUG (
                              "No colon found after key, continuing search");
                          /* Not followed by colon, continue searching */
                        }
                    }
                }
            }
          in_string = !in_string;
        }
      else if (!in_string)
        {
          /* Track brace depth */
          if (c == '{')
            {
              brace_depth++;
            }
          else if (c == '}')
            {
              brace_depth--;
              if (brace_depth < 0)
                {
                  /* End of current object */
                  U64_DEBUG ("Reached end of JSON object, key not found");
                  break;
                }
            }
        }

      parser->position++;
    }

  /* Key not found, restore position */
  parser->position = start_pos;
  U64_DEBUG ("Key '%s' not found, restored position to %lu", key,
             (unsigned long)start_pos);
  return FALSE;
}

/* Get string value from JSON */
BOOL
U64_JsonGetString (JsonParser *parser, STRPTR buffer, ULONG buffer_size)
{
  ULONG start;
  BOOL escaped = FALSE;
  ULONG out_pos = 0;

  if (!parser || !buffer || buffer_size == 0)
    {
      U64_DEBUG ("Invalid parameters to JsonGetString");
      return FALSE;
    }

  JsonSkipWhitespace (parser);

  /* Check for opening quote */
  if (parser->position >= parser->length
      || parser->json[parser->position] != '"')
    {
      U64_DEBUG ("No opening quote found at position %lu",
                 (unsigned long)parser->position);
      if (parser->position < parser->length)
        {
          U64_DEBUG ("Found character: '%c' (0x%02X)",
                     parser->json[parser->position],
                     (unsigned char)parser->json[parser->position]);
        }
      return FALSE;
    }

  parser->position++; /* Skip opening quote */
  start = parser->position;

  U64_DEBUG ("Starting string parse at position %lu", (unsigned long)start);

  /* Find closing quote and copy characters */
  while (parser->position < parser->length && out_pos < buffer_size - 1)
    {
      char c = parser->json[parser->position];

      if (escaped)
        {
          /* Handle escape sequences */
          switch (c)
            {
            case 'n':
              buffer[out_pos++] = '\n';
              break;
            case 'r':
              buffer[out_pos++] = '\r';
              break;
            case 't':
              buffer[out_pos++] = '\t';
              break;
            case '"':
              buffer[out_pos++] = '"';
              break;
            case '\\':
              buffer[out_pos++] = '\\';
              break;
            case '/':
              buffer[out_pos++] = '/';
              break;
            case 'b':
              buffer[out_pos++] = '\b';
              break;
            case 'f':
              buffer[out_pos++] = '\f';
              break;
            default:
              /* For unknown escape, just copy the character */
              buffer[out_pos++] = c;
              break;
            }
          escaped = FALSE;
        }
      else if (c == '\\')
        {
          escaped = TRUE;
        }
      else if (c == '"')
        {
          /* End of string */
          buffer[out_pos] = '\0';
          parser->position++; /* Skip closing quote */
          U64_DEBUG ("Successfully parsed string: '%s' (length %lu)", buffer,
                     (unsigned long)out_pos);
          return TRUE;
        }
      else
        {
          buffer[out_pos++] = c;
        }

      parser->position++;
    }

  /* String not properly terminated or buffer overflow */
  buffer[0] = '\0';
  U64_DEBUG (
      "String parsing failed - no closing quote found or buffer overflow");
  U64_DEBUG ("Position: %lu, Length: %lu, Buffer pos: %lu",
             (unsigned long)parser->position, (unsigned long)parser->length,
             (unsigned long)out_pos);
  return FALSE;
}
/* Get number value from JSON */
BOOL
U64_JsonGetNumber (JsonParser *parser, LONG *value)
{
  ULONG start;
  char number_str[32];
  ULONG num_len = 0;

  if (!parser || !value)
    {
      return FALSE;
    }

  JsonSkipWhitespace (parser);

  if (parser->position >= parser->length)
    {
      return FALSE;
    }

  start = parser->position;

  /* Check for negative sign */
  if (parser->json[parser->position] == '-')
    {
      number_str[num_len++] = '-';
      parser->position++;
    }

  /* Collect digits */
  while (parser->position < parser->length && num_len < 31)
    {
      char c = parser->json[parser->position];
      if (isdigit (c) || c == '.')
        {
          number_str[num_len++] = c;
          parser->position++;
        }
      else
        {
          break;
        }
    }

  if (num_len == 0)
    {
      parser->position = start;
      return FALSE;
    }

  number_str[num_len] = '\0';
  *value = atol (number_str);

  return TRUE;
}

/* Get boolean value from JSON */
BOOL
U64_JsonGetBool (JsonParser *parser, BOOL *value)
{
  if (!parser || !value)
    {
      return FALSE;
    }

  JsonSkipWhitespace (parser);

  if (parser->position + 4 <= parser->length
      && strncmp (&parser->json[parser->position], "true", 4) == 0)
    {
      *value = TRUE;
      parser->position += 4;
      return TRUE;
    }

  if (parser->position + 5 <= parser->length
      && strncmp (&parser->json[parser->position], "false", 5) == 0)
    {
      *value = FALSE;
      parser->position += 5;
      return TRUE;
    }

  return FALSE;
}

/* Free error array */
void
U64_FreeErrorArray (U64ErrorArray *errors)
{
  ULONG i;

  if (!errors)
    {
      return;
    }

  if (errors->errors)
    {
      for (i = 0; i < errors->error_count; i++)
        {
          if (errors->errors[i])
            {
              FreeMem (errors->errors[i], strlen (errors->errors[i]) + 1);
            }
        }
      FreeMem (errors->errors, sizeof (STRPTR) * errors->error_count);
    }

  errors->errors = NULL;
  errors->error_count = 0;
}

/* Parse JSON errors array */
LONG
U64_ParseErrorArray (CONST_STRPTR json, U64ErrorArray *error_array)
{
  JsonParser parser;
  BOOL in_string = FALSE;
  BOOL escaped = FALSE;
  char temp_error[512];
  STRPTR *temp_errors = NULL;
  ULONG temp_count = 0;
  ULONG temp_capacity = 4;
  ULONG string_start = 0;
  ULONG string_pos = 0;

  if (!json || !error_array)
    {
      return U64_ERR_INVALID;
    }

  /* Initialize error array */
  memset (error_array, 0, sizeof (U64ErrorArray));

  /* Initialize parser */
  if (!U64_JsonInit (&parser, json))
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Parsing JSON for errors array: %.200s", json);

  /* Find the errors array */
  if (!U64_JsonFindKey (&parser, "errors"))
    {
      U64_DEBUG ("No 'errors' field found in JSON");
      /* No errors field found - this might be OK */
      return U64_OK;
    }

  U64_DEBUG ("Found 'errors' field at position %lu",
             (unsigned long)parser.position);

  /* Skip whitespace */
  JsonSkipWhitespace (&parser);

  /* Check if it's an array */
  if (parser.position >= parser.length || parser.json[parser.position] != '[')
    {
      U64_DEBUG (
          "Errors field is not an array, trying to parse as single error");
      /* If it's not an array, try to parse as single error */
      if (U64_JsonGetString (&parser, temp_error, sizeof (temp_error)))
        {
          temp_errors = AllocMem (sizeof (STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
          if (temp_errors)
            {
              temp_errors[0] = AllocMem (strlen (temp_error) + 1, MEMF_PUBLIC);
              if (temp_errors[0])
                {
                  strcpy (temp_errors[0], temp_error);
                  error_array->errors = temp_errors;
                  error_array->error_count = 1;
                  U64_DEBUG ("Parsed single error: %s", temp_error);
                  return U64_OK;
                }
              FreeMem (temp_errors, sizeof (STRPTR));
            }
        }
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Parsing errors array starting at position %lu",
             (unsigned long)parser.position);
  parser.position++; /* Skip opening bracket */

  /* Allocate initial array */
  temp_errors
      = AllocMem (sizeof (STRPTR) * temp_capacity, MEMF_PUBLIC | MEMF_CLEAR);
  if (!temp_errors)
    {
      return U64_ERR_MEMORY;
    }

  /* Parse array elements */
  while (parser.position < parser.length)
    {
      char c = parser.json[parser.position];

      /* Handle string state */
      if (escaped)
        {
          escaped = FALSE;
        }
      else if (c == '\\')
        {
          escaped = TRUE;
        }
      else if (c == '"')
        {
          if (!in_string)
            {
              /* Start of string */
              in_string = TRUE;
              string_start = parser.position + 1;
              string_pos = 0;
            }
          else
            {
              /* End of string - extract it */
              ULONG string_len = parser.position - string_start;

              if (string_len > 0 && string_len < sizeof (temp_error) - 1)
                {
                  /* Copy string with proper bounds checking */
                  CopyMem ((APTR)&parser.json[string_start], temp_error,
                           string_len);
                  temp_error[string_len] = '\0';

                  U64_DEBUG ("Found error string: '%s'", temp_error);

                  /* Expand array if needed */
                  if (temp_count >= temp_capacity)
                    {
                      STRPTR *new_errors;
                      ULONG new_capacity = temp_capacity * 2;

                      new_errors = AllocMem (sizeof (STRPTR) * new_capacity,
                                             MEMF_PUBLIC | MEMF_CLEAR);
                      if (!new_errors)
                        {
                          /* Cleanup and return error */
                          for (ULONG i = 0; i < temp_count; i++)
                            {
                              if (temp_errors[i])
                                {
                                  FreeMem (temp_errors[i],
                                           strlen (temp_errors[i]) + 1);
                                }
                            }
                          FreeMem (temp_errors,
                                   sizeof (STRPTR) * temp_capacity);
                          return U64_ERR_MEMORY;
                        }

                      /* Copy existing errors */
                      CopyMem (temp_errors, new_errors,
                               sizeof (STRPTR) * temp_count);
                      FreeMem (temp_errors, sizeof (STRPTR) * temp_capacity);
                      temp_errors = new_errors;
                      temp_capacity = new_capacity;
                    }

                  /* Store error string */
                  temp_errors[temp_count]
                      = AllocMem (strlen (temp_error) + 1, MEMF_PUBLIC);
                  if (temp_errors[temp_count])
                    {
                      strcpy (temp_errors[temp_count], temp_error);
                      temp_count++;
                    }
                }

              in_string = FALSE;
            }
        }
      else if (!in_string)
        {
          if (c == ']')
            {
              /* End of array */
              U64_DEBUG ("End of errors array found, parsed %lu errors",
                         (unsigned long)temp_count);
              break;
            }
        }

      parser.position++;
    }

  /* Store results */
  error_array->errors = temp_errors;
  error_array->error_count = temp_count;

  U64_DEBUG ("Successfully parsed %lu errors from JSON",
             (unsigned long)temp_count);
  return U64_OK;
}

/* Get formatted error string from error array */
STRPTR
U64_FormatErrorArray (U64ErrorArray *error_array)
{
  ULONG total_len = 0;
  ULONG i;
  STRPTR result;
  STRPTR ptr;

  if (!error_array || error_array->error_count == 0)
    {
      result = AllocMem (32, MEMF_PUBLIC);
      if (result)
        {
          strcpy (result, "No specific error information");
        }
      return result;
    }

  /* Calculate total length needed */
  for (i = 0; i < error_array->error_count; i++)
    {
      if (error_array->errors[i])
        {
          total_len += strlen (error_array->errors[i])
                       + 4; /* +4 for "; " separator */
        }
    }
  total_len += 1; /* null terminator */

  /* Allocate result buffer */
  result = AllocMem (total_len, MEMF_PUBLIC | MEMF_CLEAR);
  if (!result)
    {
      return NULL;
    }

  /* Build formatted string */
  ptr = result;
  for (i = 0; i < error_array->error_count; i++)
    {
      if (error_array->errors[i])
        {
          if (i > 0)
            {
              strcpy (ptr, "; ");
              ptr += 2;
            }
          strcpy (ptr, error_array->errors[i]);
          ptr += strlen (error_array->errors[i]);
        }
    }

  return result;
}

/* Validate SID file format */
LONG
U64_ValidateSIDFile (CONST UBYTE *data, ULONG size, STRPTR *validation_info)
{
  char *info;
  UWORD version, data_offset, load_address, init_address, play_address, songs,
      start_song;

  if (!data || size < 0x7E)
    {
      if (validation_info)
        {
          info = AllocMem (64, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info, "File too small: %lu bytes (minimum 126)",
                       (unsigned long)size);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Check magic */
  if (!(data[0] == 'P' && data[1] == 'S' && data[2] == 'I' && data[3] == 'D')
      && !(data[0] == 'R' && data[1] == 'S' && data[2] == 'I'
           && data[3] == 'D'))
    {
      if (validation_info)
        {
          info = AllocMem (128, MEMF_PUBLIC);
          if (info)
            {
              sprintf (info,
                       "Invalid SID header: %02X %02X %02X %02X (expected "
                       "PSID or RSID)",
                       data[0], data[1], data[2], data[3]);
              *validation_info = info;
            }
        }
      return U64_ERR_INVALID;
    }

  /* Extract header info */
  version = (data[4] << 8) | data[5];
  data_offset = (data[6] << 8) | data[7];
  load_address = (data[8] << 8) | data[9];
  init_address = (data[10] << 8) | data[11];
  play_address = (data[12] << 8) | data[13];
  songs = (data[14] << 8) | data[15];
  start_song = (data[16] << 8) | data[17];

  if (validation_info)
    {
      info = AllocMem (512, MEMF_PUBLIC);
      if (info)
        {
          sprintf (info,
                   "SID v%d, %d songs (start:%d), load:$%04X, init:$%04X, "
                   "play:$%04X, data_offset:%d",
                   version, songs, start_song, load_address, init_address,
                   play_address, data_offset);
          *validation_info = info;
        }
    }

  return U64_OK;
}

/* Free device info */
void
U64_FreeDeviceInfo (U64DeviceInfo *info)
{
  if (!info)
    {
      return;
    }

  if (info->product_name)
    {
      FreeMem (info->product_name, strlen ((char *)info->product_name) + 1);
      info->product_name = NULL;
    }

  if (info->firmware_version)
    {
      FreeMem (info->firmware_version,
               strlen ((char *)info->firmware_version) + 1);
      info->firmware_version = NULL;
    }

  if (info->fpga_version)
    {
      FreeMem (info->fpga_version, strlen ((char *)info->fpga_version) + 1);
      info->fpga_version = NULL;
    }

  if (info->core_version)
    {
      FreeMem (info->core_version, strlen ((char *)info->core_version) + 1);
      info->core_version = NULL;
    }

  if (info->hostname)
    {
      FreeMem (info->hostname, strlen ((char *)info->hostname) + 1);
      info->hostname = NULL;
    }

  if (info->unique_id)
    {
      FreeMem (info->unique_id, strlen ((char *)info->unique_id) + 1);
      info->unique_id = NULL;
    }
}

/* Parse device info from JSON */
LONG
U64_ParseDeviceInfo (CONST_STRPTR json, U64DeviceInfo *info)
{
  JsonParser parser;
  char buffer[256];

  if (!json || !info)
    {
      return U64_ERR_INVALID;
    }

  U64_DEBUG ("Parsing device info JSON: %.200s", json);

  /* Clear info structure */
  memset (info, 0, sizeof (U64DeviceInfo));

  /* Initialize parser */
  if (!U64_JsonInit (&parser, json))
    {
      U64_DEBUG ("Failed to initialize JSON parser");
      return U64_ERR_INVALID;
    }

  /* Parse product - DON'T reset parser position */
  if (U64_JsonFindKey (&parser, "product"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found product: '%s'", buffer);
          info->product_name = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->product_name)
            {
              strcpy (info->product_name, buffer);

              /* Determine product type */
              if (strstr (buffer, "Ultimate 64")
                  || strstr (buffer, "Ultimate-64"))
                {
                  info->product = U64_PRODUCT_ULTIMATE64;
                }
              else if (strstr (buffer, "Ultimate-II+"))
                {
                  info->product = U64_PRODUCT_ULTIMATE2PLUS;
                }
              else if (strstr (buffer, "Ultimate-II")
                       || strstr (buffer, "Ultimate II"))
                {
                  info->product = U64_PRODUCT_ULTIMATE2;
                }
              else
                {
                  info->product = U64_PRODUCT_UNKNOWN;
                }
              U64_DEBUG ("Product type determined: %d", info->product);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get product string");
        }
    }
  else
    {
      U64_DEBUG ("Product field not found");
    }

  /* Parse firmware version - RESET parser position for new search */
  parser.position = 0;
  if (U64_JsonFindKey (&parser, "firmware_version"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found firmware_version: '%s'", buffer);
          info->firmware_version = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->firmware_version)
            {
              strcpy (info->firmware_version, buffer);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get firmware_version string");
        }
    }
  else
    {
      U64_DEBUG ("firmware_version field not found");
    }

  /* Parse FPGA version */
  parser.position = 0;
  if (U64_JsonFindKey (&parser, "fpga_version"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found fpga_version: '%s'", buffer);
          info->fpga_version = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->fpga_version)
            {
              strcpy (info->fpga_version, buffer);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get fpga_version string");
        }
    }
  else
    {
      U64_DEBUG ("fpga_version field not found");
    }

  /* Parse core version (Ultimate-64 only) */
  parser.position = 0;
  if (U64_JsonFindKey (&parser, "core_version"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found core_version: '%s'", buffer);
          info->core_version = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->core_version)
            {
              strcpy (info->core_version, buffer);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get core_version string");
        }
    }
  else
    {
      U64_DEBUG ("core_version field not found (normal for Ultimate-II)");
    }

  /* Parse hostname */
  parser.position = 0;
  if (U64_JsonFindKey (&parser, "hostname"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found hostname: '%s'", buffer);
          info->hostname = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->hostname)
            {
              strcpy (info->hostname, buffer);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get hostname string");
        }
    }
  else
    {
      U64_DEBUG ("hostname field not found");
    }

  /* Parse unique ID */
  parser.position = 0;
  if (U64_JsonFindKey (&parser, "unique_id"))
    {
      if (U64_JsonGetString (&parser, buffer, sizeof (buffer)))
        {
          U64_DEBUG ("Found unique_id: '%s'", buffer);
          info->unique_id = AllocMem (strlen (buffer) + 1, MEMF_PUBLIC);
          if (info->unique_id)
            {
              strcpy (info->unique_id, buffer);
            }
        }
      else
        {
          U64_DEBUG ("Failed to get unique_id string");
        }
    }
  else
    {
      U64_DEBUG ("unique_id field not found (may be disabled)");
    }

  U64_DEBUG ("Device info parsing completed successfully");
  return U64_OK;
}
