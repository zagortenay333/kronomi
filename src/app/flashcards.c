#include "app/app.h"
#include "app/flashcards.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"

istruct (Card) {
    U64 bucket;
    String question;
    String answer;
};

istruct (Deck) {
    Bool active;
    String path;
};

array_typedef(Card*, Card);
array_typedef(Deck*, Deck);

ienum (ViewTag, U8) {
    VIEW_MAIN,
    VIEW_EXAM,
    VIEW_CARD_EDITOR,
    VIEW_DECK_BROWSER,
    VIEW_SEARCH_CARDS,
};

istruct (View) {
    ViewTag tag;

    union {
        struct {
            U64 show_more_idx;
        } main;

        struct {
            Buf *question_buf;
            Buf *answer_buf;
            U64 bucket;
            U64 question_cursor;
            U64 answer_cursor;
            Card *card_to_edit;
        } card_editor;

        struct {
            Bool delete_searched;
            Bool fuzzy_search;
            I64 search_in_bucket;
            I64 move_to_bucket;
            Buf *search_buf;
            U64 search_buf_version;
            U64 show_more_idx;
            Array(SearchResult) searched_cards;
        } search_cards;

        struct {
            Buf *import_buf;
            Buf *export_buf;
            Buf *search_buf;
            U64 search_buf_version;
            Bool delete_searched;
            Array(SearchResult) searched_decks;
        } deck_browser;

        struct {
            Bool done;
            Card *shown_card;
            ArrayCard cards;
        } exam;
    };
};

ienum (CommandTag, U8) {
    CMD_SAVE_CONFIG,
    CMD_ADD_CARD,
    CMD_DEL_CARD,
    CMD_EDIT_CARD,
    CMD_MOVE_CARD,
    CMD_ADD_DECK,
    CMD_DEL_DECK,
    CMD_ACTIVATE_DECK,
    CMD_VIEW_MAIN,
    CMD_VIEW_EXAM,
    CMD_VIEW_EDIT_CARD,
    CMD_VIEW_DECK_BROWSER,
    CMD_VIEW_SEARCH_CARDS,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    U64 index;
    Deck *deck;
    Card *card;
    Card *card2;
    String text;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    ArrayDeck decks;
    ArrayCard cards;
    Deck *active_deck;
    U64 session;
    F32 editor_width;
    F32 editor_height;
};

static Context *context;

static Void load_deck (Deck *deck, U64 *out_session, ArrayCard *out_cards);

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

static Void export_deck (Deck *deck, String export_dir_path) {
    tmem_new(tm);

    String name = str_suffix_from_last(deck->path, '/');
    name = str_prefix_to_last(name, '.');
    String export_path = astr_fmt(tm, "%.*s/%.*s.csv", STR(export_dir_path), STR(name));

    U64 session;
    ArrayCard cards;
    array_init(&cards, tm);
    load_deck(deck, &session, &cards);

    AString csv = astr_new(tm);
    array_iter (card, &cards) {
        astr_push_byte(&csv, '"');
        astr_push_str(&csv, str_replace_all(tm, card->question, str("\""), str("\"\"")));
        astr_push_cstr(&csv, "\",\"");
        astr_push_str(&csv, str_replace_all(tm, card->answer, str("\""), str("\"\"")));
        astr_push_cstr(&csv, "\"\n");
    }

    fs_write_entire_file(export_path, astr_to_str(&csv));
}

static Void save_active_deck () {
    if (context->decks.count == 0) return;

    tmem_new(tm);

    AString astr = astr_new(tm);
    astr_push_fmt(&astr, "session = %lu\ncards = [\n", context->session);
    array_iter (card, &context->cards) {
        String question = str_escape(tm, card->question);
        String answer = str_escape(tm, card->answer);
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        bucket = %lu\n", card->bucket);
        astr_push_fmt(&astr,  "        question = %.*s\n", STR(question));
        astr_push_fmt(&astr,  "        answer = %.*s\n", STR(answer));
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    fs_ensure_file(context->active_deck->path, (String){});
    fs_write_entire_file(context->active_deck->path, astr_to_str(&astr));
}

static Void load_deck (Deck *deck, U64 *out_session, ArrayCard *out_cards) {
    Bool file_exists = fs_file_exists(deck->path);
    fs_ensure_file(deck->path, (String){});
    if (! file_exists) fs_write_entire_file(deck->path, str("session = 1\ncards = []"));

    tmem_new(tm);
    Config *cfg = config_parse(tm, deck->path);
    *out_session = config_get_u64(cfg, cfg->root, "session");
    ConfigAst *cards = config_get_array(cfg, cfg->root, "cards");
    array_iter (card_ast, &cards->children) {
        Card *card = mem_new(context->config_mem, Card);
        card->bucket   = config_get_u64(cfg, card_ast, "bucket");
        card->question = config_get_string(cfg, card_ast, "question", context->config_mem);
        card->answer   = config_get_string(cfg, card_ast, "answer", context->config_mem);
        array_push(out_cards, card);
    }
}

static Void load_active_deck () {
    context->config_mem_fragmentation += context->cards.count;
    context->cards.count = 0;
    if (context->decks.count == 0) return;
    Deck *deck = array_find_get(&context->decks, IT->active);
    if (! deck) deck = array_get(&context->decks, 0);
    context->active_deck = deck;
    load_deck(deck, &context->session, &context->cards);
}

static Void save_config () {
    save_active_deck();

    tmem_new(tm);

    AString astr = astr_new(tm);
    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);
    astr_push_cstr(&astr, "decks = [\n");
    array_iter (deck, &context->decks) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        active = %s\n", deck->active ? "true" : "false");
        astr_push_fmt(&astr,  "        path = \"%.*s\"\n", STR(deck->path));
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
    array_init(&context->decks, context->config_mem);
    array_init(&context->cards, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/flashcards.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width  = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");

    ConfigAst *decks = config_get_array(cfg, cfg->root, "decks");
    array_iter (deck_ast, &decks->children) {
        Deck *deck = mem_new(context->config_mem, Deck);
        deck->active = config_get_bool(cfg, deck_ast, "active");
        deck->path   = config_get_string(cfg, deck_ast, "path", context->config_mem);
        array_push(&context->decks, deck);
    }

    load_active_deck();
}

static Void build_card (Card *card, Bool *out_card_deleted, Bool reactive) {
    U64 n = array_get_last(&ui->box_stack)->children.count;
    UiBox *card_box = ui_box_fmt(reactive ? UI_BOX_CAN_FOCUS : 0, "card%lu", n) {
        ui_tag("card");

        ui_box(0, "header") {
            tmem_new(tm);
            ui_label(0, "title", astr_fmt(tm, "Bucket %lu", card->bucket));
            ui_hspacer();

            if (reactive && (ui_within_box(card_box->rect, ui->mouse) || (ui->focused && ui_is_descendant(card_box, ui->focused)))) {
                ui_box(0, "autohide_icons") {
                    UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) push_command(.tag=CMD_VIEW_EDIT_CARD, .card=card);
                    }

                    UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                        if (delete_button->signals.clicked) {
                            push_command(.tag=CMD_DEL_CARD, .card=card);
                            if (out_card_deleted) *out_card_deleted = true;
                        }
                    }
                }
            }
        }

        ui_box(0, "body") {
            ui_markup_view(str("question"), card->question, 0);
            ui_drawer(str("answer"), str("Answer")) ui_markup_view(str("answer"), card->answer, 0);
        }
    }
}

static Void build_view_search_cards () {
    Auto view = &context->view.search_cards;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_box(UI_BOX_INVISIBLE_BG, "row_group1") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "entry_row") {
                ui_tag("row");
                ui_entry(str("entry"), view->search_buf, -1, str("Search cards..."));
            }

            ui_box(0, "fuzzy_search") {
                ui_tag("row");
                ui_label(0, "title", str("Fuzzy search"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->fuzzy_search);
            }

            ui_box(0, "search_config") {
                ui_tag("row");
                ui_label(0, "title", str("Search in bucket (-1 for all buckets)"));
                ui_hspacer();
                UiBox *picker = ui_int_picker(str("picker"), &view->search_in_bucket, -1, 5, 2);
                picker->next_style.size.width.strictness = 1;
            }
        }

        ui_box(UI_BOX_INVISIBLE_BG, "row_group2") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "move_to_bucket") {
                ui_tag("row");
                ui_label(0, "title", str("Move to bucket (-1 for no move)"));
                ui_hspacer();
                UiBox *picker = ui_int_picker(str("picker"), &view->move_to_bucket, -1, 5, 2);
                picker->next_style.size.width.strictness = 1;
            }

            ui_box(0, "delete") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched cards."));
                ui_hspacer();
                ui_checkbox("checkbox", &view->delete_searched);
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *apply_button = ui_button(str("apply")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Apply"));
                if (apply_button->signals.clicked) {
                    if (view->delete_searched) {
                        array_iter (it, &view->searched_cards) push_command(.tag=CMD_DEL_CARD, .card=array_get(&context->cards, it.idx), .skip_config_save=true);
                        view->searched_cards.count = 0;
                    } else if (view->move_to_bucket != -1) {
                        array_iter (it, &view->searched_cards) push_command(.tag=CMD_MOVE_CARD, .card=array_get(&context->cards, it.idx), .index=view->move_to_bucket, .skip_config_save=!ARRAY_ITER_DONE);
                    }

                    push_command(.tag=CMD_SAVE_CONFIG);
                }
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

        view->searched_cards.count = 0;
        view->search_buf_version = buf_get_version(view->search_buf);

        tmem_new(tm);
        String needle = buf_get_str(view->search_buf, tm);

        array_iter (card, &context->cards) {
            if ((view->search_in_bucket != -1) && (card->bucket != cast(U64, view->search_in_bucket))) {
                continue;
            } else if (view->fuzzy_search) {
                I64 q = str_fuzzy_search(needle, card->question, 0);
                I64 a = str_fuzzy_search(needle, card->answer, 0);
                I64 score = max(q, a);
                if (score != INT64_MIN) array_push_lit(&view->searched_cards, .score=score, .idx=ARRAY_IDX);
            } else {
                U64 q = str_search(needle, card->question);
                U64 a = str_search(needle, card->answer);
                if (q != ARRAY_NIL_IDX || a != ARRAY_NIL_IDX) array_push_lit(&view->searched_cards, .score=0, .idx=ARRAY_IDX);
            }
        }

        if (view->fuzzy_search) array_sort_cmp(&view->searched_cards, app_cmp_search_results);

        array_iter (it, &view->searched_cards) {
            if (ARRAY_IDX == view->show_more_idx) break;
            build_card(array_get(&context->cards, it.idx), 0, true);
        }

        app_show_more_button(str("show_more"), &view->show_more_idx, view->searched_cards.count);
    }
}

static Void build_view_deck_browser () {
    Auto view = &context->view.deck_browser;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("search_entry"), view->search_buf, -1, str("Search decks..."));

        ui_box(UI_BOX_INVISIBLE_BG, "row_group1") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "import") {
                ui_tag("row");
                ui_file_picker_entry(str("picker"), view->import_buf, str("Import deck files..."), -1, true, false, (String){});
                ui_button_info_popup(str("help_button"), true, str("data/docs/flashcards_deck.txt"), true);
            }

            ui_box(0, "export") {
                ui_tag("row");
                ui_file_picker_entry(str("picker"), view->export_buf, str("Export decks to..."), -1, false, true, (String){});
                ui_button_info_popup(str("help_button"), true, str("data/docs/flashcards_csv.txt"), true);
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *apply_button = ui_button(str("apply")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Apply"));
                if (apply_button->signals.clicked) {
                    tmem_new(tm);

                    buf_iter_lines_delim (it, view->import_buf, tm, '|') {
                        String line = str_copy(context->config_mem, it->text);
                        push_command(.tag=CMD_ADD_DECK, .text=line, .skip_config_save=true);
                    }

                    String export_dir = buf_get_str(view->export_buf, tm);
                    if (export_dir.count) array_iter (it, &view->searched_decks) export_deck(array_get(&context->decks, it.idx), export_dir);

                    push_command(.tag=CMD_SAVE_CONFIG);
                    push_command(.tag=CMD_VIEW_MAIN);
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

        if (view->search_buf_version != buf_get_version(view->search_buf)) {
            view->searched_decks.count = 0;
            view->search_buf_version = buf_get_version(view->search_buf);

            tmem_new(tm);
            String needle = buf_get_str(view->search_buf, tm);

            array_iter (deck, &context->decks) {
                I64 score = str_fuzzy_search(needle, deck->path, 0);
                if (score != INT64_MIN) array_push_lit(&view->searched_decks, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched_decks, app_cmp_search_results);
        }

        Bool deck_deleted = false;

        array_iter (it, &view->searched_decks) {
            Deck *deck = array_get(&context->decks, it.idx);

            UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "deck%lu", it.idx) {
                ui_tag("card");

                ui_box(0, "header") {
                    UiBox *checkbox = ui_checkbox("checkbox", &deck->active);
                    if (checkbox->signals.clicked && deck->active) push_command(.tag=CMD_ACTIVATE_DECK, .deck=deck);

                    ui_hspacer();

                    if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                        ui_box(0, "autohide_icons") {
                            UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                                if (edit_button->signals.clicked) win_open_file_url(deck->path);
                            }

                            UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                                if (delete_button->signals.clicked) {
                                    push_command(.tag=CMD_DEL_DECK, .deck=deck);
                                    deck_deleted = true;
                                }
                            }
                        }
                    }
                }

                ui_box(0, "body") ui_markup_view(str("path"), deck->path, 0);
            }
        }

        if (deck_deleted) view->search_buf_version--; // To refresh the searched_decks array.
    }
}

static Void exam_view_collect_cards () {
    Auto view = &context->view.exam;

    view->cards.count = 0;

    array_iter (card, &context->cards) {
        U64 days = 1lu << min(5lu, card->bucket);
        if ((context->session % days) == 0) array_push(&view->cards, card);
    }

    array_reverse(&view->cards);
}

static Void exam_view_get_next_card () {
    Auto view = &context->view.exam;

    if (view->cards.count) {
        view->done = false;
        view->shown_card = array_pop(&view->cards);
    } else {
        view->done = true;
    }
}

static Void build_view_exam () {
    Auto view = &context->view.exam;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        ui_box(UI_BOX_INVISIBLE_BG, "rowgroup1") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

            ui_box(0, "session") {
                ui_tag("row");
                ui_label(0, "label", str("Session"));
                ui_hspacer();
                I64 session = context->session;
                UiBox *picker = ui_int_picker(str("picker"), &session, 1, 32, 2);
                picker->next_style.size.width.strictness = 1;
                if (cast(U64, session) != context->session) {
                    context->session = session;
                    exam_view_collect_cards();
                    exam_view_get_next_card();
                    save_config();
                }
            }

            ui_box(0, "remaining_cards") {
                ui_tag("row");
                ui_label(0, "label", str("Remaining cards"));
                ui_hspacer();
                tmem_new(tm);
                String label = astr_fmt(tm, "%lu%c", view->cards.count, 0);
                ui_label(0, "remaining", label);
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(0, "label", str("Cancel"));
                if (cancel_button->signals.clicked) {
                    context->session++;
                    if (context->session > 32) context->session = 32;
                    save_config();
                    push_command(.tag=CMD_VIEW_MAIN);
                }
            }

            if (! view->done) {
                UiBox *correct_button = ui_button(str("correct")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    UiBox *label = ui_label(0, "label", str("Correct"));
                    ui_style_box_vec4(label, UI_TEXT_COLOR, ui->theme->text_color_green);
                    if (correct_button->signals.clicked) {
                        Card *card = view->shown_card;
                        card->bucket++;
                        if (card->bucket > 5) card->bucket = 5;
                        exam_view_get_next_card();
                    }
                }

                UiBox *wrong_button = ui_button(str("wrong")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    UiBox *label = ui_label(0, "label", str("Wrong"));
                    ui_style_box_vec4(label, UI_TEXT_COLOR, ui->theme->text_color_red);
                    if (wrong_button->signals.clicked) {
                        Card *card = view->shown_card;
                        card->bucket = 0;
                        exam_view_get_next_card();
                    }
                }
            }
        }
    }

    ui_scroll_box(str("scrollbox"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        if (! view->done) build_card(view->shown_card, 0, false);
    }
}

static Void build_view_card_editor () {
    Auto view = &context->view.card_editor;

    UiBox *question_editor = 0;
    UiBox *answer_editor = 0;
    UiBox *question_markup = 0;
    UiBox *answer_markup = 0;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        F32 r = ui->theme->radius.x;
        F32 w = context->editor_width;
        F32 h = context->editor_height;

        ui_box(UI_BOX_INVISIBLE_BG, "question") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *header = ui_box(0, "header") {
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_style_box_size(header, UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(0, "title", str("Question"));
            }

            if (! view->question_buf) {
                String text = view->card_to_edit ? view->card_to_edit->question : str("");
                view->question_buf = buf_new(context->view_mem, text);
            }

            question_editor = ui_ted_resizable(str("editor"), view->question_buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(question_editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(question_editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        ui_box(UI_BOX_INVISIBLE_BG, "answer") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *header = ui_box(0, "header") {
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_box_size(header, UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_label(0, "title", str("Answer"));
            }

            if (! view->answer_buf) {
                String text = view->card_to_edit ? view->card_to_edit->answer : str("");
                view->answer_buf = buf_new(context->view_mem, text);
            }

            answer_editor = ui_ted_resizable(str("editor"), view->answer_buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(answer_editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(answer_editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        if (w != context->editor_width || h != context->editor_height) save_config();

        ui_box(UI_BOX_INVISIBLE_BG, "rows") {
            UiBox *box = ui_box(0, "bucket_picker") {
                ui_tag("row");
                ui_label(0, "title", str("Bucket"));
                ui_hspacer();
                if (box->start_frame == ui->frame && view->card_to_edit) view->bucket = view->card_to_edit->bucket;
                I64 val = view->bucket;
                UiBox *picker = ui_int_picker(str("picker"), &val, 0, 5, 1);
                view->bucket = val;
                picker->next_style.size.width.strictness = 1;
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *ok_button = ui_button(str("ok")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Ok"));
                if (ok_button->signals.clicked) {
                    Card *new_card = mem_new(context->config_mem, Card);
                    new_card->bucket   = view->bucket;
                    new_card->question = buf_get_str(view->question_buf, context->config_mem);
                    new_card->answer   = buf_get_str(view->answer_buf, context->config_mem);

                    if (view->card_to_edit) {
                        push_command(.tag=CMD_EDIT_CARD, .card=view->card_to_edit, .card2=new_card);
                    } else {
                        push_command(.tag=CMD_ADD_CARD, .card=new_card);
                    }

                    push_command(.tag=CMD_VIEW_MAIN);
                }
            }
        }
    }

    UiBox *scrollbox = ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        F32 r = ui->theme->radius.x;
        F32 b = ui->theme->border_width.x;

        ui_box(UI_BOX_INVISIBLE_BG, "question") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

            ui_box(0, "header") {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_label(0, "title", str("Question"));
            }

            question_markup = ui_markup_view_buf(str("markup"), view->question_buf, true, 0, 0);
            ui_style_box_vec2(question_markup, UI_PADDING, ui->theme->padding);
            ui_style_box_vec4(question_markup, UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_box_vec4(question_markup, UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_box_vec4(question_markup, UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_box_vec4(question_markup, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_vec4(question_markup, UI_BORDER_WIDTHS, vec4(b, 0, b, b));
        }

        ui_box(UI_BOX_INVISIBLE_BG, "answer") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

            ui_box(0, "header") {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_label(0, "title", str("Answer"));
            }

            answer_markup = ui_markup_view_buf(str("markup"), view->answer_buf, true, 0, 0);
            ui_style_box_vec2(answer_markup, UI_PADDING, ui->theme->padding);
            ui_style_box_vec4(answer_markup, UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_box_vec4(answer_markup, UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_box_vec4(answer_markup, UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_box_vec4(answer_markup, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_vec4(answer_markup, UI_BORDER_WIDTHS, vec4(b, 0, b, b));
        }
    }

    if (ui->focused) {
        if (ui_is_descendant(question_editor, ui->focused)) {
            app_sync_scroll(scrollbox, question_editor, question_markup, &view->question_cursor);
        } else if (ui_is_descendant(answer_editor, ui->focused)) {
            app_sync_scroll(scrollbox, answer_editor, answer_markup, &view->answer_cursor);
        }
    }
}

static Void build_view_main () {
    Auto view = &context->view.main;

    ui_box(0, "navbox") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        ui_style_rule(".button")       { ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0)); }
        ui_style_rule(".button:first") { ui_style_vec4(UI_RADIUS, vec4(ui->theme->radius.x, ui->theme->radius.x, 0, 0)); }
        ui_style_rule(".button:last")  { ui_style_vec4(UI_RADIUS, vec4(0, 0, ui->theme->radius.x, ui->theme->radius.x)); }

        UiBox *add_button = ui_button(str("add")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);
            if (add_button->signals.clicked) push_command(.tag=CMD_VIEW_EDIT_CARD);
            if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add flashcard")); }
        }

        UiBox *search_button = ui_button(str("search")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
            if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search flashcards")); }
            if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH_CARDS);
        }

        UiBox *deck_button = ui_button(str("deck")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_FOLDER);
            if (deck_button->signals.clicked) push_command(.tag=CMD_VIEW_DECK_BROWSER);
            if (deck_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Browse decks")); }
        }

        UiBox *exam_button = ui_button(str("exam")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EXAM);
            if (exam_button->signals.clicked) push_command(.tag=CMD_VIEW_EXAM);
            if (exam_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Start exam")); }
        }

        UiBox *info_button = ui_button_info_popup(str("help"), true, str("data/docs/flashcards.txt"), true);
        if (info_button->signals.hovered) {
            ui_parent(info_button) {
                ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Info"));
            }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        array_iter (card, &context->cards) {
            if (ARRAY_IDX == view->show_more_idx) break;
            build_card(card, 0, true);
        }

        app_show_more_button(str("show_more"), &view->show_more_idx, context->cards.count);
    }
}

Void flashcards_view_init (UiViewInstance *) {
}

Void flashcards_view_free (UiViewInstance *) {
}

UiIcon flashcards_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_FLASH;
}

String flashcards_view_get_title (UiViewInstance *, Bool visible) {
    return str("Flashcards");
}

static Void destroy_current_view () {
    switch (context->view.tag) {
    case VIEW_MAIN: break;
    case VIEW_EXAM: break;
    case VIEW_CARD_EDITOR: break;
    case VIEW_DECK_BROWSER: break;
    case VIEW_SEARCH_CARDS: break;
    }

    arena_pop_all(cast(Arena*, context->view_mem));
    context->view = (View){};
}

static Void execute_commands () {
    array_iter (cmd, &context->commands) {
        switch (cmd.tag) {
        case CMD_SAVE_CONFIG: {
            save_config();
        } break;

        case CMD_ADD_CARD: {
            array_push(&context->cards, cmd.card);
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_DEL_CARD: {
            array_find_remove(&context->cards, IT == cmd.card);
            context->config_mem_fragmentation++;
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_EDIT_CARD: {
            array_find_remove(&context->cards, IT == cmd.card);
            array_push(&context->cards, cmd.card2);
            context->config_mem_fragmentation++;
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_MOVE_CARD: {
            cmd.card->bucket = cmd.index;
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_ADD_DECK: {
            Deck *deck = mem_new(context->config_mem, Deck);
            deck->path = cmd.text;
            array_push(&context->decks, deck);
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_DEL_DECK: {
            array_find_remove(&context->decks, IT == cmd.deck);
            context->config_mem_fragmentation++;
            if (cmd.deck == context->active_deck) load_active_deck();
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_ACTIVATE_DECK: {
            array_iter (deck, &context->decks) deck->active = (deck == cmd.deck);
            load_active_deck();
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_VIEW_MAIN: {
            destroy_current_view();
            context->view.tag = VIEW_MAIN;
        } break;

        case CMD_VIEW_EDIT_CARD: {
            destroy_current_view();
            context->view.tag = VIEW_CARD_EDITOR;
            context->view.card_editor.card_to_edit = cmd.card;
        } break;

        case CMD_VIEW_EXAM: {
            destroy_current_view();
            context->view.tag = VIEW_EXAM;
            array_init(&context->view.exam.cards, context->view_mem);
            exam_view_collect_cards();
            exam_view_get_next_card();
        } break;

        case CMD_VIEW_DECK_BROWSER: {
            destroy_current_view();
            context->view.tag = VIEW_DECK_BROWSER;
            array_init(&context->view.deck_browser.searched_decks, context->view_mem);
            context->view.deck_browser.search_buf_version = UINT64_MAX;
            context->view.deck_browser.search_buf = buf_new(context->view_mem, (String){});
            context->view.deck_browser.import_buf = buf_new(context->view_mem, (String){});
            context->view.deck_browser.export_buf = buf_new(context->view_mem, (String){});
        } break;

        case CMD_VIEW_SEARCH_CARDS: {
            destroy_current_view();
            context->view.tag = VIEW_SEARCH_CARDS;
            array_init(&context->view.search_cards.searched_cards, context->view_mem);
            context->view.search_cards.search_buf = buf_new(context->view_mem, (String){});
            context->view.search_cards.move_to_bucket = -1;
            context->view.search_cards.search_in_bucket = -1;
        } break;
        }
    }

    context->commands.count = 0;
}

Void flashcards_view_build (UiViewInstance *, Bool visible) {
    if (context->decks.count == 0 && context->view.tag != VIEW_DECK_BROWSER) {
        push_command(.tag=CMD_VIEW_DECK_BROWSER);
    }

    execute_commands();
    if (context->config_mem_fragmentation > 100) {
        load_config();
        push_command(.tag=CMD_VIEW_MAIN);
        execute_commands();
    }

    switch (context->view.tag) {
    case VIEW_MAIN: build_view_main(); break;
    case VIEW_EXAM: build_view_exam(); break;
    case VIEW_CARD_EDITOR: build_view_card_editor(); break;
    case VIEW_SEARCH_CARDS: build_view_search_cards(); break;
    case VIEW_DECK_BROWSER: build_view_deck_browser(); break;
    }
}

Void flashcards_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 0;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/flashcards.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_MAIN);
}
