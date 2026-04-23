/* Assembly64 tab for u64mui — UI construction + event handling.
 *
 * Everything Assembly64-specific lives in this file and assembly64.c. The
 * only surface shared with main.c is CreateAssemblyTab() + AsmDispatch(),
 * both declared near the bottom of mui_app.h.
 */

#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <libraries/mui.h>

#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <clib/alib_protos.h>
#include "SDI_hook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mui_app.h"
#include "assembly64.h"     /* ASM_LOG lives here */
#include "file_utils.h"     /* U64_ReadFile */

/* ------------------------------------------------------------------ */
/* Module-private state                                                */
/* ------------------------------------------------------------------ */

#define ASM_PAGE_SIZE 50

struct AsmState {
    char     query[256];
    ULONG    offset;
    ULONG    page_size;

    AsmItem *results;          /* linked list from last Asm_Search */
    ULONG    result_count;
    AsmItem *selected_item;    /* points into results; NULL if none */

    AsmFile *files;            /* linked list from Asm_ListFiles */
    ULONG    file_count;
    AsmFile *selected_file;
};

static struct AsmState g_asm;          /* one tab, one state */

/* Category dropdown. Parallel arrays: label shown to the user, aqlKey
 * appended to the AQL query when picked (NULL = no filter). The set
 * mirrors Assembly64's own "category" preset — matches the mapping at
 * http://hackerswithstyle.se/leet/search/categories. */
static const char *const asm_category_labels[] = {
    "Any", "Demos", "Games", "Intros", "Music", "Tools", "Graphics",
    "Mags", "Misc", "Charts", "BBS", "C128", "Easyflash", "Reu", NULL
};
static const char *const asm_category_keys[] = {
    NULL, "demos", "games", "intros", "music", "tools", "graphics",
    "mags", "misc", "charts", "bbs", "c128", "easyflash", "reu"
};

static const char *const asm_latest_labels[] = {
    "Any time", "Day", "2 days", "4 days", "Week", "2 weeks", "3 weeks",
    "Month", "2 months", "3 months", "6 months", "Year", "2 years", NULL
};
static const char *const asm_latest_keys[] = {
    NULL, "1days", "2days", "4days", "1week", "2weeks", "3weeks",
    "1month", "2months", "3months", "6months", "1year", "2years"
};

/* Source / repository picker. Matches Assembly64's "repo" preset list —
 * narrowing by repo restricts results to a single archive (CSDB, HVSC,
 * OneLoad64, …). "Any" = no filter. */
static const char *const asm_source_labels[] = {
    "Any source",  "CSDB",       "HVSC",    "c64.com",     "OneLoad64",
    "Gamebase64",  "c64.org",    "C64Tapes.org",
    "SEUCK",       "Mayhem CRT", "Preservers", "Guybrush",
    "Ultimate Tapes", NULL
};
static const char *const asm_source_keys[] = {
    NULL,          "csdb",       "hvsc",    "c64com",      "oneload",
    "gamebase",    "c64orgintro","tapes",
    "seuck",       "mayhem",     "pres",       "guybrush",
    "utape"
};

/* Quality / rank filter. Ratings run 0-10 on Assembly64 (0 = unrated).
 * The final "Top rated first" option doesn't filter, it re-orders the
 * server's results by rating descending — surface-best first, top of
 * the scene's taste. */
static const char *const asm_rank_labels[] = {
    "Any rating",   "Good (>=7)",   "Great (>=8)",   "Excellent (>=9)",
    "Perfect 10",   "Top rated first", NULL
};
/* Parallel AQL fragment per entry. Empty string = skip. "Top rated first"
 * combines the sort with a >=5 floor so the list isn't polluted by the
 * thousands of unrated entries the server would otherwise drag in. */
static const char *const asm_rank_aql[] = {
    "",
    "rating:>=7",
    "rating:>=8",
    "rating:>=9",
    "rating:>=10",
    "sort:rating order:desc rating:>=5"
};

/* Drive picker. Maps to Ultimate64's drive_id string "a" / "b".
 * Labels include the default bus IDs (8 and 9) — the actual values can
 * be verified from U64_GetDriveList if needed. */
static const char *const asm_drive_labels[] = { "Drive A (8)", "Drive B (9)", NULL };
static const char *const asm_drive_keys[]   = { "a",           "b"           };

static void asm_status(struct AppData *data, CONST_STRPTR msg);

/* ------------------------------------------------------------------ */
/* MUI display hooks                                                   */
/* ------------------------------------------------------------------ */

/* Called by MUI for every row on every redraw — for the data row it
 * receives our AsmItem pointer, for the title row it receives NULL and
 * we return the column headers (with \033b bold). Year is intentionally
 * absent: Assembly64 has it as 0/null for the majority of entries, so
 * the column was mostly visual noise. */
HOOKPROTONH(ResultsDisplayFunc, VOID, char **array, AsmItem *item)
{
    if (item) {
        *array++ = item->name[0]      ? item->name      : (STRPTR)"?";
        *array++ = item->group[0]     ? item->group     : (STRPTR)"";
        *array++ = item->updated[0]   ? item->updated   : (STRPTR)"";
        *array   = item->cat_str[0]   ? item->cat_str   : (STRPTR)"";
    } else {
        *array++ = (STRPTR)"\033b" "Name";
        *array++ = (STRPTR)"\033b" "Group";
        *array++ = (STRPTR)"\033b" "Added";
        *array   = (STRPTR)"\033b" "Source / Category";
    }
}
MakeStaticHook(ResultsDisplayHook, ResultsDisplayFunc);

HOOKPROTONH(FilesDisplayFunc, VOID, char **array, AsmFile *f)
{
    if (f) {
        *array++ = f->path;
        *array   = f->size_str;
    } else {
        *array++ = (STRPTR)"\033b" "File";
        *array   = (STRPTR)"\033b" "Size";
    }
}
MakeStaticHook(FilesDisplayHook, FilesDisplayFunc);
static void asm_refresh_results_list(struct AppData *data);
static void asm_refresh_files_list(struct AppData *data);
static AsmItem *asm_item_at(LONG idx);
static AsmFile *asm_file_at(LONG idx);
static BOOL ext_matches(CONST_STRPTR path, CONST_STRPTR ext);
static BOOL ext_matches_any(CONST_STRPTR path, const char *const *exts);

/* ------------------------------------------------------------------ */
/* Tab construction                                                    */
/* ------------------------------------------------------------------ */

Object *
CreateAssemblyTab (struct AppData *data)
{
    g_asm.page_size = ASM_PAGE_SIZE;

    /* Query row: free text + category + latest + source pickers. The
     * handler composes AQL from all of them — users never have to write
     * the field prefixes themselves. */
    data->cyc_asm_category = CycleObject,
        MUIA_Cycle_Entries, asm_category_labels,
        MUIA_Cycle_Active, 0,
        End;
    data->cyc_asm_latest = CycleObject,
        MUIA_Cycle_Entries, asm_latest_labels,
        MUIA_Cycle_Active, 0,
        End;
    data->cyc_asm_source = CycleObject,
        MUIA_Cycle_Entries, asm_source_labels,
        MUIA_Cycle_Active, 0,
        End;
    data->cyc_asm_rank = CycleObject,
        MUIA_Cycle_Entries, asm_rank_labels,
        MUIA_Cycle_Active, 0,
        End;

    Object *row_query = HGroup,
        Child, Label("Find:"),
        Child, data->str_asm_query = StringObject,
            MUIA_String_MaxLen, sizeof(g_asm.query) - 1,
            End,
        Child, data->cyc_asm_category,
        Child, data->cyc_asm_latest,
        Child, data->cyc_asm_source,
        Child, data->cyc_asm_rank,
        Child, data->btn_asm_search  = SimpleButton("Search"),
        End;

    /* Multi-column lists using a custom display hook that plucks fields
     * straight out of AsmItem/AsmFile. Same idiom used by TuneFinderMUI
     * for its tune list. No ConstructHook because MUI stores our pointer
     * verbatim — we own the lifetime of the linked lists and call
     * MUIM_List_Clear before freeing them. */
    data->lst_asm_results = MUI_NewObject(MUIC_Listview,
        MUIA_Listview_List, MUI_NewObject(MUIC_List,
            MUIA_Frame, MUIV_Frame_InputList,
            MUIA_List_Format,
              (CONST_STRPTR)"BAR,BAR,BAR,",   /* 4 columns: name | group | added | category */
            MUIA_List_Title, TRUE,
            MUIA_List_DisplayHook, &ResultsDisplayHook,
            TAG_DONE),
        MUIA_Listview_Input, TRUE,
        MUIA_Listview_DoubleClick, TRUE,
        MUIA_CycleChain, TRUE,
        TAG_DONE);

    data->lst_asm_files = MUI_NewObject(MUIC_Listview,
        MUIA_Listview_List, MUI_NewObject(MUIC_List,
            MUIA_Frame, MUIV_Frame_InputList,
            MUIA_List_Format, (CONST_STRPTR)"BAR,",
            MUIA_List_Title, TRUE,
            MUIA_List_DisplayHook, &FilesDisplayHook,
            TAG_DONE),
        MUIA_Listview_Input, TRUE,
        MUIA_Listview_DoubleClick, TRUE,
        MUIA_CycleChain, TRUE,
        TAG_DONE);

    data->txt_asm_info = TextObject,
        MUIA_Text_Contents, (CONST_STRPTR)
            "Type a word (or leave blank) and pick Category/Latest to filter.",
        End;

    data->btn_asm_prev       = SimpleButton("< Prev");
    data->btn_asm_next       = SimpleButton("Next >");
    data->btn_asm_show_files = SimpleButton("Show Files");
    data->btn_asm_run        = SimpleButton("Run");
    data->btn_asm_mount      = SimpleButton("Mount");
    data->btn_asm_download   = SimpleButton("Download...");
    data->cyc_asm_drive      = CycleObject,
        MUIA_Cycle_Entries, asm_drive_labels,
        MUIA_Cycle_Active, 0,
        End;

    /* The whole tab. Pagination arrows sit directly under the results
     * table (alongside Show Files) so walking through pages happens in
     * the same visual region as reading results. */
    Object *tab = VGroup,
        Child, row_query,
        Child, data->txt_asm_info,
        Child, data->lst_asm_results,
        Child, HGroup,
            Child, data->btn_asm_show_files,
            Child, HSpace(0),                 /* stretch pushes arrows to the right */
            Child, data->btn_asm_prev,
            Child, data->btn_asm_next,
            End,
        Child, data->lst_asm_files,
        Child, HGroup,
            Child, Label("Drive:"),
            Child, data->cyc_asm_drive,
            Child, data->btn_asm_run,
            Child, data->btn_asm_mount,
            Child, data->btn_asm_download,
            Child, HSpace(0),
            End,
        End;

    return tab;
}

/* Wire notifications.  Must be called AFTER data->app has been created,
 * because MUIM_Notify needs a real Application object as its target — during
 * ApplicationObject construction `data->app` is still garbage. */
void
ConnectAssemblyEvents (struct AppData *data)
{
    if (!data || !data->app) return;

    DoMethod(data->btn_asm_search, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_SEARCH);
    DoMethod(data->str_asm_query, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
             data->app, 2, MUIM_Application_ReturnID, ID_ASM_SEARCH);
    DoMethod(data->btn_asm_prev, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_PREV);
    DoMethod(data->btn_asm_next, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_NEXT);
    DoMethod(data->lst_asm_results, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
             data->app, 2, MUIM_Application_ReturnID, ID_ASM_SHOW_FILES);
    DoMethod(data->btn_asm_show_files, MUIM_Notify, MUIA_Pressed, FALSE,
             data->app, 2, MUIM_Application_ReturnID, ID_ASM_SHOW_FILES);
    DoMethod(data->lst_asm_files, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
             data->app, 2, MUIM_Application_ReturnID, ID_ASM_RUN);
    DoMethod(data->btn_asm_run, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_RUN);
    DoMethod(data->btn_asm_mount, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_MOUNT);
    DoMethod(data->btn_asm_download, MUIM_Notify, MUIA_Pressed, FALSE, data->app, 2,
             MUIM_Application_ReturnID, ID_ASM_DOWNLOAD);

    /* Keyboard shortcut to focus the search field. */
    set(data->btn_asm_search, MUIA_ControlChar, (ULONG)'f');
}

/* Free any lists we allocated. main.c calls this during cleanup. */
void
DisposeAssemblyState (void)
{
    Asm_FreeItems(g_asm.results);   g_asm.results = NULL;
    Asm_FreeFiles(g_asm.files);     g_asm.files = NULL;
    g_asm.selected_item = NULL;
    g_asm.selected_file = NULL;
    g_asm.result_count = 0;
    g_asm.file_count = 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void
asm_status (struct AppData *data, CONST_STRPTR msg)
{
    UpdateStatus(data, msg, FALSE);
}

static AsmItem *
asm_item_at (LONG idx)
{
    AsmItem *e = g_asm.results;
    while (e && idx-- > 0) e = e->next;
    return e;
}

static AsmFile *
asm_file_at (LONG idx)
{
    AsmFile *e = g_asm.files;
    while (e && idx-- > 0) e = e->next;
    return e;
}

static BOOL
ext_matches (CONST_STRPTR path, CONST_STRPTR ext)
{
    if (!path || !ext) return FALSE;
    ULONG pl = strlen((char *)path), el = strlen((char *)ext);
    if (pl < el) return FALSE;
    return stricmp((char *)path + pl - el, (char *)ext) == 0;
}

static BOOL
ext_matches_any (CONST_STRPTR path, const char *const *exts)
{
    for (ULONG i = 0; exts[i]; i++) {
        if (ext_matches(path, (CONST_STRPTR)exts[i])) return TRUE;
    }
    return FALSE;
}

static void
asm_refresh_results_list (struct AppData *data)
{
    set(data->lst_asm_results, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_results, MUIM_List_Clear);

    AsmItem *e = g_asm.results;
    while (e) {
        /* MUI stores the pointer verbatim; the display hook reads the
         * cached fields (year_str / cat_str populated at parse time). */
        DoMethod(data->lst_asm_results, MUIM_List_InsertSingle,
                 (APTR)e, MUIV_List_Insert_Bottom);
        e = e->next;
    }
    set(data->lst_asm_results, MUIA_List_Quiet, FALSE);
}

static void
asm_refresh_files_list (struct AppData *data)
{
    set(data->lst_asm_files, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_files, MUIM_List_Clear);

    AsmFile *f = g_asm.files;
    while (f) {
        DoMethod(data->lst_asm_files, MUIM_List_InsertSingle,
                 (APTR)f, MUIV_List_Insert_Bottom);
        f = f->next;
    }
    set(data->lst_asm_files, MUIA_List_Quiet, FALSE);
}

/* ------------------------------------------------------------------ */
/* Handlers                                                            */
/* ------------------------------------------------------------------ */

/* Build an AQL query string from the four UI widgets.
 *
 * Rules:
 *   - If user text already contains ':' treat it as raw AQL and pass
 *     through verbatim.
 *   - Otherwise wrap non-empty text as `name:"<text>"` — quotes are
 *     required whenever the text has a space; quoting unconditionally is
 *     harmless and fixes the "test 64" → `name:test 64` → HTTP 463 bug.
 *   - Append `category:<key>` if the category cycle isn't "Any".
 *   - Append `latest:<key>`   if the latest   cycle isn't "Any".
 *   - Append `repo:<key>`     if the source   cycle isn't "Any".
 *   - If everything is empty/Any, fall back to `latest:1week` so there's
 *     always something on screen.
 */
static void
AsmComposeQuery (struct AppData *data, char *out, ULONG out_size)
{
    STRPTR text = NULL;
    get(data->str_asm_query, MUIA_String_Contents, &text);

    LONG cat_idx = 0, lat_idx = 0, src_idx = 0, rank_idx = 0;
    get(data->cyc_asm_category, MUIA_Cycle_Active, &cat_idx);
    get(data->cyc_asm_latest,   MUIA_Cycle_Active, &lat_idx);
    get(data->cyc_asm_source,   MUIA_Cycle_Active, &src_idx);
    get(data->cyc_asm_rank,     MUIA_Cycle_Active, &rank_idx);

    out[0] = '\0';

    BOOL have_text = text && *text;
    if (have_text) {
        if (strchr((char *)text, ':')) {
            /* Raw AQL — user knows what they're doing. */
            strncat(out, (char *)text, out_size - strlen(out) - 1);
        } else if (strchr((char *)text, '*')) {
            /* User already wrote wildcards — respect them. */
            strncat(out, "name:", out_size - strlen(out) - 1);
            strncat(out, (char *)text, out_size - strlen(out) - 1);
        } else {
            /* Build a wildcard substring match:
             *   "drumbox"     → name:*drumbox*
             *   "drumbox 64"  → name:*drumbox*64*
             * Quoted wildcards (`"*x y*"`) are rejected by the server
             * with 463 when the string contains a space, so we stay
             * unquoted and replace each whitespace run with a '*'. */
            strncat(out, "name:*", out_size - strlen(out) - 1);
            ULONG olen = strlen(out);
            BOOL last_star = TRUE;  /* we just wrote one */
            for (const char *p = (const char *)text; *p; p++) {
                if (olen + 2 >= out_size) break;
                if (*p == ' ' || *p == '\t') {
                    if (!last_star) { out[olen++] = '*'; last_star = TRUE; }
                } else {
                    out[olen++] = *p;
                    last_star = FALSE;
                }
            }
            if (!last_star && olen + 2 < out_size) out[olen++] = '*';
            out[olen] = '\0';
        }
    }

    if (cat_idx > 0 && asm_category_keys[cat_idx]) {
        if (out[0]) strncat(out, " ", out_size - strlen(out) - 1);
        strncat(out, "category:", out_size - strlen(out) - 1);
        strncat(out, asm_category_keys[cat_idx], out_size - strlen(out) - 1);
    }
    if (lat_idx > 0 && asm_latest_keys[lat_idx]) {
        if (out[0]) strncat(out, " ", out_size - strlen(out) - 1);
        strncat(out, "latest:", out_size - strlen(out) - 1);
        strncat(out, asm_latest_keys[lat_idx], out_size - strlen(out) - 1);
    }
    if (src_idx > 0 && asm_source_keys[src_idx]) {
        if (out[0]) strncat(out, " ", out_size - strlen(out) - 1);
        strncat(out, "repo:", out_size - strlen(out) - 1);
        strncat(out, asm_source_keys[src_idx], out_size - strlen(out) - 1);
    }
    if (rank_idx > 0 && asm_rank_aql[rank_idx][0]) {
        if (out[0]) strncat(out, " ", out_size - strlen(out) - 1);
        strncat(out, asm_rank_aql[rank_idx], out_size - strlen(out) - 1);
    }

    if (!out[0]) {
        strncpy(out, "latest:1week", out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static void
AsmDoSearch (struct AppData *data)
{
    char composed[256];
    AsmComposeQuery(data, composed, sizeof(composed));
    ASM_LOG("Search pressed; AQL='%s'", composed);

    strncpy(g_asm.query, composed, sizeof(g_asm.query) - 1);
    g_asm.query[sizeof(g_asm.query) - 1] = '\0';
    g_asm.offset = 0;

    /* MUI stores raw pointers to our AsmItem/AsmFile structs — we MUST
     * clear the MUI lists before freeing the linked lists, or MUI's next
     * redraw reads freed memory. */
    set(data->lst_asm_files, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_files, MUIM_List_Clear);
    set(data->lst_asm_files, MUIA_List_Quiet, FALSE);
    Asm_FreeFiles(g_asm.files); g_asm.files = NULL; g_asm.file_count = 0;
    g_asm.selected_file = NULL;

    set(data->lst_asm_results, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_results, MUIM_List_Clear);
    set(data->lst_asm_results, MUIA_List_Quiet, FALSE);
    Asm_FreeItems(g_asm.results); g_asm.results = NULL; g_asm.result_count = 0;
    g_asm.selected_item = NULL;

    asm_status(data, (CONST_STRPTR)"Searching Assembly64...");

    LONG rc = Asm_Search((CONST_STRPTR)g_asm.query, g_asm.offset, g_asm.page_size,
                         &g_asm.results, &g_asm.result_count);
    ASM_LOG("Asm_Search rc=%ld count=%lu", (long)rc,
            (unsigned long)g_asm.result_count);
    asm_refresh_results_list(data);

    char msg[192];
    if (rc <= -1000) {
        /* Negative-encoded HTTP status from the library — give a hint if
         * it's Assembly64's AQL syntax code 463. */
        LONG http = -(rc + 1000);
        if (http == 463) {
            sprintf(msg, "AQL syntax error — use a field prefix, e.g. "
                         "name:%s or category:games latest:1week",
                         g_asm.query);
        } else {
            sprintf(msg, "Server error (HTTP %ld). Check query syntax.", http);
        }
    } else if (rc != U64_OK) {
        sprintf(msg, "Search failed (%s).", (char *)U64_GetErrorString(rc));
    } else if (g_asm.result_count == 0) {
        strcpy(msg, "No matches.");
    } else {
        sprintf(msg, "%lu results (page starting at %lu)",
                (unsigned long)g_asm.result_count, (unsigned long)g_asm.offset);
    }
    set(data->txt_asm_info, MUIA_Text_Contents, (CONST_STRPTR)msg);
    asm_status(data, (CONST_STRPTR)msg);
}

static void
AsmDoPage (struct AppData *data, BOOL forward)
{
    if (!g_asm.query[0]) {
        asm_status(data, (CONST_STRPTR)"Run a search first.");
        return;
    }
    if (forward)                          g_asm.offset += g_asm.page_size;
    else if (g_asm.offset >= g_asm.page_size) g_asm.offset -= g_asm.page_size;
    else                                  g_asm.offset  = 0;

    set(data->lst_asm_results, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_results, MUIM_List_Clear);
    set(data->lst_asm_results, MUIA_List_Quiet, FALSE);
    Asm_FreeItems(g_asm.results); g_asm.results = NULL; g_asm.result_count = 0;
    g_asm.selected_item = NULL;

    LONG rc = Asm_Search((CONST_STRPTR)g_asm.query, g_asm.offset, g_asm.page_size,
                         &g_asm.results, &g_asm.result_count);
    asm_refresh_results_list(data);

    char msg[128];
    if (rc != U64_OK) {
        sprintf(msg, "Paging failed (%s)", U64_GetErrorString(rc));
    } else if (g_asm.result_count == 0 && !forward) {
        strcpy(msg, "Already at first page.");
    } else if (g_asm.result_count == 0) {
        /* Ran past the end — snap back. */
        if (g_asm.offset >= g_asm.page_size) g_asm.offset -= g_asm.page_size;
        strcpy(msg, "No more results.");
    } else {
        sprintf(msg, "%lu results (offset %lu)",
                (unsigned long)g_asm.result_count, (unsigned long)g_asm.offset);
    }
    set(data->txt_asm_info, MUIA_Text_Contents, (CONST_STRPTR)msg);
    asm_status(data, (CONST_STRPTR)msg);
}

static void
AsmDoListFiles (struct AppData *data)
{
    LONG active = -1;
    get(data->lst_asm_results, MUIA_List_Active, &active);
    if (active < 0) {
        asm_status(data, (CONST_STRPTR)"Select a result first.");
        return;
    }
    AsmItem *item = asm_item_at(active);
    if (!item) { asm_status(data, (CONST_STRPTR)"No such result"); return; }
    g_asm.selected_item = item;

    set(data->lst_asm_files, MUIA_List_Quiet, TRUE);
    DoMethod(data->lst_asm_files, MUIM_List_Clear);
    set(data->lst_asm_files, MUIA_List_Quiet, FALSE);
    Asm_FreeFiles(g_asm.files); g_asm.files = NULL; g_asm.file_count = 0;
    g_asm.selected_file = NULL;

    asm_status(data, (CONST_STRPTR)"Fetching file list...");
    ASM_LOG("list files id=%s cat=%u", item->id, (unsigned)item->category);
    LONG rc = Asm_ListFiles((CONST_STRPTR)item->id, item->category,
                            &g_asm.files, &g_asm.file_count);
    ASM_LOG("Asm_ListFiles rc=%ld count=%lu",
            (long)rc, (unsigned long)g_asm.file_count);
    asm_refresh_files_list(data);

    char msg[192];
    if (rc <= -1000) {
        LONG http = -(rc + 1000);
        sprintf(msg, "Could not list files — server returned HTTP %ld", http);
    } else if (rc != U64_OK) {
        sprintf(msg, "Could not list files (%s)",
                (char *)U64_GetErrorString(rc));
    } else if (g_asm.file_count == 0) {
        sprintf(msg, "Item has no files listed (id=%s cat=%u)",
                item->id, (unsigned)item->category);
    } else {
        sprintf(msg, "%lu file(s) in \"%.80s\"",
                (unsigned long)g_asm.file_count, item->name);
    }
    asm_status(data, (CONST_STRPTR)msg);
}

static void
AsmDoRun (struct AppData *data)
{
    if (!data->connection) {
        asm_status(data, (CONST_STRPTR)"Not connected to Ultimate64.");
        return;
    }
    if (!g_asm.selected_item) {
        asm_status(data, (CONST_STRPTR)"Select a result and list files first.");
        return;
    }

    LONG active = -1;
    get(data->lst_asm_files, MUIA_List_Active, &active);
    if (active < 0) {
        asm_status(data, (CONST_STRPTR)"Select a file in the file list.");
        return;
    }
    AsmFile *f = asm_file_at(active);
    if (!f) { asm_status(data, (CONST_STRPTR)"No such file"); return; }

    /* Reject obviously-useless extensions before the download. */
    static const char *const supported[] = {
        ".prg", ".crt", ".d64", ".g64", ".d71", ".g71", ".d81", ".sid", NULL
    };
    if (!ext_matches_any((CONST_STRPTR)f->path, supported)) {
        asm_status(data,
            (CONST_STRPTR)"Unsupported file type for direct Run on Ultimate64.");
        return;
    }

    asm_status(data, (CONST_STRPTR)"Downloading from Assembly64...");
    char local_path[128];
    LONG rc = Asm_DownloadFile((CONST_STRPTR)g_asm.selected_item->id,
                               g_asm.selected_item->category,
                               f->id, (CONST_STRPTR)f->path,
                               local_path, sizeof(local_path));
    if (rc != U64_OK) {
        asm_status(data, (CONST_STRPTR)"Download failed");
        return;
    }

    /* For RunPRG / RunCRT / PlaySID we need the bytes in RAM; use the
     * existing common-file helper. For MountDisk the library wants a local
     * filename and handles the read itself. */
    STRPTR err = NULL;
    LONG run_rc = U64_ERR_GENERAL;

    if (ext_matches_any((CONST_STRPTR)f->path,
                        (const char *const []){".d64",".g64",".d71",".g71",".d81",NULL})) {
        LONG drv = 0;
        get(data->cyc_asm_drive, MUIA_Cycle_Active, &drv);
        if (drv < 0 || drv > 1) drv = 0;
        run_rc = U64_MountDisk(data->connection, (CONST_STRPTR)local_path,
                               (CONST_STRPTR)asm_drive_keys[drv],
                               U64_MOUNT_RW, TRUE, &err);
    } else {
        ULONG file_size = 0;
        UBYTE *file_data = U64_ReadFile((CONST_STRPTR)local_path, &file_size);
        if (!file_data) {
            asm_status(data, (CONST_STRPTR)"Could not read downloaded file");
            DeleteFile((STRPTR)local_path);
            return;
        }
        if (ext_matches((CONST_STRPTR)f->path, (CONST_STRPTR)".prg"))
            run_rc = U64_RunPRG(data->connection, file_data, file_size, &err);
        else if (ext_matches((CONST_STRPTR)f->path, (CONST_STRPTR)".crt"))
            run_rc = U64_RunCRT(data->connection, file_data, file_size, &err);
        else if (ext_matches((CONST_STRPTR)f->path, (CONST_STRPTR)".sid"))
            run_rc = U64_PlaySID(data->connection, file_data, file_size, 0, &err);

        FreeVec(file_data);
    }

    /* Temp file is no longer needed either way. */
    DeleteFile((STRPTR)local_path);

    char msg[320];
    if (run_rc == U64_OK) {
        sprintf(msg, "Running \"%.80s\" on Ultimate64", f->path);
    } else {
        sprintf(msg, "Run failed: %s",
                err ? (char *)err : (char *)U64_GetErrorString(run_rc));
    }
    if (err) FreeMem(err, strlen((char *)err) + 1);
    asm_status(data, (CONST_STRPTR)msg);
}

/* Mount the selected disk image without kicking off LOAD"*",8,1:RUN.
 *
 * This exists so multi-disk games (which typically have -a / -b / -c .d64
 * files) can have their other sides swapped in without re-triggering a
 * boot sequence — especially useful for disk 2 of a 2-sider that you
 * already loaded disk 1 of, or for manually issuing your own LOAD from
 * BASIC. Only applies to disk-image extensions; anything else is rejected. */
static void
AsmDoMount (struct AppData *data)
{
    if (!data->connection) {
        asm_status(data, (CONST_STRPTR)"Not connected to Ultimate64.");
        return;
    }
    if (!g_asm.selected_item) {
        asm_status(data, (CONST_STRPTR)"Select a result and list files first.");
        return;
    }
    LONG active = -1;
    get(data->lst_asm_files, MUIA_List_Active, &active);
    if (active < 0) {
        asm_status(data, (CONST_STRPTR)"Select a file in the file list.");
        return;
    }
    AsmFile *f = asm_file_at(active);
    if (!f) { asm_status(data, (CONST_STRPTR)"No such file"); return; }

    static const char *const disk_exts[] =
        { ".d64", ".g64", ".d71", ".g71", ".d81", NULL };
    if (!ext_matches_any((CONST_STRPTR)f->path, disk_exts)) {
        asm_status(data,
            (CONST_STRPTR)"Mount is only for .d64/.g64/.d71/.g71/.d81 files.");
        return;
    }

    asm_status(data, (CONST_STRPTR)"Downloading disk image...");
    char local_path[128];
    LONG rc = Asm_DownloadFile((CONST_STRPTR)g_asm.selected_item->id,
                               g_asm.selected_item->category,
                               f->id, (CONST_STRPTR)f->path,
                               local_path, sizeof(local_path));
    if (rc != U64_OK) {
        asm_status(data, (CONST_STRPTR)"Download failed");
        return;
    }

    LONG drv = 0;
    get(data->cyc_asm_drive, MUIA_Cycle_Active, &drv);
    if (drv < 0 || drv > 1) drv = 0;

    STRPTR err = NULL;
    LONG mount_rc = U64_MountDisk(data->connection, (CONST_STRPTR)local_path,
                                  (CONST_STRPTR)asm_drive_keys[drv],
                                  U64_MOUNT_RW, FALSE /* no run */,
                                  &err);
    DeleteFile((STRPTR)local_path);

    char msg[320];
    if (mount_rc == U64_OK) {
        sprintf(msg, "Mounted \"%.80s\" on drive %c (no auto-run)",
                f->path, drv == 1 ? 'B' : 'A');
    } else {
        sprintf(msg, "Mount failed: %s",
                err ? (char *)err : (char *)U64_GetErrorString(mount_rc));
    }
    if (err) FreeMem(err, strlen((char *)err) + 1);
    asm_status(data, (CONST_STRPTR)msg);
}

/* Download the selected file to a user-chosen local path. Unlike Run, this
 * never touches the Ultimate64 — it's pure "save to my Amiga disk". */
static void
AsmDoDownload (struct AppData *data)
{
    if (!g_asm.selected_item) {
        asm_status(data, (CONST_STRPTR)"Select a result and list files first.");
        return;
    }
    LONG active = -1;
    get(data->lst_asm_files, MUIA_List_Active, &active);
    if (active < 0) {
        asm_status(data, (CONST_STRPTR)"Select a file in the file list.");
        return;
    }
    AsmFile *f = asm_file_at(active);
    if (!f) { asm_status(data, (CONST_STRPTR)"No such file"); return; }

    /* Ask the user where to save. Pre-fill the filename with the path
     * component the server reported. */
    struct FileRequester *req = AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (CONST_STRPTR)"Save Assembly64 file to...",
        ASLFR_DoSaveMode, TRUE,
        ASLFR_InitialFile, (CONST_STRPTR)f->path,
        TAG_DONE);
    if (!req || !AslRequest(req, NULL)) {
        if (req) FreeAslRequest(req);
        return;  /* user cancelled */
    }

    char save_path[256];
    strncpy(save_path, (char *)req->rf_Dir, sizeof(save_path) - 1);
    save_path[sizeof(save_path) - 1] = '\0';
    AddPart((STRPTR)save_path, req->rf_File, sizeof(save_path));
    FreeAslRequest(req);

    asm_status(data, (CONST_STRPTR)"Downloading...");
    char tmp_path[128];
    LONG rc = Asm_DownloadFile((CONST_STRPTR)g_asm.selected_item->id,
                               g_asm.selected_item->category,
                               f->id, (CONST_STRPTR)f->path,
                               tmp_path, sizeof(tmp_path));
    if (rc != U64_OK) {
        asm_status(data, (CONST_STRPTR)"Download failed");
        return;
    }

    /* Move the temp file to the user's chosen destination. Rename first
     * (cheap on same volume); if cross-volume, fall back to a copy. */
    char msg[320];
    if (Rename((STRPTR)tmp_path, (STRPTR)save_path)) {
        sprintf(msg, "Saved to %s", save_path);
    } else {
        BPTR src = Open((STRPTR)tmp_path, MODE_OLDFILE);
        BPTR dst = src ? Open((STRPTR)save_path, MODE_NEWFILE) : 0;
        BOOL ok = FALSE;
        if (src && dst) {
            char buf[4096];
            LONG n;
            ok = TRUE;
            while ((n = Read(src, buf, sizeof(buf))) > 0) {
                if (Write(dst, buf, n) != n) { ok = FALSE; break; }
            }
            if (n < 0) ok = FALSE;
        }
        if (src) Close(src);
        if (dst) Close(dst);
        DeleteFile((STRPTR)tmp_path);
        if (ok) sprintf(msg, "Saved to %s", save_path);
        else    sprintf(msg, "Could not write %s", save_path);
    }
    asm_status(data, (CONST_STRPTR)msg);
}

/* ------------------------------------------------------------------ */
/* Dispatch from main loop                                             */
/* ------------------------------------------------------------------ */

BOOL
AsmDispatch (struct AppData *data, ULONG id)
{
    switch (id) {
    case ID_ASM_SEARCH:     AsmDoSearch(data);            return TRUE;
    case ID_ASM_PREV:       AsmDoPage(data, FALSE);       return TRUE;
    case ID_ASM_NEXT:       AsmDoPage(data, TRUE);        return TRUE;
    case ID_ASM_SHOW_FILES: AsmDoListFiles(data);         return TRUE;
    case ID_ASM_RUN:        AsmDoRun(data);               return TRUE;
    case ID_ASM_MOUNT:      AsmDoMount(data);             return TRUE;
    case ID_ASM_DOWNLOAD:   AsmDoDownload(data);          return TRUE;
    }
    return FALSE;
}

/* One-shot: populate the results pane with the last week's releases so the
 * Assembly64 tab isn't empty on first launch. Called once by main.c after
 * notifications are wired and the window is open. */
void
AsmKickstart (struct AppData *data)
{
    /* Seed the cycle gadgets to match what we're about to query. */
    set(data->cyc_asm_latest, MUIA_Cycle_Active, 4 /* "Week" */);
    AsmDoSearch(data);
}
