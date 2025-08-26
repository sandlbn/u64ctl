/* Ultimate64/Ultimate-II Control Library for Amiga OS 3.x
 * Configuration management implementation
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ultimate64_amiga.h"
#include "ultimate64_private.h"

/* Internal configuration category structure */
typedef struct U64ConfigCategory
{
    STRPTR name;            /* Category name */
    U64ConfigItem *items;   /* Array of configuration items */
    ULONG item_count;       /* Number of items in category */
} U64ConfigCategory;

/* Internal configuration structure */
typedef struct U64Config
{
    U64ConfigCategory *categories;  /* Array of categories */
    ULONG category_count;          /* Number of categories */
} U64Config;

/* Free configuration value */
static void
U64_FreeConfigValue(U64ConfigValue *value)
{
    if (!value) return;
    
    if (value->current_str)
    {
        FreeMem(value->current_str, strlen(value->current_str) + 1);
        value->current_str = NULL;
    }
    
    if (value->format)
    {
        FreeMem(value->format, strlen(value->format) + 1);
        value->format = NULL;
    }
    
    if (value->default_str)
    {
        FreeMem(value->default_str, strlen(value->default_str) + 1);
        value->default_str = NULL;
    }
}

/* Free configuration category */
static void
U64_FreeConfigCategory(U64ConfigCategory *category)
{
    ULONG i;
    
    if (!category) return;
    
    if (category->name)
    {
        FreeMem(category->name, strlen(category->name) + 1);
        category->name = NULL;
    }
    
    if (category->items)
    {
        for (i = 0; i < category->item_count; i++)
        {
            U64_FreeConfigItem(&category->items[i]);
        }
        FreeMem(category->items, sizeof(U64ConfigItem) * category->item_count);
        category->items = NULL;
    }
    
    category->item_count = 0;
}

/* Forward declaration */
static void JsonSkipWhitespace(JsonParser *parser);

/* Skip whitespace in JSON */
static void
JsonSkipWhitespace(JsonParser *parser)
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

/* URL encode a string for use in HTTP requests */
static STRPTR
U64_URLEncode(CONST_STRPTR input)
{
    STRPTR output;
    ULONG input_len, output_len;
    ULONG i, j;
    
    if (!input) return NULL;
    
    input_len = strlen(input);
    /* Worst case: every character needs encoding (3 chars each) */
    output_len = input_len * 3 + 1;
    
    output = AllocMem(output_len, MEMF_PUBLIC | MEMF_CLEAR);
    if (!output) return NULL;
    
    for (i = 0, j = 0; i < input_len && j < output_len - 3; i++)
    {
        UBYTE c = input[i];
        
        /* Safe characters that don't need encoding */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
        {
            output[j++] = c;
        }
        else
        {
            /* Encode as %XX */
            char hex_chars[] = "0123456789ABCDEF";
            output[j++] = '%';
            output[j++] = hex_chars[(c >> 4) & 0x0F];
            output[j++] = hex_chars[c & 0x0F];
        }
    }
    
    output[j] = '\0';
    return output;
}

/* Parse configuration categories from JSON */
static LONG
U64_ParseConfigCategories(CONST_STRPTR json, STRPTR **categories, ULONG *count)
{
    JsonParser parser;
    STRPTR *temp_categories = NULL;
    ULONG temp_count = 0;
    ULONG temp_capacity = 16;
    BOOL in_array = FALSE;
    BOOL in_string = FALSE;
    BOOL escaped = FALSE;
    ULONG string_start = 0;
    char temp_string[256];
    
    if (!json || !categories || !count)
    {
        return U64_ERR_INVALID;
    }
    
    *categories = NULL;
    *count = 0;
    
    U64_DEBUG("Parsing configuration categories JSON: %.200s", json);
    
    if (!U64_JsonInit(&parser, json))
    {
        return U64_ERR_INVALID;
    }
    
    /* Find the categories array */
    if (!U64_JsonFindKey(&parser, "categories"))
    {
        U64_DEBUG("No 'categories' field found in JSON");
        return U64_ERR_GENERAL;
    }
    
    /* Skip whitespace and find opening bracket */
    JsonSkipWhitespace(&parser);
    if (parser.position >= parser.length || parser.json[parser.position] != '[')
    {
        U64_DEBUG("Categories field is not an array");
        return U64_ERR_GENERAL;
    }
    
    parser.position++; /* Skip opening bracket */
    in_array = TRUE;
    
    /* Allocate initial array */
    temp_categories = AllocMem(sizeof(STRPTR) * temp_capacity, MEMF_PUBLIC | MEMF_CLEAR);
    if (!temp_categories)
    {
        return U64_ERR_MEMORY;
    }
    
    /* Parse array elements */
    while (parser.position < parser.length && in_array)
    {
        char c = parser.json[parser.position];
        
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
            }
            else
            {
                /* End of string */
                ULONG string_len = parser.position - string_start;
                if (string_len > 0 && string_len < sizeof(temp_string) - 1)
                {
                    /* Extract category name */
                    CopyMem((APTR)&parser.json[string_start], temp_string, string_len);
                    temp_string[string_len] = '\0';
                    
                    U64_DEBUG("Found category: '%s'", temp_string);
                    
                    /* Expand array if needed */
                    if (temp_count >= temp_capacity)
                    {
                        STRPTR *new_array;
                        ULONG new_capacity = temp_capacity * 2;
                        
                        new_array = AllocMem(sizeof(STRPTR) * new_capacity, MEMF_PUBLIC | MEMF_CLEAR);
                        if (!new_array)
                        {
                            /* Cleanup and return error */
                            for (ULONG i = 0; i < temp_count; i++)
                            {
                                if (temp_categories[i])
                                {
                                    FreeMem(temp_categories[i], strlen(temp_categories[i]) + 1);
                                }
                            }
                            FreeMem(temp_categories, sizeof(STRPTR) * temp_capacity);
                            return U64_ERR_MEMORY;
                        }
                        
                        /* Copy existing categories */
                        CopyMem(temp_categories, new_array, sizeof(STRPTR) * temp_count);
                        FreeMem(temp_categories, sizeof(STRPTR) * temp_capacity);
                        temp_categories = new_array;
                        temp_capacity = new_capacity;
                    }
                    
                    /* Store category string */
                    temp_categories[temp_count] = AllocMem(string_len + 1, MEMF_PUBLIC);
                    if (temp_categories[temp_count])
                    {
                        strcpy(temp_categories[temp_count], temp_string);
                        temp_count++;
                    }
                }
                in_string = FALSE;
            }
        }
        else if (!in_string && c == ']')
        {
            /* End of array */
            in_array = FALSE;
            break;
        }
        
        parser.position++;
    }
    
    /* Return results */
    *categories = temp_categories;
    *count = temp_count;
    
    U64_DEBUG("Successfully parsed %lu categories", (unsigned long)temp_count);
    return U64_OK;
}

/* Get list of all configuration categories */
LONG
U64_GetConfigCategories(U64Connection *conn, STRPTR **categories, ULONG *count)
{
    HttpRequest req;
    LONG result;
    
    if (!conn || !categories || !count)
    {
        return U64_ERR_INVALID;
    }
    
    *categories = NULL;
    *count = 0;
    
    U64_DEBUG("Getting configuration categories...");
    
    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.path = "/v1/configs";
    
    /* Execute request */
    result = U64_HttpRequest(conn, &req);
    if (result != U64_OK)
    {
        U64_DEBUG("Failed to get config categories: %ld", result);
        conn->last_error = result;
        return result;
    }
    
    if (!req.response)
    {
        U64_DEBUG("No response received");
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    U64_DEBUG("Config categories response: %.500s", req.response);
    
    /* Parse JSON response */
    result = U64_ParseConfigCategories(req.response, categories, count);
    
    /* Free response */
    FreeMem(req.response, req.response_size + 1);
    
    conn->last_error = result;
    return result;
}

/* Free categories array */
void
U64_FreeConfigCategories(STRPTR *categories, ULONG count)
{
    ULONG i;
    
    if (!categories) return;
    
    for (i = 0; i < count; i++)
    {
        if (categories[i])
        {
            FreeMem(categories[i], strlen(categories[i]) + 1);
        }
    }
    
    FreeMem(categories, sizeof(STRPTR) * count);
}

/* Get configuration items in a category */
LONG
U64_GetConfigCategory(U64Connection *conn, CONST_STRPTR category,
                      U64ConfigItem **items, ULONG *item_count)
{
    HttpRequest req;
    LONG result;
    char path[512];
    STRPTR encoded_category;
    JsonParser parser;
    U64ConfigItem *temp_items = NULL;
    ULONG temp_count = 0;
    
    if (!conn || !category || !items || !item_count)
    {
        return U64_ERR_INVALID;
    }
    
    *items = NULL;
    *item_count = 0;
    
    U64_DEBUG("Getting configuration category: %s", category);
    
    /* URL encode category name */
    encoded_category = U64_URLEncode(category);
    if (!encoded_category)
    {
        return U64_ERR_MEMORY;
    }
    
    /* Build request path */
    snprintf(path, sizeof(path), "/v1/configs/%s", encoded_category);
    FreeMem(encoded_category, strlen(encoded_category) + 1);
    
    U64_DEBUG("Config category request path: %s", path);
    
    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.path = path;
    
    /* Execute request */
    result = U64_HttpRequest(conn, &req);
    if (result != U64_OK)
    {
        U64_DEBUG("Failed to get config category: %ld", result);
        conn->last_error = result;
        return result;
    }
    
    if (!req.response)
    {
        U64_DEBUG("No response received");
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    U64_DEBUG("Config category response: %.500s", req.response);
    
    /* Parse JSON response - look for the category object */
    if (!U64_JsonInit(&parser, req.response))
    {
        FreeMem(req.response, req.response_size + 1);
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    /* Find the category in the response */
    if (!U64_JsonFindKey(&parser, category))
    {
        U64_DEBUG("Category '%s' not found in response", category);
        FreeMem(req.response, req.response_size + 1);
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    /* Parse the category object manually since it contains key-value pairs */
    /* This is a simplified parser that extracts string values */
    ULONG max_items = 32;
    temp_items = AllocMem(sizeof(U64ConfigItem) * max_items, MEMF_PUBLIC | MEMF_CLEAR);
    if (!temp_items)
    {
        FreeMem(req.response, req.response_size + 1);
        return U64_ERR_MEMORY;
    }
    
    /* Simple parsing: look for "key": "value" pairs in the category object */
    char *response_copy = req.response;
    char *category_start = strstr(response_copy, category);
    if (category_start)
    {
        char *obj_start = strchr(category_start, '{');
        if (obj_start)
        {
            char *current = obj_start + 1;
            BOOL in_string = FALSE;
            BOOL escaped = FALSE;
            BOOL parsing_key = TRUE;
            char key_buffer[256] = {0};
            char value_buffer[256] = {0};
            ULONG key_pos = 0, value_pos = 0;
            
            while (*current && *current != '}' && temp_count < max_items)
            {
                if (escaped)
                {
                    if (parsing_key && key_pos < sizeof(key_buffer) - 1)
                    {
                        key_buffer[key_pos++] = *current;
                    }
                    else if (!parsing_key && value_pos < sizeof(value_buffer) - 1)
                    {
                        value_buffer[value_pos++] = *current;
                    }
                    escaped = FALSE;
                }
                else if (*current == '\\')
                {
                    escaped = TRUE;
                }
                else if (*current == '"')
                {
                    if (!in_string)
                    {
                        /* Start of string */
                        in_string = TRUE;
                        if (parsing_key)
                        {
                            key_pos = 0;
                            memset(key_buffer, 0, sizeof(key_buffer));
                        }
                        else
                        {
                            value_pos = 0;
                            memset(value_buffer, 0, sizeof(value_buffer));
                        }
                    }
                    else
                    {
                        /* End of string */
                        in_string = FALSE;
                        if (parsing_key)
                        {
                            key_buffer[key_pos] = '\0';
                        }
                        else
                        {
                            value_buffer[value_pos] = '\0';
                            
                            /* Store this key-value pair */
                            if (strlen(key_buffer) > 0 && temp_count < max_items)
                            {
                                U64ConfigItem *item = &temp_items[temp_count];
                                
                                item->name = AllocMem(strlen(key_buffer) + 1, MEMF_PUBLIC);
                                if (item->name)
                                {
                                    strcpy(item->name, key_buffer);
                                }
                                
                                /* Try to parse as number first */
                                char *endptr;
                                LONG num_value = strtol(value_buffer, &endptr, 10);
                                if (*endptr == '\0' && endptr != value_buffer)
                                {
                                    /* It's a number */
                                    item->value.is_numeric = TRUE;
                                    item->value.current_int = num_value;
                                }
                                else
                                {
                                    /* It's a string */
                                    item->value.is_numeric = FALSE;
                                    item->value.current_str = AllocMem(strlen(value_buffer) + 1, MEMF_PUBLIC);
                                    if (item->value.current_str)
                                    {
                                        strcpy(item->value.current_str, value_buffer);
                                    }
                                }
                                
                                temp_count++;
                                U64_DEBUG("Parsed config item: '%s' = '%s'", key_buffer, value_buffer);
                            }
                        }
                    }
                }
                else if (in_string)
                {
                    /* Character inside string */
                    if (parsing_key && key_pos < sizeof(key_buffer) - 1)
                    {
                        key_buffer[key_pos++] = *current;
                    }
                    else if (!parsing_key && value_pos < sizeof(value_buffer) - 1)
                    {
                        value_buffer[value_pos++] = *current;
                    }
                }
                else if (*current == ':')
                {
                    /* Switch from parsing key to parsing value */
                    parsing_key = FALSE;
                }
                else if (*current == ',')
                {
                    /* Next key-value pair */
                    parsing_key = TRUE;
                }
                
                current++;
            }
        }
    }
    
    /* Free response */
    FreeMem(req.response, req.response_size + 1);
    
    /* Return results */
    *items = temp_items;
    *item_count = temp_count;
    
    U64_DEBUG("Successfully parsed %lu configuration items", (unsigned long)temp_count);
    conn->last_error = U64_OK;
    return U64_OK;
}

/* Free configuration items */
void
U64_FreeConfigItems(U64ConfigItem *items, ULONG count)
{
    ULONG i;
    
    if (!items) return;
    
    for (i = 0; i < count; i++)
    {
        U64_FreeConfigItem(&items[i]);
    }
    
    FreeMem(items, sizeof(U64ConfigItem) * count);
}

/* Get detailed information about a configuration item */
LONG
U64_GetConfigItem(U64Connection *conn, CONST_STRPTR category, CONST_STRPTR item,
                  U64ConfigItem *config_item)
{
    HttpRequest req;
    LONG result;
    char path[512];
    STRPTR encoded_category, encoded_item;
    JsonParser parser;
    char buffer[256];
    LONG value;
    
    if (!conn || !category || !item || !config_item)
    {
        return U64_ERR_INVALID;
    }
    
    memset(config_item, 0, sizeof(U64ConfigItem));
    
    U64_DEBUG("Getting configuration item: %s / %s", category, item);
    
    /* URL encode category and item names */
    encoded_category = U64_URLEncode(category);
    encoded_item = U64_URLEncode(item);
    
    if (!encoded_category || !encoded_item)
    {
        if (encoded_category) FreeMem(encoded_category, strlen(encoded_category) + 1);
        if (encoded_item) FreeMem(encoded_item, strlen(encoded_item) + 1);
        return U64_ERR_MEMORY;
    }
    
    /* Build request path */
    snprintf(path, sizeof(path), "/v1/configs/%s/%s", encoded_category, encoded_item);
    
    FreeMem(encoded_category, strlen(encoded_category) + 1);
    FreeMem(encoded_item, strlen(encoded_item) + 1);
    
    U64_DEBUG("Config item request path: %s", path);
    
    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.path = path;
    
    /* Execute request */
    result = U64_HttpRequest(conn, &req);
    if (result != U64_OK)
    {
        U64_DEBUG("Failed to get config item: %ld", result);
        conn->last_error = result;
        return result;
    }
    
    if (!req.response)
    {
        U64_DEBUG("No response received");
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    U64_DEBUG("Config item response: %.500s", req.response);
    
    /* Parse JSON response */
    if (!U64_JsonInit(&parser, req.response))
    {
        FreeMem(req.response, req.response_size + 1);
        conn->last_error = U64_ERR_GENERAL;
        return U64_ERR_GENERAL;
    }
    
    /* Store item name */
    config_item->name = AllocMem(strlen(item) + 1, MEMF_PUBLIC);
    if (config_item->name)
    {
        strcpy(config_item->name, item);
    }
    
    /* Parse the detailed item information */
    parser.position = 0;
    if (U64_JsonFindKey(&parser, "current"))
    {
        /* Try to get as number first */
        ULONG old_pos = parser.position;
        if (U64_JsonGetNumber(&parser, &value))
        {
            config_item->value.is_numeric = TRUE;
            config_item->value.current_int = value;
            U64_DEBUG("Current value (numeric): %ld", value);
        }
        else
        {
            /* Try as string */
            parser.position = old_pos;
            if (U64_JsonGetString(&parser, buffer, sizeof(buffer)))
            {
                config_item->value.is_numeric = FALSE;
                config_item->value.current_str = AllocMem(strlen(buffer) + 1, MEMF_PUBLIC);
                if (config_item->value.current_str)
                {
                    strcpy(config_item->value.current_str, buffer);
                }
                U64_DEBUG("Current value (string): '%s'", buffer);
            }
        }
    }
    
    /* Parse min value (for numeric types) */
    parser.position = 0;
    if (U64_JsonFindKey(&parser, "min"))
    {
        if (U64_JsonGetNumber(&parser, &value))
        {
            config_item->value.min_value = value;
            U64_DEBUG("Min value: %ld", value);
        }
    }
    
    /* Parse max value (for numeric types) */
    parser.position = 0;
    if (U64_JsonFindKey(&parser, "max"))
    {
        if (U64_JsonGetNumber(&parser, &value))
        {
            config_item->value.max_value = value;
            U64_DEBUG("Max value: %ld", value);
        }
    }
    
    /* Parse format */
    parser.position = 0;
    if (U64_JsonFindKey(&parser, "format"))
    {
        if (U64_JsonGetString(&parser, buffer, sizeof(buffer)))
        {
            config_item->value.format = AllocMem(strlen(buffer) + 1, MEMF_PUBLIC);
            if (config_item->value.format)
            {
                strcpy(config_item->value.format, buffer);
            }
            U64_DEBUG("Format: '%s'", buffer);
        }
    }
    
    /* Parse default value */
    parser.position = 0;
    if (U64_JsonFindKey(&parser, "default"))
    {
        ULONG old_pos = parser.position;
        if (U64_JsonGetNumber(&parser, &value))
        {
            config_item->value.default_int = value;
            U64_DEBUG("Default value (numeric): %ld", value);
        }
        else
        {
            parser.position = old_pos;
            if (U64_JsonGetString(&parser, buffer, sizeof(buffer)))
            {
                config_item->value.default_str = AllocMem(strlen(buffer) + 1, MEMF_PUBLIC);
                if (config_item->value.default_str)
                {
                    strcpy(config_item->value.default_str, buffer);
                }
                U64_DEBUG("Default value (string): '%s'", buffer);
            }
        }
    }
    
    /* Free response */
    FreeMem(req.response, req.response_size + 1);
    
    conn->last_error = U64_OK;
    return U64_OK;
}

/* Set a configuration item value */
LONG
U64_SetConfigItem(U64Connection *conn, CONST_STRPTR category, CONST_STRPTR item,
                  CONST_STRPTR value)
{
    HttpRequest req;
    LONG result;
    char path[1024];
    STRPTR encoded_category, encoded_item, encoded_value;
    
    if (!conn || !category || !item || !value)
    {
        return U64_ERR_INVALID;
    }
    
    U64_DEBUG("Setting configuration item: %s / %s = %s", category, item, value);
    
    /* URL encode all components */
    encoded_category = U64_URLEncode(category);
    encoded_item = U64_URLEncode(item);
    encoded_value = U64_URLEncode(value);
    
    if (!encoded_category || !encoded_item || !encoded_value)
    {
        if (encoded_category) FreeMem(encoded_category, strlen(encoded_category) + 1);
        if (encoded_item) FreeMem(encoded_item, strlen(encoded_item) + 1);
        if (encoded_value) FreeMem(encoded_value, strlen(encoded_value) + 1);
        return U64_ERR_MEMORY;
    }
    
    /* Build request path with value parameter */
    snprintf(path, sizeof(path), "/v1/configs/%s/%s?value=%s", 
             encoded_category, encoded_item, encoded_value);
    
    FreeMem(encoded_category, strlen(encoded_category) + 1);
    FreeMem(encoded_item, strlen(encoded_item) + 1);
    FreeMem(encoded_value, strlen(encoded_value) + 1);
    
    U64_DEBUG("Config set request path: %s", path);
    
    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_PUT;
    req.path = path;
    
    /* Execute request */
    result = U64_HttpRequest(conn, &req);
    
    U64_DEBUG("Config set result: %ld", result);
    U64_DEBUG("HTTP status: %d", req.status_code);
    
    if (req.response)
    {
        U64_DEBUG("Config set response: %.200s", req.response);
        FreeMem(req.response, req.response_size + 1);
    }
    
    /* Check for success */
    if (result == U64_OK)
    {
        if (req.status_code >= 200 && req.status_code < 300)
        {
            conn->last_error = U64_OK;
            return U64_OK;
        }
        else
        {
            conn->last_error = U64_ERR_GENERAL;
            return U64_ERR_GENERAL;
        }
    }
    
    conn->last_error = result;
    return result;
}

/* Set multiple configuration items at once using JSON */
LONG
U64_SetConfigItems(U64Connection *conn, CONST_STRPTR json_config)
{
    HttpRequest req;
    LONG result;
    
    if (!conn || !json_config)
    {
        return U64_ERR_INVALID;
    }
    
    U64_DEBUG("Setting multiple configuration items with JSON: %.200s", json_config);
    
    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_POST;
    req.path = "/v1/configs";
    req.content_type = "application/json";
    req.body = (UBYTE *)json_config;
    req.body_size = strlen(json_config);
    
    /* Execute request */
    result = U64_HttpRequest(conn, &req);
    
    U64_DEBUG("Config bulk set result: %ld", result);
    U64_DEBUG("HTTP status: %d", req.status_code);
    
    if (req.response)
    {
        U64_DEBUG("Config bulk set response: %.200s", req.response);
        FreeMem(req.response, req.response_size + 1);
    }
    
    /* Check for success */
    if (result == U64_OK)
    {
        if (req.status_code >= 200 && req.status_code < 300)
        {
            conn->last_error = U64_OK;
            return U64_OK;
        }
        else
        {
            conn->last_error = U64_ERR_GENERAL;
            return U64_ERR_GENERAL;
        }
    }
    
    conn->last_error = result;
    return result;
}

/* Load configuration from flash memory */
LONG
U64_LoadConfigFromFlash(U64Connection *conn)
{
    HttpRequest req;
    LONG result;

    if (!conn)
    {
        return U64_ERR_INVALID;
    }

    U64_DEBUG("Loading configuration from flash...");

    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_PUT;
    req.path = "/v1/configs:load_from_flash";

    /* Execute request */
    result = U64_HttpRequest(conn, &req);

    U64_DEBUG("Config load result: %ld", result);
    U64_DEBUG("HTTP status: %d", req.status_code);

    if (req.response)
    {
        U64_DEBUG("Config load response: %.200s", req.response);
        FreeMem(req.response, req.response_size + 1);
    }

    /* Check for success */
    if (result == U64_OK)
    {
        if (req.status_code >= 200 && req.status_code < 300)
        {
            conn->last_error = U64_OK;
            return U64_OK;
        }
        else
        {
            conn->last_error = U64_ERR_GENERAL;
            return U64_ERR_GENERAL;
        }
    }

    conn->last_error = result;
    return result;
}

/* Save configuration to flash memory */
LONG
U64_SaveConfigToFlash(U64Connection *conn)
{
    HttpRequest req;
    LONG result;

    if (!conn)
    {
        return U64_ERR_INVALID;
    }

    U64_DEBUG("Saving configuration to flash...");

    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_PUT;
    req.path = "/v1/configs:save_to_flash";

    /* Execute request */
    result = U64_HttpRequest(conn, &req);

    U64_DEBUG("Config save result: %ld", result);
    U64_DEBUG("HTTP status: %d", req.status_code);

    if (req.response)
    {
        U64_DEBUG("Config save response: %.200s", req.response);
        FreeMem(req.response, req.response_size + 1);
    }

    /* Check for success */
    if (result == U64_OK)
    {
        if (req.status_code >= 200 && req.status_code < 300)
        {
            conn->last_error = U64_OK;
            return U64_OK;
        }
        else
        {
            conn->last_error = U64_ERR_GENERAL;
            return U64_ERR_GENERAL;
        }
    }

    conn->last_error = result;
    return result;
}

/* Reset configuration to factory defaults */
LONG
U64_ResetConfigToDefault(U64Connection *conn)
{
    HttpRequest req;
    LONG result;

    if (!conn)
    {
        return U64_ERR_INVALID;
    }

    U64_DEBUG("Resetting configuration to factory defaults...");

    /* Setup HTTP request */
    memset(&req, 0, sizeof(req));
    req.method = HTTP_PUT;
    req.path = "/v1/configs:reset_to_default";

    /* Execute request */
    result = U64_HttpRequest(conn, &req);

    U64_DEBUG("Config reset result: %ld", result);
    U64_DEBUG("HTTP status: %d", req.status_code);

    if (req.response)
    {
        U64_DEBUG("Config reset response: %.200s", req.response);
        FreeMem(req.response, req.response_size + 1);
    }

    /* Check for success */
    if (result == U64_OK)
    {
        if (req.status_code >= 200 && req.status_code < 300)
        {
            conn->last_error = U64_OK;
            return U64_OK;
        }
        else
        {
            conn->last_error = U64_ERR_GENERAL;
            return U64_ERR_GENERAL;
        }
    }

    conn->last_error = result;
    return result;
}

/* Free a single configuration item */
void
U64_FreeConfigItem(U64ConfigItem *item)
{
    if (!item) return;
    
    if (item->name)
    {
        FreeMem(item->name, strlen(item->name) + 1);
        item->name = NULL;
    }
    
    U64_FreeConfigValue(&item->value);
    memset(item, 0, sizeof(U64ConfigItem));
}

/* Build JSON string for setting multiple config items */
STRPTR
U64_BuildConfigJSON(CONST_STRPTR *categories, CONST_STRPTR *items,
                   CONST_STRPTR *values, ULONG count)
{
    STRPTR json;
    ULONG json_size = 1024; /* Initial size */
    ULONG i;
    char *current_category = NULL;
    BOOL first_category = TRUE;
    BOOL first_item = TRUE;
    
    if (!categories || !items || !values || count == 0)
    {
        return NULL;
    }
    
    /* Calculate approximate size needed */
    for (i = 0; i < count; i++)
    {
        json_size += strlen(categories[i]) + strlen(items[i]) + strlen(values[i]) + 32;
    }
    
    json = AllocMem(json_size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!json)
    {
        return NULL;
    }
    
    strcpy(json, "{\n");
    
    for (i = 0; i < count; i++)
    {
        /* Check if we're starting a new category */
        if (!current_category || strcmp(current_category, categories[i]) != 0)
        {
            /* Close previous category if needed */
            if (current_category)
            {
                strcat(json, "\n  }");
                first_category = FALSE;
            }
            
            /* Start new category */
            if (!first_category)
            {
                strcat(json, ",\n");
            }
            
            strcat(json, "  \"");
            strcat(json, categories[i]);
            strcat(json, "\": {\n");
            
            current_category = (char *)categories[i];
            first_item = TRUE;
        }
        
        /* Add item */
        if (!first_item)
        {
            strcat(json, ",\n");
        }
        
        strcat(json, "    \"");
        strcat(json, items[i]);
        strcat(json, "\": \"");
        strcat(json, values[i]);
        strcat(json, "\"");
        
        first_item = FALSE;
    }
    
    /* Close last category and JSON object */
    if (current_category)
    {
        strcat(json, "\n  }\n");
    }
    strcat(json, "}");
    
    return json;
}

/* Free JSON string created by U64_BuildConfigJSON */
void
U64_FreeConfigJSON(STRPTR json)
{
    if (json)
    {
        FreeMem(json, strlen(json) + 1);
    }
}