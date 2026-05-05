#include "app/app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"
#include "app/todo.h"
#include "os/time.h"

ienum (SortBy, U8) {
    SORT_BY_PIN,
    SORT_BY_DUE,
    SORT_BY_PRIORITY,
    SORT_BY_DONE,
    SORT_BY_HIDE,
    SORT_COUNT
};

istruct (Sort) {
    Bool ascending;
    SortBy by;
};

istruct (Deck) {
    Bool active;
    Buf *path;
    Buf *filters;
};

istruct (Task) {
    String text; // Superset of the task text. Use markup_ast_get_text().
    MarkupAst *ast;
    MarkupAstMetaConfig *config;
};

array_typedef(Sort, Sort);
array_typedef(Deck*, Deck);
array_typedef(Task*, Task);

istruct (KanbanColumn) {
    Bool with_header;
    MarkupAst *filter_node;
    String filter_text;
    ArrayTask tasks;
    U64 show_more_idx;
};

istruct (FilteredTrackerSlot) {
    U64 idx;
    Seconds total;
};

istruct (TimeTrackerSlot) {
    String task_str;
    MarkupAst *task_ast;
    Array(struct { Date date; Seconds seconds; }) time;
};

ienum (MainViewTag, U8) {
    MAIN_VIEW_SPARSE,
    MAIN_VIEW_COMPACT,
};

ienum (ViewTag, U8) {
    VIEW_MAIN,
    VIEW_SEARCH,
    VIEW_EDITOR,
    VIEW_SORT,
    VIEW_DECK_BROWSER,
    VIEW_TIME_TRACKER,
};

istruct (View) {
    ViewTag tag;

    union {
        struct {
            Bool has_filters;
            ArrayString filters;
            Array(KanbanColumn*) columns;
        } main;

        struct {
            Bool dragging;
            U64 draggee;
            U64 show_more;
        } sort;

        struct {
            Buf *buf;
            U64 cursor;
            Task *task_to_edit;
            Task task;
        } editor;

        struct {
            Bool delete_searched;
            Bool pin_searched;
            Bool hide_searched;
            Bool check_searched;
            U64 buf_version;
            U64 show_more_idx;
            Buf *buf;
            Buf *tags_add_buf;
            Buf *tags_del_buf;
            ArrayTask searched;
        } search;

        struct {
            Buf *buf;
            Bool delete_searched;
            Array(SearchResult) searched;
        } deck_browser;

        struct {
            U64 show_more_idx;
            Buf *filter_buf;
            Date since;
            Date until;
            Bool descending;
            Seconds today;
            Seconds month;
            Seconds year;
            Seconds all_time;
            Seconds custom;
            Date heatmap_date;
            Map(String, Seconds) heatmap_data;
            Array(FilteredTrackerSlot) filtered_slots;
        } time_tracker;
    };
};

ienum (CommandTag, U8) {
    CMD_SAVE_CONFIG,
    CMD_ADD_TASKS,
    CMD_DEL_TASK,
    CMD_NEW_TASK_FLAGS,
    CMD_TOGGLE_TASK_DONE,
    CMD_ADD_TAGS,
    CMD_DELETE_TAGS,
    CMD_START_TRACKING,
    CMD_STOP_TRACKING,
    CMD_NEW_TRACKER_SLOT,
    CMD_UPDATE_TRACKER_SLOT,
    CMD_SORT_TASKS,
    CMD_NEW_DECK,
    CMD_DEL_DECK,
    CMD_ACTIVATE_DECK,
    CMD_CHANGE_SORT,
    CMD_VIEW_MAIN,
    CMD_VIEW_EDITOR,
    CMD_VIEW_SORT,
    CMD_VIEW_SEARCH,
    CMD_VIEW_DECK_BROWSER,
    CMD_VIEW_TIME_TRACKER,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    Bool save_deck;
    MarkupAstMetaConfigFlags flags;
    Task *task;
    Task *task2;
    Deck *deck;
    U64 index;
    U64 index2;
    String str;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    MainViewTag main_view_type;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    ArraySort sorts;
    ArrayDeck decks;
    ArrayTask tasks;
    ArrayString non_tasks;
    Array(TimeTrackerSlot) tracker_slots;
    Buf *time_tracker_file;
    Task *tracked_task; // Is 0 when not tracking.
    Millisec active_track_total; // Total for today.
    Millisec tic_checkpoint;
    Millisec tic_save_prev;
    TickId tick_id;
    F32 editor_width;
    F32 editor_height;
};

static Context *context;

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

static Void save_active_deck ();
static Void save_time_tracker_data ();

String sort_to_string (SortBy sort) {
    switch (sort) {
    case SORT_BY_PIN:      return str("Sort by pin");
    case SORT_BY_DUE:      return str("Sort by due");
    case SORT_BY_PRIORITY: return str("Sort by priority");
    case SORT_BY_DONE:     return str("Sort by done");
    case SORT_BY_HIDE:     return str("Sort by hide");
    default:               return str("");
    }
}

I64 compare_tasks (Task *a, Task *b) {
    array_iter (sort, &context->sorts) {
        I64 result = 0;

        switch (sort.by) {
        case SORT_BY_PIN: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN);
            result = x - y;
        } break;
        case SORT_BY_DONE: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE);
            result = x - y;
        } break;
        case SORT_BY_HIDE: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE);
            result = x - y;
        } break;
        case SORT_BY_DUE: {
            String x = (a->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) ? a->config->due : str("9999-99-99");
            String y = (b->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) ? b->config->due : str("9999-99-99");
            array_iter (X, &x) {
                Char Y = array_get(&y, ARRAY_IDX);
                result = (X < Y) ? -1 : (X > Y) ? 1 : 0;
                if (result) break;
            }
        } break;
        case SORT_BY_PRIORITY: {
            U64 x  = (a->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) ? a->config->priority : UINT64_MAX;
            U64 y  = (b->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) ? b->config->priority : UINT64_MAX;
            result = (x < y) ? -1 : (x > y) ? 1 : 0;
        } break;
        default: break;
        }

        if (result) return sort.ascending ? result : -result;
    }

    return 0;
}

static Int cmp_tasks_ (Void *a_, Void *b_) {
    Task **a = a_;
    Task **b = b_;
    return compare_tasks(*a, *b);
}

static Void sort_tasks () {
    array_sort_cmp(&context->tasks, cmp_tasks_);
}

static Bool task_passes_filter_ (Task *task, MarkupAst *filter) {
    MarkupAstMetaConfig *c = task->config;
    String text = markup_ast_get_text(task->ast, task->text);
    MarkupAst *op1 = array_try_get(&filter->children, 0);
    MarkupAst *op2 = array_try_get(&filter->children, 1);

    switch (filter->tag) {
    case MARKUP_AST_FILTER_ERROR:    return false;
    case MARKUP_AST_FILTER_NOT:      return !task_passes_filter_(task, op1);
    case MARKUP_AST_FILTER_OR:       return task_passes_filter_(task, op1) || task_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_AND:      return task_passes_filter_(task, op1) && task_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_ANY:      return true;
    case MARKUP_AST_FILTER_DONE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_DONE;
    case MARKUP_AST_FILTER_PIN:      return c->flags & MARKUP_AST_META_CONFIG_HAS_PIN;
    case MARKUP_AST_FILTER_HIDE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_HIDE;
    case MARKUP_AST_FILTER_TAG:      return (c->flags & MARKUP_AST_META_CONFIG_HAS_TAGS) && map_has(&c->tags, cast(MarkupAstFilterTag*, filter)->text);
    case MARKUP_AST_FILTER_TRACK:    return (c->flags & MARKUP_AST_META_CONFIG_HAS_TRACK) && (cast(MarkupAstFilterTrack*, filter)->id == c->track);
    case MARKUP_AST_FILTER_FUZZY:    return str_fuzzy_search(cast(MarkupAstFilterFuzzy*, filter)->needle, text, 0) != INT64_MIN;
    case MARKUP_AST_FILTER_STRING:   return str_search(cast(MarkupAstFilterString*, filter)->needle, text) != ARRAY_NIL_IDX;
    case MARKUP_AST_FILTER_PRIORITY: return (c->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) && (cast(MarkupAstFilterPriority*, filter)->priority == c->priority);
    case MARKUP_AST_FILTER_DUE: {
        if (! (c->flags & MARKUP_AST_META_CONFIG_HAS_DUE)) return false;
        String filter_date_str = cast(MarkupAstFilterDue*, filter)->date;
        if (! filter_date_str.data) return true;
        Date filter_date = os_str_to_date(filter_date_str);
        Date task_date = os_str_to_date(c->due);
        switch (cast(MarkupAstFilterDue*, filter)->cmp) {
        case MARKUP_CMP_EQUAL:   return os_date_cmp(task_date, filter_date) == 0;
        case MARKUP_CMP_LESS:    return os_date_cmp(task_date, filter_date) == -1;
        case MARKUP_CMP_GREATER: return os_date_cmp(task_date, filter_date) == 1;
        }
        badpath;
    }
    case MARKUP_AST_FILTER_CREATED: {
        if (! (c->flags & MARKUP_AST_META_CONFIG_HAS_CREATED)) return false;
        String filter_date_str = cast(MarkupAstFilterCreated*, filter)->date;
        if (! filter_date_str.data) return true;
        Date filter_date = os_str_to_date(filter_date_str);
        Date task_date = os_str_to_date(c->created);
        switch (cast(MarkupAstFilterCreated*, filter)->cmp) {
        case MARKUP_CMP_EQUAL:   return os_date_cmp(task_date, filter_date) == 0;
        case MARKUP_CMP_LESS:    return os_date_cmp(task_date, filter_date) == -1;
        case MARKUP_CMP_GREATER: return os_date_cmp(task_date, filter_date) == 1;
        }
        badpath;
    }
    case MARKUP_AST_FILTER_COMPLETED: {
        if (! (c->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED)) return false;
        String filter_date_str = cast(MarkupAstFilterCompleted*, filter)->date;
        if (! filter_date_str.data) return true;
        Date filter_date = os_str_to_date(filter_date_str);
        Date task_date = os_str_to_date(c->completed);
        switch (cast(MarkupAstFilterCompleted*, filter)->cmp) {
        case MARKUP_CMP_EQUAL:   return os_date_cmp(task_date, filter_date) == 0;
        case MARKUP_CMP_LESS:    return os_date_cmp(task_date, filter_date) == -1;
        case MARKUP_CMP_GREATER: return os_date_cmp(task_date, filter_date) == 1;
        }
        badpath;
    }
    default: badpath;
    }
}

static Bool task_passes_filter (Task *task, MarkupAst *filter) {
    MarkupAst *op1 = array_try_get(&filter->children, 0);

    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
        if (filter->tag == MARKUP_AST_FILTER_ANY || filter->tag == MARKUP_AST_FILTER_HIDE) return true;
        if (! (filter->tag == MARKUP_AST_FILTER_AND && op1->tag == MARKUP_AST_FILTER_HIDE)) return false;
    }

    return task_passes_filter_(task, filter);
}

static Void task_serialize (Task *task) {
    AString a = astr_new(context->config_mem);

    astr_push_byte(&a, '[');
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE)      astr_push_cstr(&a, "x ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY)  astr_push_fmt(&a, "#%lu ", task->config->priority);
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TRACK)     astr_push_fmt(&a, "track:%lu ", task->config->track);
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_CREATED)   astr_push_fmt(&a, "created:%.*s ", STR(task->config->created));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED) astr_push_fmt(&a, "completed:%.*s ", STR(task->config->completed));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE)       astr_push_fmt(&a, "due:%.*s ", STR(task->config->due));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN)       astr_push_cstr(&a, "pin ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE)      astr_push_cstr(&a, "hide ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TAGS)      map_iter (s, &task->config->tags) astr_push_fmt(&a, "%.*s ", STR(s->key));
    if (array_get_last(&a) == ' ') a.count--;
    astr_push_byte(&a, ']');

    if (task->ast->children.count) {
        MarkupAst *first_child = array_get(&task->ast->children, 0);
        MarkupAst *last_child  = array_get_last(&task->ast->children);
        U64 start   = first_child->pos.offset;
        U64 end     = last_child->pos.offset + last_child->pos.length;
        String body = str_slice(task->text, start, end-start);
        astr_push_byte(&a, ' ');
        astr_push_str(&a, body);
    }

    if (! str_ends_with(astr_to_str(&a), str("\n"))) astr_push_byte(&a, '\n');

    task->text = astr_to_str(&a);
    MarkupAst *root = markup_parse(context->config_mem, task->text);
    task->ast = array_get(&root->children, 0);
    assert_always(task->ast->tag == MARKUP_AST_META);
    task->config = cast(MarkupAstMeta*, task->ast)->config;

    context->config_mem_fragmentation++;
}

static Bool is_valid_task (MarkupAst *node) {
    if (node->tag != MARKUP_AST_META) return false;
    Auto n = cast(MarkupAstMeta*, node);
    return !(n->config->flags & ~MARKUP_AST_META_TODO_FLAGS);
}

static Void add_task (String text, MarkupAst *ast) {
    assert_always(ast->tag == MARKUP_AST_META);
    Task *task   = mem_new(context->config_mem, Task);
    task->text   = text;
    task->ast    = ast;
    task->config = cast(MarkupAstMeta*, ast)->config;
    array_push(&context->tasks, task);
}

static Void add_tasks (String text) {
    MarkupAst *root = markup_parse(context->config_mem, text);

    array_iter (node, &root->children) {
        if (is_valid_task(node)) {
            add_task(text, node);
        } else {
            String t = markup_ast_get_text(node, text);
            array_push(&context->non_tasks, t);
        }
    }
}

static String time_to_str (Mem *mem, Millisec ms) {
    U64 seconds = (ms / 1000) % 60;
    U64 minutes = (ms / 60000) % 60;
    U64 hours   = ms / 3600000;
    return astr_fmt(mem, "%02lu:%02lu:%02lu", hours, minutes, seconds);
}

static TimeTrackerSlot *get_tracker_slot_for_task (Task *task) {
    U64 slot_idx = (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TRACK) ? task->config->track : ARRAY_NIL_IDX;
    return array_try_ref(&context->tracker_slots, slot_idx);
}

static Void assign_new_tracker_slot (Task *task) {
    task->config->flags |= MARKUP_AST_META_CONFIG_HAS_TRACK;
    task->config->track = context->tracker_slots.count;
    task_serialize(task);
    TimeTrackerSlot *slot = array_push_slot(&context->tracker_slots);
    slot->task_str = markup_ast_get_text(task->ast, task->text);
    slot->task_ast = task->ast;
    array_init(&slot->time, context->config_mem);
    array_push_lit(&slot->time, .date=os_get_date());
    save_time_tracker_data();
    save_active_deck();
}

static Seconds get_time_tracked_today (Task *task) {
    TimeTrackerSlot *slot = get_tracker_slot_for_task(task);
    if (! slot) return 0;
    Date date = os_get_date();
    array_iter_back (it, &slot->time, *) {
        if (! os_date_cmp(date, it->date)) {
            return it->seconds;
        }
    }
    return 0;
}

static Bool is_task_tracked (Task *task) {
    return context->tracked_task == task;
}

static Void stop_tracking () {
    context->tracked_task = 0;
    if (context->tick_id) {
        win_tick_end(context->tick_id);
        context->tick_id = 0;
    }
}

static Void start_tracking (Task *task) {
    context->tracked_task = task;
    if (context->tick_id == 0) context->tick_id = win_tick_start(1000);
    context->tic_checkpoint = os_get_time_ms();
    TimeTrackerSlot *slot = get_tracker_slot_for_task(task);
    if (! slot) assign_new_tracker_slot(task);
    context->active_track_total = get_time_tracked_today(task) * 1000;
}

static Deck *get_active_deck () {
    array_iter (deck, &context->decks) if (deck->active) return deck;
    return array_try_get(&context->decks, 0);
}

static Void save_active_deck () {
    tmem_new(tm);

    AString content = astr_new(tm);

    array_iter (task, &context->tasks) {
        String text = markup_ast_get_text(task->ast, task->text);
        astr_push_str(&content, text);
    }

    array_iter (non_task, &context->non_tasks) {
        astr_push_str(&content, non_task);
    }

    Deck *deck = get_active_deck();
    if (deck) fs_write_entire_file(buf_get_str(deck->path, tm), astr_to_str(&content));
}

static Void load_active_deck () {
    stop_tracking();
    context->config_mem_fragmentation += context->tasks.count;
    context->tasks.count = 0;
    context->non_tasks.count = 0;

    Deck *deck = get_active_deck();
    if (! deck) return;

    tmem_new(tm);
    String file = fs_read_entire_file(context->config_mem, buf_get_str(deck->path, tm), 0);
    add_tasks(file);
}

static Void save_time_tracker_data () {
    tmem_new(tm);

    String filepath = buf_get_str(context->time_tracker_file, tm);
    AString astr = astr_new(tm);

    astr_push_cstr(&astr, "slots = [\n");
    array_iter (slot, &context->tracker_slots, *) {
        String task_str = str_escape(tm, slot->task_str);
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        task = %.*s\n", STR(task_str));
        astr_push_cstr(&astr, "        time = [\n");
        array_iter (it, &slot->time, *) {
            String date = os_date_to_str(tm, it->date);
            astr_push_fmt(&astr, "            { date=\"%.*s\" seconds=%lu }\n", STR(date), it->seconds);
        }
        astr_push_cstr(&astr, "        ]\n");
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    fs_ensure_file(filepath, (String){});
    fs_write_entire_file(filepath, astr_to_str(&astr));
}

static Void load_time_tracker_data () {
    tmem_new(tm);

    String filepath = buf_get_str(context->time_tracker_file, tm);
    if (! fs_is_file(filepath)) return;

    context->config_mem_fragmentation += context->tracker_slots.count;
    context->tracker_slots.count = 0;

    Config *cfg = config_parse(tm, filepath);
    if (cfg->root->children.count == 0) return;

    ConfigAst *slots = config_get_array(cfg, cfg->root, "slots");
    array_iter (slot_ast, &slots->children) {
        TimeTrackerSlot *slot = array_push_slot(&context->tracker_slots);
        slot->task_str = str_unescape(context->config_mem, config_get_string(cfg, slot_ast, "task", tm));

        MarkupAst *root = markup_parse(context->config_mem, slot->task_str);
        slot->task_ast = array_try_get(&root->children, 0);

        array_init(&slot->time, context->config_mem);

        ConfigAst *times = config_get_array(cfg, slot_ast, "time");
        array_iter (time_ast, &times->children) {
            String key  = config_get_string(cfg, time_ast, "date", tm);
            Seconds val = config_get_u64(cfg, time_ast, "seconds");
            Date date   = os_str_to_date(key);
            if (date.month != 0) array_push_lit(&slot->time, date, val);
        }
    }
}

static Void save_config (Bool save_deck) {
    tmem_new(tm);

    if (save_deck) save_active_deck();

    AString astr = astr_new(tm);

    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);
    astr_push_fmt(&astr, "view_type = %u\n", context->main_view_type);

    String time_tracker_file = buf_get_str(context->time_tracker_file, tm);
    astr_push_fmt(&astr, "time_tracker_file = \"%.*s\"\n", STR(time_tracker_file));

    astr_push_cstr(&astr, "sort = [\n");
    array_iter (sort, &context->sorts) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr, "         ascending = %s\n", sort.ascending ? "true" : "false");
        astr_push_fmt(&astr, "         by        = %u\n", sort.by);
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    astr_push_cstr(&astr, "decks = [\n");
    array_iter (deck, &context->decks) {
        String path = buf_get_str(deck->path, tm);
        String filters = buf_get_str(deck->filters, tm);
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        active  = %s\n", deck->active ? "true" : "false");
        astr_push_fmt(&astr,  "        path    = \"%.*s\"\n", STR(path));
        astr_push_fmt(&astr,  "        filters = \"%.*s\"\n", STR(filters));
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    fs_ensure_file(context->config_file_path, (String){});
    fs_write_entire_file(context->config_file_path, astr_to_str(&astr));
}

static Void load_config () {
    tmem_new(tm);

    context->config_mem_fragmentation = 0;
    arena_pop_all(cast(Arena*, context->config_mem));
    array_init(&context->sorts, context->config_mem);
    array_init(&context->decks, context->config_mem);
    array_init(&context->tasks, context->config_mem);
    array_init(&context->non_tasks, context->config_mem);
    array_init(&context->tracker_slots, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/todo.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");
    context->main_view_type = config_get_u64(cfg, cfg->root, "view_type");
    context->time_tracker_file = buf_new(context->config_mem, config_get_string(cfg, cfg->root, "time_tracker_file", tm));

    ConfigAst *sorts = config_get_array(cfg, cfg->root, "sort");
    array_iter (sort, &sorts->children) {
        Sort s = {
            .ascending = config_get_bool(cfg, sort, "ascending"),
            .by = config_get_u64(cfg, sort, "by"),
        };
        array_push(&context->sorts, s);
    }

    ConfigAst *decks = config_get_array(cfg, cfg->root, "decks");
    array_iter (deck_ast, &decks->children) {
        Deck *deck    = mem_new(context->config_mem, Deck);
        deck->active  = config_get_bool(cfg, deck_ast, "active");
        deck->path    = buf_new(context->config_mem, config_get_string(cfg, deck_ast, "path", tm));
        deck->filters = buf_new(context->config_mem, config_get_string(cfg, deck_ast, "filters", tm));
        array_push(&context->decks, deck);
    }

    load_active_deck();
    load_time_tracker_data();
}

static void build_tracker_popup (Task *task) {
    tmem_new(tm);

    if (buf_get_count(context->time_tracker_file) == 0) {
        String text = str("[note] To track time you must select a backing text file in the time tracker ui.");
        ui_markup_view(str("markup"), text, 0);
        return;
    }

    U64 track_idx = (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TRACK) ? task->config->track : ARRAY_NIL_IDX;
    TimeTrackerSlot *slot = array_try_ref(&context->tracker_slots, track_idx);
    String task_str = markup_ast_get_text(task->ast, task->text);

    if (slot && !str_match(task_str, slot->task_str)) {
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 0});
        String mod = str_replace_all(tm, slot->task_str, str("\n"), str("\n  "));
        String info = astr_fmt(tm,
            "The text of the corresponding tracker slot does not match this task.\n"
            "Here is what the text in the slot currently looks like:\n"
            "> %.*s\n"
            "You can do one of the following:\n"
            "- Create a new slot leaving the old one in the tracker.\n"
            "- Update the current slot to match with this task.\n", STR(mod));
        ui_markup_view(str("markup"), info, 0);
        ui_button_group(str("linked")) {
            UiBox *new_button = ui_button(str("new")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("New slot"));
                if (new_button->signals.clicked) push_command(.tag=CMD_NEW_TRACKER_SLOT, .task=task);
            }

            UiBox *update_button = ui_button(str("update")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Update"));
                if (update_button->signals.clicked) push_command(.tag=CMD_UPDATE_TRACKER_SLOT, .task=task);
            }
        }
    } else {
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width/2, 0});
        if (is_task_tracked(task)) {
            ui_box(0, "clock") {
                ui_label(0, "title", str("Time tracked today:"));
                ui_hspacer();
                String label = time_to_str(tm, context->active_track_total);
                UiBox *clock = ui_label_extra(0, "clock", label, ui->config->font_path_bold, ui->config->font_size, 0);
                ui_style_box_vec4(clock, UI_TEXT_COLOR, ui->theme->text_color_red);
            }
        }

        ui_button_group(str("linked")) {
            if (is_task_tracked(task)) {
                UiBox *stop_button = ui_button(str("stop")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Stop"));
                    if (stop_button->signals.clicked) push_command(.tag=CMD_STOP_TRACKING);
                }
            } else {
                UiBox *start_button = ui_button(str("start")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Start"));
                    if (start_button->signals.clicked) push_command(.tag=CMD_START_TRACKING, .task=task);
                }
            }

            UiBox *stats_button = ui_button(str("stats")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Stats"));
                if (stats_button->signals.clicked) push_command(.tag=CMD_VIEW_TIME_TRACKER, .task=task);
            }
        }
    }
}

static Void build_task_body (Task *task) {
    tmem_new(tm);

    ui_style_rule(".tag_button") {
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
        ui_style_vec2(UI_PADDING, vec2(4, 2));
    }

    ui_box(0, "body") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        if (task->config->flags & (MARKUP_AST_META_CONFIG_HAS_PRIORITY | MARKUP_AST_META_CONFIG_HAS_DUE | MARKUP_AST_META_CONFIG_HAS_CREATED | MARKUP_AST_META_CONFIG_HAS_COMPLETED)) {
            ui_box(0, "dates") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) {
                    UiBox *priority_button = ui_button(str("priority_button")) {
                        ui_tag("tag_button");
                        priority_button->next_style.size.width.strictness = 1;
                        String label = astr_fmt(tm, "#%lu", task->config->priority);
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_red);
                        if (priority_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH, .str=astr_fmt(context->view_mem, "#%lu", task->config->priority));
                    }
                }

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) {
                    UiBox *due_button = ui_button(str("due_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Due", STR(task->config->due));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_red);
                        if (due_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH, .str=astr_fmt(context->view_mem, "due %.*s", STR(task->config->due)));
                    }
                }

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_CREATED) {
                    UiBox *created_button = ui_button(str("created_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Created", STR(task->config->created));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_green);
                        if (created_button->signals.clicked) {
                            push_command(.tag=CMD_VIEW_SEARCH, .str=astr_fmt(context->view_mem, "created %.*s", STR(task->config->created)));
                        }
                    }
                }

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED) {
                    UiBox *completed_button = ui_button(str("completed_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Completed", STR(task->config->completed));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_green);
                        if (completed_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH, .str=astr_fmt(context->view_mem, "completed %.*s", STR(task->config->completed)));
                    }
                }
            }
        }

        if (task->config->tags.umap.count) {
            ui_box(0, "tags") {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_f32(UI_SPACING, ui->theme->spacing);

                U64 col_count = 0;
                U64 row_count = 0;
                UiBox *box = ui_box(0, "row0");
                ui_style_box_f32(box, UI_SPACING, ui->theme->spacing);

                map_iter (slot, &task->config->tags) {
                    if (col_count == 3) {
                        col_count = 0;
                        row_count++;
                        box = ui_box_fmt(0, "row%lu", row_count);
                        ui_style_box_f32(box, UI_SPACING, ui->theme->spacing);
                    }

                    ui_parent(box) {
                        UiBox *tag_button = ui_button(astr_fmt(tm, "tag%lu", col_count)) {
                            ui_tag("tag_button");
                            tag_button->flags |= UI_BOX_CLIPPING;
                            String label = astr_fmt(tm, "%.*s", STR(slot->key));
                            UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                            ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_yellow);
                            if (tag_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH, .str=str_copy(context->view_mem, label));
                        }
                    }

                    col_count++;
                }
            }
        }

        String text = markup_ast_get_text(task->ast, task->text);
        String clicked_tag = {};
        ui_markup_view(str("markup"), text, &clicked_tag);
        if (clicked_tag.data) push_command(.tag=CMD_VIEW_SEARCH, .str=clicked_tag);
    }
}

static UiBox *build_task (Task *task, Bool compact, Bool *out_deleted) {
    U64 n = array_get_last(&ui->box_stack)->children.count;
    UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "task%lu", n) {
        ui_tag("card");
        ui_style_vec2(UI_PADDING, vec2(ui->theme->border_width.x, ui->theme->border_width.x));

        if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
            ui_style_rule(".card") ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
        }

        if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) {
            Date today = os_get_date();
            Date due = os_str_to_date(task->config->due);
            if (os_date_cmp(due, today) == -1 && !(task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE)) {
                ui_style_rule(".card") ui_style_vec4(UI_BG_COLOR, ui->theme->color_red);
            }
        }

        if (compact) {
            ui_style_rule(".card #header") {
                ui_style_vec4(UI_RADIUS, ui->theme->radius);
                ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, 0));
            }
        }

        ui_box(0, "header") {
            if (is_task_tracked(task)) {
                ui_style_vec4(UI_BG_COLOR, ui->theme->color_red);
            }

            Bool checked = task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE;
            UiBox *checkbox = ui_checkbox("checkbox", &checked);
            if (checkbox->signals.clicked) {
                push_command(.tag=CMD_TOGGLE_TASK_DONE, .task=task);
                push_command(.tag=CMD_VIEW_MAIN);
            }

            if (compact) {
                String text = {};
                if (task->ast->children.count) {
                    MarkupAst *first_child = array_get(&task->ast->children, 0);
                    if (first_child->tag == MARKUP_AST_PARAGRAPH) {
                        text = markup_ast_get_text(first_child, task->text);
                        text = str_prefix_to_first(text, '\n');
                    }
                }

                String clicked_tag = {};
                UiBox *m = ui_markup_view(str("markup"), text, &clicked_tag);
                m->flags |= UI_BOX_CLIPPING;
                Font *font = font_get(ui->font_cache, ui->config->font_path_normal, ui->config->font_size);
                ui_style_box_size(m, UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, font->height, 1});

                if (clicked_tag.data) push_command(.tag=CMD_VIEW_SEARCH, .str=clicked_tag);
            } else {
                ui_hspacer();
            }

            if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                ui_box(0, "autohide_icons") {
                    if (compact) {
                        UiBox *preview_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "preview") {
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EYE);
                            Bool opened = preview_button->scratch;

                            if (opened || preview_button->signals.clicked) {
                                ui_tag("press");
                                ui_grab_focus(box);
                                ui_popup(str("popup"), &opened, false, preview_button) {
                                    ui_box(0, "popup") {
                                        ui_tag("card");
                                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 0});
                                        ui_style_f32(UI_SPACING, ui->theme->spacing);
                                        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                                        build_task_body(task);
                                    }
                                }
                            }

                            preview_button->scratch = opened;
                        }
                    }

                    UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .task=task);
                    }

                    UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                        if (delete_button->signals.clicked) {
                            if (out_deleted) *out_deleted = true;
                            push_command(.tag=CMD_DEL_TASK, .task=task);
                        }
                    }

                    UiBox *tracker_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "tracker") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TIME_TRACKER);
                        Bool opened = tracker_button->scratch;

                        if (opened || tracker_button->signals.clicked) {
                            ui_tag("press");
                            ui_grab_focus(box);
                            ui_popup(str("popup"), &opened, false, tracker_button) {
                                ui_box(0, "popup") {
                                    ui_style_f32(UI_SPACING, ui->theme->spacing);
                                    ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                                    build_tracker_popup(task);
                                }
                            }
                        }

                        tracker_button->scratch = opened;
                    }

                    if (! (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN)) {
                        UiBox *pin_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "pin") {
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PIN);
                            if (pin_button->signals.clicked) {
                                push_command(.tag=CMD_NEW_TASK_FLAGS, .task=task, .flags=(task->config->flags | MARKUP_AST_META_CONFIG_HAS_PIN));
                                push_command(.tag=CMD_VIEW_MAIN);
                            }
                        }
                    }
                }
            }

            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN) {
                UiBox *pin_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "pin") {
                    ui_tag("button");
                    ui_style_rule(".button *") { ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_green); }
                    ui_style_rule(".button.hover *") { ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue); }
                    ui_style_rule(".button.focus") {
                        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color_focus);
                        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width_focus);
                    }

                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PIN);
                    pin_button->next_style.size.width.strictness = 1;
                    if (pin_button->signals.clicked) {
                        push_command(.tag=CMD_NEW_TASK_FLAGS, .task=task, .flags=(task->config->flags & ~MARKUP_AST_META_CONFIG_HAS_PIN));
                        push_command(.tag=CMD_VIEW_MAIN);
                    }
                }
            }

            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
                UiBox *hide_button = ui_box(0, "hide") {
                    hide_button->next_style.size.width.strictness = 1;
                    ui_tag("button");
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_HIDDEN);
                }
            }
        }

        if (! compact) build_task_body(task);
    }

    return box;
}

static UiBox *preview_builder (MarkupView *info, MarkupAst *node) {
    UiBox *parent = array_get_last(&ui->box_stack);
    if (parent != info->root) return 0; // Not top level markup node.

    if (is_valid_task(node)) {
        Task dummy = {
            .text   = info->text,
            .ast    = node,
            .config = cast(MarkupAstMeta*, node)->config,
        };
        EventTag etag = ui->event->tag;
        ui->event->tag = EVENT_DUMMY;
        UiBox *box = build_task(&dummy, false, 0);
        ui->event->tag = etag;
        return box;
    } else {
        UiBox *container = ui_box_fmt(0, "non_task%lu", parent->children.count) {
            ui_tag("card");
            ui_style_vec2(UI_PADDING, vec2(ui->theme->border_width.x, ui->theme->border_width.x));

            ui_box(0, "header") {
                ui_style_vec4(UI_BG_COLOR, ui->theme->color_red);
                ui_label(0, "label", str("Non task. Valid tasks must start with square brackets."));
                ui_hspacer();
            }

            ui_box(0, "body") {
                String text = markup_ast_get_text(node, info->text);
                ui_markup_view(str("markup"), text, 0);
            }
        }

        return container;
    }
}

static Void build_view_editor () {
    Auto view = &context->view.editor;

    UiBox *editor = 0;
    UiBox *preview = 0;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        F32 r = ui->theme->radius.x;
        F32 w = context->editor_width;
        F32 h = context->editor_height;

        ui_box(UI_BOX_INVISIBLE_BG, "message") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *header = ui_box(0, "header") {
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_style_box_size(header, UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

                ui_label(0, "title", str("Tasks"));
            }

            editor = ui_ted_resizable(str("editor"), view->buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        if (w != context->editor_width || h != context->editor_height) save_config(false);

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            ui_button_group(str("linked")) {
                UiBox *cancel_button = ui_button(str("cancel")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                    if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
                }

                UiBox *ok_button = ui_button(str("ok")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Ok"));
                    if (ok_button->signals.clicked) {
                        if (view->task_to_edit) push_command(.tag=CMD_DEL_TASK, .task=view->task_to_edit, .skip_config_save=true);
                        if (! buf_ends_with_newline(view->buf)) buf_insert(view->buf, buf_get_count(view->buf), str("\n"));
                        String str = buf_get_str(view->buf, context->config_mem);
                        push_command(.tag=CMD_ADD_TASKS, .str=str);
                        push_command(.tag=CMD_VIEW_MAIN);
                    }
                }
            }

            ui_button_info_popup(str("help_button"), true, str("data/docs/todo.txt"), true);
        }
    }

    UiBox *scrollbox = ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        ui_box(UI_BOX_INVISIBLE_BG, "previews") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 1});

            U64 count = context->tasks.count;
            preview = ui_markup_view_buf(str("markup"), view->buf, true, 0, preview_builder);
            context->tasks.count = count;
        }
    }

    if (ui->focused && ui_is_descendant(editor, ui->focused)) {
        app_sync_scroll(scrollbox, editor, preview, &view->cursor);
    }
}

static Void build_view_search () {
    tmem_new(tm);

    Auto view = &context->view.search;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_box(0, "search_box") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);
            ui_entry(str("entry"), view->buf, -1, str("Filter expression..."));
            ui_button_info_popup(str("help_button"), true, str("data/docs/filters.txt"), true);
        }

        ui_box(UI_BOX_INVISIBLE_BG, "checkboxes") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "delete") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched tasks"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->delete_searched);
            }

            ui_box(0, "pin") {
                ui_tag("row");
                ui_label(0, "title", str("Mark searched tasks as pinned"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->pin_searched);
            }

            ui_box(0, "hide") {
                ui_tag("row");
                ui_label(0, "title", str("Mark searched tasks as hidden"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->hide_searched);
            }

            ui_box(0, "check") {
                ui_tag("row");
                ui_label(0, "title", str("Mark searched tasks as done"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->check_searched);
            }
        }

        ui_box(UI_BOX_INVISIBLE_BG, "add_del_tags") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "add_tags") {
                ui_tag("row");
                ui_label(0, "title", str("Add tags"));
                ui_hspacer();
                UiBox *entry = ui_entry(str("entry"), view->tags_add_buf, 25, str("Tags comma separated..."));
                entry->next_style.size.width.strictness = 1;
            }

            ui_box(0, "remove_tags") {
                ui_tag("row");
                ui_label(0, "title", str("Delete tags"));
                ui_hspacer();
                UiBox *entry = ui_entry(str("entry"), view->tags_del_buf, 25, str("Tags comma separated..."));
                entry->next_style.size.width.strictness = 1;
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *close_button = ui_button(str("close")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                if (close_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *apply_button = ui_button(str("apply")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Apply"));
                if (apply_button->signals.clicked) {
                    if (view->delete_searched) {
                        array_iter (task, &view->searched) push_command(.tag=CMD_DEL_TASK, .task=task, .skip_config_save=true);
                        view->searched.count = 0;
                    } else {
                        array_iter (task, &view->searched) {
                            MarkupAstMetaConfigFlags flags = task->config->flags;
                            if (view->pin_searched) flags |= MARKUP_AST_META_CONFIG_HAS_PIN;
                            if (view->hide_searched) flags |= MARKUP_AST_META_CONFIG_HAS_HIDE;
                            if (view->check_searched) flags |= MARKUP_AST_META_CONFIG_HAS_DONE;
                            push_command(.tag=CMD_NEW_TASK_FLAGS, .task=task, .flags=flags, .skip_config_save=true);
                        }

                        String str = buf_get_str(view->tags_add_buf, context->view_mem);
                        if (str.count) array_iter (task, &view->searched) push_command(.tag=CMD_ADD_TAGS, .task=task, .str=str, .skip_config_save=true);

                        str = buf_get_str(view->tags_del_buf, context->view_mem);
                        if (str.count) array_iter (task, &view->searched) push_command(.tag=CMD_DELETE_TAGS, .task=task, .str=str, .skip_config_save=true);
                    }

                    push_command(.tag=CMD_SORT_TASKS);
                    push_command(.tag=CMD_SAVE_CONFIG, .save_deck=true);
                }
            }
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        if (view->buf_version != buf_get_version(view->buf)) {
            view->searched.count = 0;
            view->buf_version = buf_get_version(view->buf);

            String filter_text = buf_get_str(view->buf, tm);
            MarkupAst *filter_node = markup_filter_parse(tm, filter_text);

            array_iter (task, &context->tasks) {
                if (task_passes_filter(task, filter_node)) {
                    array_push(&view->searched, task);
                }
            }
        }

        Bool deleted = false;

        array_iter (task, &view->searched) {
            if (ARRAY_IDX == view->show_more_idx) break;
            build_task(task, false, &deleted);
        }

        app_show_more_button(str("show_more"), &view->show_more_idx, view->searched.count);

        if (deleted) view->buf_version--; // To refresh the searched array.
    }
}

static Void build_view_sort () {
    Auto view = &context->view.sort;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_box(0, "items") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_style_rule(".item") {
                F32 b = ui->theme->border_width.x;
                ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0));
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_vec4(UI_BORDER_WIDTHS, vec4(b, b, b, 0));
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            }

            ui_style_rule(".item:first") {
                F32 r = ui->theme->radius.x;
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
            }

            ui_style_rule(".item:last") {
                F32 r = ui->theme->radius.x;
                ui_style_vec4(UI_RADIUS, vec4(0, 0, r, r));
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            }

            ui_style_rule(".button.hover *") {
                ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
            }

            ui_style_rule(".button.focus") {
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color_focus);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width_focus);
            }

            array_iter (sort, &context->sorts) {
                UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "sort%lu", ARRAY_IDX) {
                    ui_tag("item");

                    if (view->dragging && ui_within_box(box->rect, ui->mouse)) {
                        ui_style_rule(".item") ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z1);
                        ui_label(0, "label", str(""));
                        ui_hspacer();
                        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                            view->dragging = false;
                            push_command(.tag=CMD_CHANGE_SORT, .index=view->draggee, .index2=ARRAY_IDX);
                        }
                    } else {
                        ui_label(0, "label", sort_to_string(sort.by));
                        ui_hspacer();

                        if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                            UiBox *up_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "up") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_UP);
                                up_button->next_style.size.width.strictness = 1;
                                if (up_button->signals.clicked && ARRAY_IDX > 0) push_command(.tag=CMD_CHANGE_SORT, .index=ARRAY_IDX, .index2=ARRAY_IDX-1);
                            }

                            UiBox *down_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "down") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_DOWN);
                                down_button->next_style.size.width.strictness = 1;
                                if (down_button->signals.clicked && !ARRAY_ITER_DONE) push_command(.tag=CMD_CHANGE_SORT, .index=ARRAY_IDX, .index2=ARRAY_IDX+1);
                            }
                        }

                        UiBox *sort_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "sort") {
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", sort.ascending ? UI_ICON_SORT_ASC : UI_ICON_SORT_DESC);
                            sort_button->next_style.size.width.strictness = 1;
                            if (sort_button->signals.clicked) push_command(.tag=CMD_CHANGE_SORT, .index=ARRAY_IDX, .index2=ARRAY_NIL_IDX);
                        }
                    }

                    if (box->signals.pressed && (ui->event->tag == EVENT_MOUSE_MOVE)) {
                        view->dragging = true;
                        view->draggee = ARRAY_IDX;
                    }
                }
            }

            if (view->dragging) {
                ui_parent(ui->root) {
                    ui_box(0, "preview") {
                        ui_style_f32(UI_FLOAT_X, ui->mouse.x + 10);
                        ui_style_f32(UI_FLOAT_Y, ui->mouse.y + 10);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 10*ui->config->font_size, 1});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 2*ui->config->font_size, 1});
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                    }
                }
            }
        }

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            UiBox *close_button = ui_button(str("close")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                if (close_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            ui_button_info_popup(str("help_button"), true, str("data/docs/sort.txt"), true);
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        array_iter (task, &context->tasks) {
            if (ARRAY_IDX == view->show_more) break;
            build_task(task, false, 0);
        }

        app_show_more_button(str("show_more"), &view->show_more, context->tasks.count);
    }

    if (view->dragging && ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
        view->dragging = false;
    }
}

static Void build_view_deck_browser () {
    tmem_new(tm);
    Auto view = &context->view.deck_browser;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("search_entry"), view->buf, -1, str("Search decks..."));

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            ui_button_group(str("linked")) {
                UiBox *close_button = ui_button(str("close")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                    if (close_button->signals.clicked) {
                        Deck *deck = 0;
                        array_iter (it, &context->decks) if (it->active) deck = it;
                        push_command(.tag=CMD_SAVE_CONFIG);
                        push_command(.tag=CMD_ACTIVATE_DECK, .deck=deck);
                        push_command(.tag=CMD_VIEW_MAIN);
                    }
                }

                UiBox *add_button = ui_button(str("add")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Add"));
                    if (add_button->signals.clicked) push_command(.tag=CMD_NEW_DECK);
                }
            }

            String todo = fs_read_entire_file(tm, str("data/docs/todo.txt"), 0);
            String filters = fs_read_entire_file(tm, str("data/docs/filters.txt"), 0);
            String info = astr_fmt(tm, "%.*s\n%.*s", STR(todo), STR(filters));
            ui_button_info_popup(str("help_button"), true, info, false);
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        { // Search:
            view->searched.count = 0;

            String needle = buf_get_str(view->buf, tm);

            array_iter (deck, &context->decks) {
                I64 score = str_fuzzy_search(needle, buf_get_str(deck->path, tm), 0);
                if (score != INT64_MIN) array_push_lit(&view->searched, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched, app_cmp_search_results);
        }

        array_iter (d, &view->searched) {
            Deck *deck = array_get(&context->decks, d.idx);

            UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "deck%lu", d.idx) {
                ui_tag("card");

                ui_box(0, "header") {
                    UiBox *checkbox = ui_checkbox("checkbox", &deck->active);
                    if (checkbox->signals.clicked && deck->active) push_command(.tag=CMD_ACTIVATE_DECK, .deck=deck);

                    ui_hspacer();

                    if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                        ui_box(0, "autohide_icons") {
                            UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                                if (delete_button->signals.clicked) push_command(.tag=CMD_DEL_DECK, .deck=deck);
                            }
                        }
                    }
                }

                ui_box(0, "body") {
                    ui_file_picker_entry(str("file"), deck->path, str("Select file..."), -1, false, false, (String){});
                    ui_entry(str("filters"), deck->filters, -1, str("Comma separated list of filter expressions..."));
                }
            }
        }
    }
}

static Int cmp_filtered_slots_ascending (Void *a_, Void *b_) {
    FilteredTrackerSlot *a = a_;
    FilteredTrackerSlot *b = b_;
    return (a->total < b->total) ? -1 : (a->total > b->total) ? 1 : 0;
}

static Int cmp_filtered_slots_descending (Void *a_, Void *b_) {
    FilteredTrackerSlot *a = a_;
    FilteredTrackerSlot *b = b_;
    return (a->total < b->total) ? 1 : (a->total > b->total) ? -1 : 0;
}

static Void compute_time_tracker_stats () {
    tmem_new(tm);

    assert_dbg(context->view.tag == VIEW_TIME_TRACKER);
    Auto view = &context->view.time_tracker;

    String filter = buf_get_str(view->filter_buf, tm);
    MarkupAst *filter_node = markup_filter_parse(tm, filter);

    Date since = view->since;
    Date until = view->until;

    if (! os_is_date_ymd_valid(since)) since = (Date){};
    if (! os_is_date_ymd_valid(until)) until = (Date){9999, 99, 99, 0};
    if (os_date_cmp(since, until) == 1) swap(since, until);

    // Filter slots:
    view->filtered_slots.count = 0;
    array_iter (slot, &context->tracker_slots, *) {
        MarkupAstMetaConfig config = {};
        if (slot->task_ast->tag == MARKUP_AST_META) config = *cast(MarkupAstMeta*, slot->task_ast)->config;
        Task dummy = { .text=slot->task_str, .ast=slot->task_ast, .config=&config };
        if (task_passes_filter(&dummy, filter_node)) {
            Seconds total = 0;
            array_iter (t, &slot->time, *) {
                if (os_date_cmp(t->date, since) != -1 && os_date_cmp(t->date, until) != 1) {
                    total = sat_add64(total, t->seconds);
                }
            }
            array_push_lit(&view->filtered_slots, .idx=ARRAY_IDX, .total=total);
        }
    }

    // Sort slots:
    if (view->descending) {
        array_sort_cmp(&view->filtered_slots, cmp_filtered_slots_descending);
    } else {
        array_sort_cmp(&view->filtered_slots, cmp_filtered_slots_ascending);
    }

    { // Compute totals and heatmap stats:
        view->today    = 0;
        view->month    = 0;
        view->year     = 0;
        view->all_time = 0;
        view->custom   = 0;
        map_clear(&view->heatmap_data);

        Date today = os_get_date();
        Date start_of_month = { today.year, today.month, 1, 0 };
        Date start_of_year = { today.year, 1, 1, 0 };

        array_iter (it, &view->filtered_slots, *) {
            TimeTrackerSlot *slot = array_ref(&context->tracker_slots, it->idx);

            array_iter (t, &slot->time, *) {
                view->all_time = sat_add64(view->all_time, t->seconds);
                I32 tcmp = os_date_cmp(t->date, today);
                if (tcmp == 0) view->today = sat_add64(view->today, t->seconds);
                if (os_date_cmp(t->date, start_of_month) != -1 && tcmp != 1) view->month = sat_add64(view->month, t->seconds);
                if (os_date_cmp(t->date, start_of_year) != -1 && tcmp != 1) view->year = sat_add64(view->year, t->seconds);
                if (os_date_cmp(t->date, since) != -1 && os_date_cmp(t->date, until) != 1) view->custom = sat_add64(view->custom, t->seconds);
                if (t->date.year == view->heatmap_date.year) {
                    String date_str = os_date_to_str(context->view_mem, t->date);
                    Seconds total = 0;
                    map_get(&view->heatmap_data, date_str, &total);
                    map_add(&view->heatmap_data, date_str, sat_add64(total, t->seconds));
                }
            }
        }
    }
}

static Vec4 get_color_for_time (Seconds total) {
    U32 hours = total / 3600;
    Vec4 a = ui->theme->bg_color_z2;
    Vec4 b = ui->theme->color_green;

    if (total == 0) {
        return a;
    } else if (hours < 2) {
        return lerp(a, b, .4);
    } else if (hours < 4) {
        return lerp(a, b, .7);
    } else if (hours < 8) {
        return lerp(a, b, .85);
    }

    return b;
}

static Void build_heatmap () {
    tmem_new(tm);

    assert_dbg(context->view.tag == VIEW_TIME_TRACKER);
    Auto view = &context->view.time_tracker;

    Date today = os_get_date();
    U32 month_spacing = 10;
    U32 day_spacing = 2;

    ui_box(0, "container") {
        ui_tag("card");

        F32 b = ui->theme->border_width.x;
        ui_style_vec2(UI_PADDING, vec2(b, b));

        ui_box(0, "header") {
            ui_label(0, "label", str("Day totals"));
            ui_hspacer();
            EventTag etag = ui->event->tag;
            I64 year = view->heatmap_date.year;
            UiBox *picker = ui_int_picker(str("picker"), &year, 0, 9999, 4);
            view->heatmap_date.year = year;
            picker->next_style.size.width.strictness = 1;
            if (ui->event->tag != etag) compute_time_tracker_stats();
        }

        ui_scroll_box(str("inner"), true) {
            ui_style_f32(UI_SPACING, month_spacing);
            ui_style_vec2(UI_PADDING, vec2(ui->theme->padding.x + 4, ui->theme->padding.x + 4));

            ui_style_rule(".cell") {
                ui_style_f32(UI_EDGE_SOFTNESS, 0);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->font_size, 1});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, ui->config->font_size, 1});
            }

            U32 year = view->heatmap_date.year;
            CString day_labels[] = {"Sun", "Mon","Tue","Wed","Thu","Fri","Sat"};
            CString month_labels[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

            for (U32 month = 1; month < 13; ++month) {
                U32 week = 0;
                U32 wday = os_first_weekday(year, month);
                U32 days = os_days_in_month(year, month);

                ui_box_fmt(0, "month%lu", month) {
                    ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                    ui_style_f32(UI_SPACING, ui->theme->spacing);
                    ui_label(0, "label", str(month_labels[month-1]));

                    ui_box_fmt(0, "grid") {
                        ui_style_f32(UI_SPACING, day_spacing);

                        UiBox *column = ui_box(0, "column0") {
                            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                            ui_style_f32(UI_SPACING, day_spacing);
                        }

                        for (U32 i = 0; i < wday; ++i) {
                            ui_parent(column) { ui_box_fmt(UI_BOX_INVISIBLE_BG, "cell%lu", i) ui_tag("cell"); }
                        }

                        for (U32 day = 1; day < days+1; ++day) {
                            if (wday == 7) {
                                wday = 0;
                                week++;
                                column = ui_box_fmt(0, "column%lu", week) {
                                    ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                                    ui_style_f32(UI_SPACING, day_spacing);
                                }
                            }

                            ui_parent(column) {
                                UiBox *cell = ui_box_fmt(UI_BOX_REACTIVE, "cell%lu", wday) {
                                    ui_tag("cell");

                                    Date date = {year, month, day, wday};
                                    String date_str = os_date_to_str(tm, date);

                                    Seconds total = 0;
                                    map_get(&view->heatmap_data, date_str, &total);

                                    Vec4 cell_color = get_color_for_time(total);

                                    ui_style_vec4(UI_BG_COLOR, cell_color);
                                    if (os_date_cmp(today, date) == 0) {
                                        ui_style_rule(".cell") ui_style_vec4(UI_BORDER_COLOR, ui->theme->color_yellow);
                                    }

                                    if (cell->signals.hovered) {
                                        ui_style_rule(".cell") ui_style_vec4(UI_BORDER_COLOR, ui->theme->color_yellow);
                                        ui_tooltip(str("tooltip")) {
                                            String date_str = os_date_to_str(tm, date);
                                            String time_str = time_to_str(tm, total*1000);
                                            String label = astr_fmt(tm, "%s %.*s (Total: %.*s)", day_labels[wday], STR(date_str), STR(time_str));
                                            ui_label(0, "label", label);
                                        }
                                    }

                                    if (cell->signals.clicked) {
                                        view->since = date;
                                        view->until = date;
                                        compute_time_tracker_stats();
                                    }
                                }
                            }

                            wday++;
                        }
                    }
                }
            }
        }
    }
}

static Void build_view_time_tracker () {
    tmem_new(tm);

    Auto view = &context->view.time_tracker;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        EventTag etag = ui->event->tag;

        ui_box(UI_BOX_INVISIBLE_BG, "row_group1") {
            ui_box(0, "row1") {
                ui_tag("row");

                ui_file_picker_entry(str("tracker_file"), context->time_tracker_file, str("Time tracker file..."), -1, false, false, (String){});

                String filepath = buf_get_str(context->time_tracker_file, tm);
                Bool is_file = fs_is_file(filepath);

                UiBox *button_group = ui_button_group(str("linked")) {
                    button_group->next_style.size.width.strictness = 1;

                    UiBox *edit_button = ui_button(str("edit")) {
                        edit_button->next_style.size.width.strictness = 1;
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) win_open_file_url(filepath);
                    }

                    UiBox *info_button = ui_button_info_popup(str("info_button"), true, str("data/docs/tracker.txt"), true);
                    info_button->next_style.size.width.strictness = 1;
                }


                if (! is_file) {
                    ui_style_rule("#entry #text_box") ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_red);
                } else if (ui->event->tag != etag) {
                    stop_tracking();
                    load_time_tracker_data();
                    save_config(false);
                }
            }
        }

        ui_box(UI_BOX_INVISIBLE_BG, "row_group2") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "filter_expression") {
                ui_tag("row");
                ui_entry(str("entry"), view->filter_buf, -1, str("Filter expression..."));
                UiBox *info_button = ui_button_info_popup(str("info_button"), true, str("data/docs/filters.txt"), true);
                info_button->next_style.size.width.strictness = 1;
            }

            ui_box(0, "since_date") {
                ui_tag("row");
                ui_label(0, "label", str("Since"));
                ui_hspacer();
                UiBox *entry = ui_date_picker_entry(str("date_picker"), &view->since, (String){}, 25);
                entry->next_style.size.width.strictness = 1;
            }

            ui_box(0, "until_date") {
                ui_tag("row");
                ui_label(0, "label", str("Until"));
                ui_hspacer();
                UiBox *entry = ui_date_picker_entry(str("date_picker"), &view->until, (String){}, 25);
                entry->next_style.size.width.strictness = 1;
            }

            UiBox *b = ui_box(0, "descending") {
                ui_tag("row");
                ui_label(0, "label", str("Descending"));
                ui_hspacer();
                ui_toggle("toggle", &view->descending);
            }

            Bool tracking = !!context->tracked_task;
            Seconds dt = (os_get_time_ms() - context->tic_save_prev) / 1000;
            if ((b->start_frame == ui->frame) || (ui->event->tag != etag) || (tracking && dt >= 1)) compute_time_tracker_stats();
        }

        if (context->tracked_task) {
            ui_box(UI_BOX_INVISIBLE_BG, "row_group3") {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

                ui_box(0, "row1") {
                    ui_tag("row");
                    String time = time_to_str(tm, context->active_track_total);
                    UiBox *clock = ui_label_extra(0, "clock", time, ui->config->font_path_bold, ui->config->font_size, 0);
                    ui_style_box_vec4(clock, UI_TEXT_COLOR, ui->theme->text_color_red);
                    ui_hspacer();
                    UiBox *preview_button = ui_button(str("preview")) {
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EYE);
                        preview_button->next_style.size.width.strictness = 1;
                        Bool opened = preview_button->scratch;
                        if (opened || preview_button->signals.clicked) {
                            ui_tag("press");
                            ui_popup(str("popup"), &opened, false, preview_button) {
                                String text = markup_ast_get_text(context->tracked_task->ast, context->tracked_task->text);
                                ui_text_view(0, str("text"), text, ui->config->font_size, (SliceUiMarkupRange){});
                            }
                        }
                        preview_button->scratch = opened;
                    }
                }

                ui_box(0, "row2") {
                    ui_tag("row");
                    UiBox *stop_button = ui_button(str("stop")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Stop"));
                        if (stop_button->signals.clicked) push_command(.tag=CMD_STOP_TRACKING);
                    }
                }
            }
        }

        ui_button_group(str("linked")) {
            UiBox *close_button = ui_button(str("close")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                if (close_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *copy_button = ui_button(str("copy")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Copy"));

                if (copy_button->signals.clicked) {
                    AString astr = astr_new(tm);
                    astr_push_cstr(&astr, "{\n");
                    astr_push_fmt(&astr, "    \"today\": %lu,\n", view->today);
                    astr_push_fmt(&astr, "    \"month\": %lu,\n", view->month);
                    astr_push_fmt(&astr, "    \"year\": %lu,\n", view->year);
                    astr_push_fmt(&astr, "    \"all_time\": %lu,\n", view->all_time);
                    astr_push_cstr(&astr, "    \"custom\": {\n");
                    astr_push_fmt(&astr, "        \"since\": \"%.*s\",\n", STR(os_date_to_str(tm, view->since)));
                    astr_push_fmt(&astr, "        \"until\": \"%.*s\",\n", STR(os_date_to_str(tm, view->until)));
                    astr_push_fmt(&astr, "        \"total\": %lu,\n", view->custom);
                    astr_push_cstr(&astr, "        \"slots\": [\n");
                    array_iter (it, &view->filtered_slots, *) {
                        if (it->total == 0) continue;
                        TimeTrackerSlot *slot = array_ref(&context->tracker_slots, it->idx);
                        String text = str_escape(tm, slot->task_str);
                        astr_push_fmt(&astr, "            { \"total\": %lu, \"str\": %.*s  },\n",  it->total, STR(text));
                    }
                    if (str_ends_with(astr_to_str(&astr), str(",\n"))) { // Get rid of last comma.
                        astr.count -= 2;
                        astr_push_byte(&astr, '\n');
                    }
                    astr_push_cstr(&astr, "        ]\n");
                    astr_push_cstr(&astr, "    }\n");
                    astr_push_cstr(&astr, "}\n");
                    win_set_clipboard_text(astr_to_str(&astr));
                }

                Bool opened = copy_button->scratch;

                if (opened || copy_button->signals.clicked) {
                    ui_tag("press");
                    ui_popup(str("popup"), &opened, false, copy_button) {
                        String info = str("Search results copied to clipboard!\n"
                                          "[note]\n"
                                          "  - The time unit used is `seconds`.\n"
                                          "  - Double quotes inside the task text are escaped with a `\\` char.\n"
                                          "  - Newlines inside the task text appear as `\\n`.");
                        ui_markup_view(str("info"), info, 0);
                    }
                }

                copy_button->scratch = opened;
            }
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        ui_box(0, "content") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_f32(UI_SPACING, ui->theme->spacing);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 0});

            build_heatmap();

            { // Build totals table:
                String ftoday  = time_to_str(tm, view->today*1000);
                String fmonth  = time_to_str(tm, view->month*1000);
                String fyear   = time_to_str(tm, view->year*1000);
                String fall    = time_to_str(tm, view->all_time*1000);
                String fcustom = time_to_str(tm, view->custom*1000);

                AString markup = astr_new(tm);
                astr_push_fmt(&markup, "|Today\n|%.*s\n|-\n", STR(ftoday));
                astr_push_fmt(&markup, "|This month\n|%.*s\n|-\n", STR(fmonth));
                astr_push_fmt(&markup, "|This year\n|%.*s\n|-\n", STR(fyear));
                astr_push_fmt(&markup, "|All time\n|%.*s\n|-\n", STR(fall));
                astr_push_fmt(&markup, "|Custom range\n|%.*s\n|-\n", STR(fcustom));
                ui_markup_view(str("totals"), astr_to_str(&markup), 0);
            }

            // Build per slot infos:
            array_iter (it, &view->filtered_slots, *) {
                if (ARRAY_IDX == view->show_more_idx) break;
                if (it->total == 0) continue;

                TimeTrackerSlot *slot = array_ref(&context->tracker_slots, it->idx);

                ui_box_fmt(0, "slot%lu", ARRAY_IDX) {
                    ui_tag("card");
                    ui_style_vec2(UI_PADDING, vec2(ui->theme->border_width.x, ui->theme->border_width.x));

                    ui_box(0, "header") {
                        ui_style_vec4(UI_BG_COLOR, get_color_for_time(it->total));
                        ui_label(0, "title", time_to_str(tm, it->total*1000));
                        ui_hspacer();
                    }

                    ui_box(0, "body") {
                        ui_text_view(0, str("text"), slot->task_str, ui->config->font_size, (SliceUiMarkupRange){});
                    }
                }
            }

            app_show_more_button(str("show_more"), &view->show_more_idx, view->filtered_slots.count);
        }
    }
}

static Void build_view_main_compact () {
    Auto view = &context->view.main;

    ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);

    ui_box(0, "cards") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing*3);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 1});

        array_iter (column, &view->columns) {
            ui_box_fmt(0, "row%lu", ARRAY_IDX) {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_f32(UI_SPACING, ui->theme->spacing);

                if (column->with_header) {
                    UiBox *header = ui_box(UI_BOX_REACTIVE|UI_BOX_CAN_FOCUS, "header") {
                        ui_tag("card");

                        ui_style_rule(".card") {
                            ui_style_u32(UI_AXIS, UI_AXIS_HORIZONTAL);
                            ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                            ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0));
                            ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, 0));
                        }

                        ui_label_extra(0, "filter", column->filter_text, ui->config->font_path_bold, ui->config->font_size*1.25, false);
                        ui_hspacer();

                        if (ui_within_box(header->rect, ui->mouse) || (ui->focused && ui_is_descendant(header, ui->focused))) {
                            ui_box(0, "autohide_icons") {
                                UiBox *search_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "search") {
                                    ui_tag("button");
                                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
                                    search_button->next_style.size.width.strictness = 1;
                                    if (search_button->signals.clicked) {
                                        AString filter = astr_new(context->view_mem);
                                        array_iter (c, &view->columns) {
                                            if (c == column) break;
                                            astr_push_fmt(&filter, "!(%.*s) & ", STR(c->filter_text));
                                        }
                                        astr_push_fmt(&filter, "(%.*s)", STR(column->filter_text));
                                        push_command(.tag=CMD_VIEW_SEARCH, .str=astr_to_str(&filter));
                                    }
                                }
                            }
                        }
                    }
                }

                array_iter (task, &column->tasks) {
                    if (ARRAY_IDX == column->show_more_idx) break;
                    Bool deleted = false;
                    build_task(task, true, &deleted);
                    if (deleted) push_command(.tag=CMD_VIEW_MAIN); // To recompute the cached columns.
                }

                app_show_more_button(str("show_more"), &column->show_more_idx, column->tasks.count);
            }
        }
    }
}

static Void build_view_main_sparse () {
    Auto view = &context->view.main;

    if (! view->has_filters) ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);

    ui_box(0, "cards") {
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        array_iter (column, &view->columns) {
            ui_box_fmt(0, "column%lu", ARRAY_IDX) {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_f32(UI_SPACING, ui->theme->spacing);

                if (column->with_header) {
                    UiBox *header = ui_box(UI_BOX_REACTIVE|UI_BOX_CAN_FOCUS, "header") {
                        ui_tag("card");
                        ui_style_rule(".card") ui_style_u32(UI_AXIS, UI_AXIS_HORIZONTAL);
                        ui_style_vec2(UI_PADDING, ui->theme->padding);

                        ui_label_extra(0, "filter", column->filter_text, ui->config->font_path_bold, ui->config->font_size, false);
                        ui_hspacer();

                        if (ui_within_box(header->rect, ui->mouse) || (ui->focused && ui_is_descendant(header, ui->focused))) {
                            ui_box(0, "autohide_icons") {
                                UiBox *search_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "search") {
                                    ui_tag("button");
                                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
                                    search_button->next_style.size.width.strictness = 1;
                                    if (search_button->signals.clicked) {
                                        AString filter = astr_new(context->view_mem);
                                        array_iter (c, &view->columns) {
                                            if (c == column) break;
                                            astr_push_fmt(&filter, "!(%.*s) & ", STR(c->filter_text));
                                        }
                                        astr_push_fmt(&filter, "(%.*s)", STR(column->filter_text));
                                        push_command(.tag=CMD_VIEW_SEARCH, .str=astr_to_str(&filter));
                                    }
                                }
                            }
                        }
                    }
                }

                array_iter (task, &column->tasks) {
                    if (ARRAY_IDX == column->show_more_idx) break;
                    Bool deleted = false;
                    build_task(task, false, &deleted);
                    if (deleted) push_command(.tag=CMD_VIEW_MAIN); // To recompute the cached columns.
                }

                app_show_more_button(str("show_more"), &column->show_more_idx, column->tasks.count);
            }
        }
    }
}

static Void build_view_main () {
    tmem_new(tm);

    ui_box(0, "navbox") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        ui_style_rule(".button")       { ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0)); }
        ui_style_rule(".button:first") { ui_style_vec4(UI_RADIUS, vec4(ui->theme->radius.x, ui->theme->radius.x, 0, 0)); }
        ui_style_rule(".button:last")  { ui_style_vec4(UI_RADIUS, vec4(0, 0, ui->theme->radius.x, ui->theme->radius.x)); }

        ui_box(0, "group0") {
            ui_style_rule(".button.press *") { ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue); }
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *sparse_button = ui_button(str("sparse")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_KANBAN);
                if (context->main_view_type == MAIN_VIEW_SPARSE) ui_tag("press");
                if (sparse_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Sparse view")); }
                if (sparse_button->signals.clicked) { context->main_view_type = MAIN_VIEW_SPARSE; save_config(false); }
            }

            UiBox *compact_button = ui_button(str("compact")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EXAM);
                if (context->main_view_type == MAIN_VIEW_COMPACT) ui_tag("press");
                if (compact_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Compact view")); }
                if (compact_button->signals.clicked) { context->main_view_type = MAIN_VIEW_COMPACT; save_config(false); }
            }
        }

        ui_box(0, "group1") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *add_button = ui_button(str("add")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);
                if (add_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR);
                if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add task")); }
            }

            UiBox *search_button = ui_button(str("search")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
                if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH);
                if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search tasks")); }
            }

            UiBox *deck_button = ui_button(str("deck")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_FOLDER);
                if (deck_button->signals.clicked) push_command(.tag=CMD_VIEW_DECK_BROWSER);
                if (deck_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Browse decks")); }
            }

            UiBox *sort_button = ui_button(str("sort")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SORT_ASC);
                if (sort_button->signals.clicked) push_command(.tag=CMD_VIEW_SORT);
                if (sort_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Sort tasks")); }
            }

            UiBox *tracker_button = ui_button(str("tracker")) {
                UiBox *icon = ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TIME_TRACKER);
                if (context->tracked_task) ui_style_box_vec4(icon, UI_TEXT_COLOR, ui->theme->text_color_red);
                if (tracker_button->signals.clicked) push_command(.tag=CMD_VIEW_TIME_TRACKER);
                if (tracker_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Time tracker")); }
            }

            UiBox *file_button = ui_button(str("file")) {
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_FILE);
                String todo_filepath = {};
                Deck *deck = get_active_deck();
                if (deck) todo_filepath = buf_get_str(deck->path, tm);
                Bool have_file = fs_is_file(todo_filepath);
                if (file_button->signals.clicked && have_file) win_open_file_url(todo_filepath);
                if (file_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Open todo file")); }
            }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        switch (context->main_view_type) {
        case MAIN_VIEW_SPARSE: build_view_main_sparse(); break;
        case MAIN_VIEW_COMPACT: build_view_main_compact(); break;
        }
    }
}

Void todo_view_init (UiViewInstance *) {
}

Void todo_view_free (UiViewInstance *) {
}

UiIcon todo_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_TODO;
}

String todo_view_get_title (UiViewInstance *, Bool visible) {
    return str("Todo");
}

static Void destroy_current_view () {
    switch (context->view.tag) {
    case VIEW_MAIN: break;
    case VIEW_EDITOR: break;
    case VIEW_SORT: break;
    case VIEW_SEARCH: break;
    case VIEW_DECK_BROWSER: break;
    case VIEW_TIME_TRACKER: break;
    }

    arena_pop_all(cast(Arena*, context->view_mem));
    context->view = (View){};
}

static Void execute_commands () {
    tmem_new(tm);

    array_iter (cmd, &context->commands, *) {
        switch (cmd->tag) {
        case CMD_SAVE_CONFIG: {
            save_config(cmd->save_deck);
        } break;

        case CMD_ADD_TASKS: {
            add_tasks(cmd->str);
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_DEL_TASK: {
            context->config_mem_fragmentation++;
            if (is_task_tracked(cmd->task)) stop_tracking();
            array_find_remove(&context->tasks, IT == cmd->task);
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_NEW_TASK_FLAGS: {
            cmd->task->config->flags = cmd->flags;
            task_serialize(cmd->task);
            if (is_task_tracked(cmd->task)) stop_tracking();
            if (! cmd->skip_config_save) {
                sort_tasks();
                save_config(true);
            }
        } break;

        case CMD_TOGGLE_TASK_DONE: {
            Task *task = cmd->task;
            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE) {
                task->config->flags &= ~MARKUP_AST_META_CONFIG_HAS_DONE;
                task->config->flags &= ~MARKUP_AST_META_CONFIG_HAS_COMPLETED;
                context->config_mem_fragmentation++; // In case we leak config->completed.
            } else {
                task->config->flags |= MARKUP_AST_META_CONFIG_HAS_DONE;
                task->config->flags |= MARKUP_AST_META_CONFIG_HAS_COMPLETED;
                task->config->completed = os_date_to_str(context->config_mem, os_get_date());
            }
            if (is_task_tracked(task)) stop_tracking();
            task_serialize(task);
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_ADD_TAGS: {
            ArrayString tags;
            array_init(&tags, tm);
            str_split(cmd->str, str(", "), false, false, &tags);

            array_iter (tag, &tags) {
                MarkupAst *n = markup_filter_parse(tm, tag);
                if (n->tag != MARKUP_AST_FILTER_TAG) array_remove(&tags, ARRAY_IDX--);
            }

            if (tags.count) {
                if (! (cmd->task->config->flags & MARKUP_AST_META_CONFIG_HAS_TAGS)) {
                    cmd->task->config->flags |= MARKUP_AST_META_CONFIG_HAS_TAGS;
                    map_init(&cmd->task->config->tags, context->config_mem);
                }

                array_iter (tag, &tags) map_add(&cmd->task->config->tags, tag, 0);
                task_serialize(cmd->task);

                if (is_task_tracked(cmd->task)) stop_tracking();
                if (! cmd->skip_config_save) {
                    sort_tasks();
                    save_active_deck();
                }
            }
        } break;

        case CMD_DELETE_TAGS: {
            ArrayString tags;
            array_init(&tags, tm);
            str_split(cmd->str, str(", "), false, false, &tags);

            if (tags.count) {
                if (cmd->task->config->flags & MARKUP_AST_META_CONFIG_HAS_TAGS) {
                    array_iter (tag, &tags) map_remove(&cmd->task->config->tags, tag);
                    task_serialize(cmd->task);

                    if (is_task_tracked(cmd->task)) stop_tracking();
                    if (! cmd->skip_config_save) {
                        sort_tasks();
                        save_active_deck();
                    }
                }
            }
        } break;

        case CMD_NEW_DECK: {
            Deck *deck = mem_new(context->config_mem, Deck);
            deck->active = false;
            deck->path = buf_new(context->config_mem, str(""));
            deck->filters = buf_new(context->config_mem, str(""));
            array_push(&context->decks, deck);
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_DEL_DECK: {
            array_find_remove(&context->decks, IT == cmd->deck);
            context->config_mem_fragmentation++;
            load_active_deck();
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_ACTIVATE_DECK: {
            array_iter (deck, &context->decks) deck->active = (deck == cmd->deck);
            load_active_deck();
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_START_TRACKING: {
            start_tracking(cmd->task);
        } break;

        case CMD_STOP_TRACKING: {
            stop_tracking();
        } break;

        case CMD_NEW_TRACKER_SLOT: {
            assign_new_tracker_slot(cmd->task);
        } break;

        case CMD_UPDATE_TRACKER_SLOT: {
            TimeTrackerSlot *slot = get_tracker_slot_for_task(cmd->task);
            slot->task_str = markup_ast_get_text(cmd->task->ast, cmd->task->text);
            slot->task_ast = cmd->task->ast;
            context->config_mem_fragmentation++;
            save_time_tracker_data();
        } break;

        case CMD_SORT_TASKS: {
            sort_tasks();
        } break;

        case CMD_CHANGE_SORT: {
            if (cmd->index2 == ARRAY_NIL_IDX) {
                Sort *s = array_ref(&context->sorts, cmd->index);
                s->ascending = !s->ascending;
            } else {
                array_swap(&context->sorts, cmd->index, cmd->index2);
            }
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_VIEW_MAIN: {
            destroy_current_view();
            context->view.tag = VIEW_MAIN;

            Auto view = &context->view.main;

            array_init(&view->columns, context->view_mem);
            array_init(&view->filters, context->view_mem);

            view->has_filters = false;
            Deck *deck = get_active_deck();

            if (deck) {
                String s = buf_get_str(deck->filters, context->view_mem);
                str_split(s, str(","), false, false, &view->filters);
                if (s.count) {
                    view->has_filters = true;
                } else {
                    array_push(&view->filters, str("* & !hide"));
                }
            }

            array_iter (filter, &view->filters) {
                MarkupAst *filter_node = markup_filter_parse(context->view_mem, filter);
                KanbanColumn *col = mem_new(context->view_mem, KanbanColumn);
                col->filter_text = filter;
                col->filter_node = filter_node;
                col->with_header = view->has_filters;
                array_init(&col->tasks, context->view_mem);
                array_push(&view->columns, col);
            }

            array_iter (task, &context->tasks) {
                array_iter (column, &view->columns) {
                    if (task_passes_filter(task, column->filter_node)) {
                        array_push(&column->tasks, task);
                        break;
                    }
                }
            }
        } break;

        case CMD_VIEW_SORT: {
            destroy_current_view();
            context->view.tag = VIEW_SORT;
        } break;

        case CMD_VIEW_DECK_BROWSER: {
            destroy_current_view();
            context->view.tag = VIEW_DECK_BROWSER;
            context->view.deck_browser.buf = buf_new(context->view_mem, str(""));
            array_init(&context->view.deck_browser.searched, context->view_mem);
        } break;

        case CMD_VIEW_TIME_TRACKER: {
            destroy_current_view();
            context->view.tag = VIEW_TIME_TRACKER;
            Task *task = cmd->task;

            if (task) {
                TimeTrackerSlot *slot = get_tracker_slot_for_task(task);
                if (! slot) assign_new_tracker_slot(task);
                context->view.time_tracker.filter_buf = buf_new(context->view_mem, astr_fmt(tm, "track %lu", task->config->track));
            } else {
                context->view.time_tracker.filter_buf = buf_new(context->view_mem, str("*"));
            }

            context->view.time_tracker.heatmap_date = os_get_date();
            context->view.time_tracker.descending = true;
            array_init(&context->view.time_tracker.filtered_slots, context->view_mem);
            map_init(&context->view.time_tracker.heatmap_data, context->view_mem);
        } break;

        case CMD_VIEW_SEARCH: {
            String needle = cmd->str.data ? str_copy(tm, cmd->str) : str("*");
            destroy_current_view();
            context->view.tag = VIEW_SEARCH;
            context->view.search.buf = buf_new(context->view_mem, needle);
            context->view.search.tags_add_buf = buf_new(context->view_mem, str(""));
            context->view.search.tags_del_buf = buf_new(context->view_mem, str(""));
            array_init(&context->view.search.searched, context->view_mem);
        } break;

        case CMD_VIEW_EDITOR: {
            destroy_current_view();
            context->view.tag = VIEW_EDITOR;
            context->view.editor.task_to_edit = cmd->task;
            Task *task = cmd->task;
            String init_text;
            if (task) {
                if (is_task_tracked(task)) stop_tracking();
                init_text = markup_ast_get_text(task->ast, task->text);
            } else {
                Date d = os_get_date();
                init_text = astr_fmt(tm, "[created: %u-%02u-%02u] ", d.year, d.month, d.day);
            }
            context->view.editor.buf = buf_new(context->view_mem, init_text);
            if (task) context->view.editor.task = *task;
        } break;
        }
    }

    context->commands.count = 0;
}

static Void tic () {
    TimeTrackerSlot *slot = array_ref(&context->tracker_slots, context->tracked_task->config->track);

    assert_dbg(context->tracked_task->config->flags & MARKUP_AST_META_CONFIG_HAS_TRACK);

    Date date = os_get_date();
    Millisec now = os_get_time_ms();
    Millisec dt = now - context->tic_checkpoint;

    U64 time_idx = ARRAY_NIL_IDX;
    { // Find time_idx:
        array_iter_back (it, &slot->time, *) {
            if (! os_date_cmp(date, it->date)) {
                time_idx = ARRAY_IDX;
                break;
            }
        }

        if (time_idx == ARRAY_NIL_IDX) {
            time_idx = slot->time.count;
            array_push_lit(&slot->time, .date=date);
            context->active_track_total = 0;
        }
    }

    context->active_track_total = sat_add64(context->active_track_total, dt);
    Auto time_slot = array_ref(&slot->time, time_idx);
    time_slot->seconds = context->active_track_total / 1000;

    if ((now - context->tic_save_prev) / 1000 > 1) {
        context->tic_save_prev = now;
        save_time_tracker_data();
    }

    context->tic_checkpoint = now;
}

Void todo_view_build (UiViewInstance *, Bool visible) {
    if (context->decks.count == 0 && context->view.tag != VIEW_DECK_BROWSER) {
        push_command(.tag=CMD_VIEW_DECK_BROWSER);
    }

    execute_commands();
    if (context->config_mem_fragmentation > 100) {
        load_config();
        push_command(.tag=CMD_VIEW_MAIN);
        execute_commands();
    }
    if (context->tick_id) tic();

    switch (context->view.tag) {
    case VIEW_MAIN: build_view_main(); break;
    case VIEW_EDITOR: build_view_editor(); break;
    case VIEW_SEARCH: build_view_search(); break;
    case VIEW_SORT: build_view_sort(); break;
    case VIEW_DECK_BROWSER: build_view_deck_browser(); break;
    case VIEW_TIME_TRACKER: build_view_time_tracker(); break;
    }
}

Void todo_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 2;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/todo.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_MAIN);
}
