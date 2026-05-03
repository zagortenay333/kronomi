#include "markup/markup.h"
#include "lexer/lexer.h"
#include "os/time.h"

// A delimiter is a sequence of tokens that delimit an
// inline element. For example: **.
istruct (Delimiter) {
    TokenTag t1, t2, t3;
};

istruct (Parser) {
    Mem *mem;
    String text;
    Lexer *lex;
    I64 wanted_indent;
    I64 actual_indent;
    Bool trimming_tokens;
    Bool start_of_word;
    Bool start_of_line;
    Bool inside_raw_text_region;
    SrcPos last_eaten_token;
    U64 stop_parsing_inline_at_newline;
    Set(String) body_tags;
    Array(Delimiter) stop_parsing_inline_at;
};

typedef MarkupAst *(*Parselet)      (Parser *);
typedef MarkupAst *(*InfixParselet) (Parser *, MarkupAst *lhs);

CString markup_ast_tag_to_cstr [] = {
    #define X(TAG, ...) #TAG,
        EACH_MARKUP_AST_NODE(X)
    #undef X
};

U64 markup_ast_get_node_size [] = {
    #define X(_, T, ...) sizeof(T),
        EACH_MARKUP_AST_NODE(X)
    #undef X
};

U8 markup_ast_get_node_align [] = {
    #define X(_, T, ...) alignof(T),
        EACH_MARKUP_AST_NODE(X)
    #undef X
};

MarkupAstTableCellConfig default_table_cell_config = {
    .width  = 1,
    .height = 1,
    .no_bg  = false,
};

static MarkupAst *parse_table (Parser *p);
static Parselet get_block_parselet (Parser *p);
static Parselet get_inline_parselet (Parser *p);
static Void on_token_eaten (Token *token, Void *p_);
static Void parse_blocks (Parser *p, ArrayMarkupAst *out);
static MarkupAst *parse_filter (Parser *p, U64 lhs_precedence);
static Void parse_inlines (Parser *p, ArrayMarkupAst *out, Delimiter delim);

U64 markup_ast_hash (MarkupAst *node) {
    return hash_u64(node->tag + node->pos.offset + node->pos.length);
}

Parser *parser_new (Mem *mem, String text) {
    Auto p = mem_new(mem, Parser);
    p->mem = mem;
    p->lex = lex_new(mem, false);
    p->text = text;
    p->wanted_indent = -1;
    lex_reset(p->lex, text);
    lex_set_on_token_eaten(p->lex, on_token_eaten, p);
    map_init(&p->body_tags, mem);
    array_init(&p->stop_parsing_inline_at, mem);
    return p;
}

MarkupAst *markup_ast_alloc (Mem *mem, MarkupAstTag tag) {
    MarkupAst *node = mem_alloc(mem, MarkupAst, .zeroed=true, .size=markup_ast_get_node_size[tag], .align=markup_ast_get_node_align[tag]);
    node->tag = tag;
    array_init(&node->children, mem);
    return node;
}

static MarkupAst *complete_node (Parser *p, MarkupAst *node) {
    SrcPos prev = p->last_eaten_token;
    node->pos.last_line = prev.last_line;
    node->pos.length = prev.offset + prev.length - node->pos.offset;
    return node;
}

static MarkupAst *make_node (Parser *p, MarkupAstTag tag) {
    MarkupAst *node = markup_ast_alloc(p->mem, tag);
    SrcPos pos = lex_peek(p->lex)->pos;
    node->pos.offset = pos.offset;
    node->pos.first_line = pos.first_line;
    return node;
}

static MarkupAst *make_node_lhs (Parser *p, MarkupAstTag tag, MarkupAst *lhs) {
    MarkupAst *node = markup_ast_alloc(p->mem, tag);
    node->pos.offset = lhs->pos.offset;
    node->pos.first_line = lhs->pos.first_line;
    return node;
}

String markup_ast_get_text (MarkupAst *node, String text) {
    return (String){
        .data  = text.data + node->pos.offset,
        .count = node->pos.length,
    };
}

static Delimiter delim0 () {
    return (Delimiter){};
}

static Delimiter delim1 (TokenTag t1) {
    return (Delimiter){ .t1=t1 };
}

static Delimiter delim2 (TokenTag t1, TokenTag t2) {
    return (Delimiter){ .t1=t1, .t2=t2 };
}

static Delimiter delim3 (TokenTag t1, TokenTag t2, TokenTag t3) {
    return (Delimiter){ .t1=t1, .t2=t2, .t3=t3 };
}

static Bool try_peek_delim (Lexer *lex, Delimiter delim) {
    #if BUILD_DEBUG
        if (delim.t1 == TOKEN_EOF) assert_dbg(delim.t2 == TOKEN_EOF);
        if (delim.t2 == TOKEN_EOF) assert_dbg(delim.t3 == TOKEN_EOF);
    #endif

    if ((delim.t1 != TOKEN_EOF) && (lex_peek_nth(lex, 1)->tag != delim.t1)) return false;
    if ((delim.t2 != TOKEN_EOF) && (lex_peek_nth(lex, 2)->tag != delim.t2)) return false;
    if ((delim.t3 != TOKEN_EOF) && (lex_peek_nth(lex, 3)->tag != delim.t3)) return false;
    return true;
}

static Void eat_blank_line (Parser *p) {
    p->trimming_tokens = true;
    lex_eat_line(p->lex);
    p->trimming_tokens = false;
}

static Void eat_tokens_until (Parser *p, Bool(*until)(Parser *, Token *)) {
    while (true) {
        Token *token = lex_peek(p->lex);
        if (until(p, token) || token->tag == TOKEN_EOF) break;
        lex_eat(p->lex);
    }
}

static Bool try_eat_indent (Parser *p) {
    if (p->actual_indent == -1) {
        Token *token = lex_try_peek(p->lex, TOKEN_SPACES);

        if (! token) {
            p->actual_indent = 0;
        } else {
            I64 indent = token->text.count / 2;
            p->actual_indent = min(indent, p->wanted_indent);

            // Trim token:
            token->text.data  += 2 * p->actual_indent;
            token->text.count -= 2 * p->actual_indent;
            token->pos.offset += 2 * p->actual_indent;
            token->pos.length -= 2 * p->actual_indent;
            if (token->text.count == 0) {
                p->trimming_tokens = true;
                lex_eat(p->lex);
                p->trimming_tokens = false;
            }
        }
    }

    return (p->actual_indent == p->wanted_indent);
}

static Bool rest_of_line_is_blank (Parser *p) {
    Token *t1 = lex_peek_nth(p->lex, 1);
    Token *t2 = lex_peek_nth(p->lex, 2);
    return t1->tag == '\n' || (t1->tag == TOKEN_SPACES && t2->tag == '\n');
}

static Void on_token_eaten (Token *token, Void *p_) {
    Auto p = cast(Parser*, p_);

    if (! p->trimming_tokens) p->last_eaten_token = token->pos;

    if (token->tag == '\n') {
        p->start_of_word = true;
        p->start_of_line = true;
        p->actual_indent = -1;
    } else {
        p->start_of_word = (token->tag == TOKEN_SPACES);
        p->start_of_line = p->start_of_line && p->start_of_word;
    }
}

// These are dummy parselets returned by get_block_parselet()
// to indicate a special case. They should never be called.
Noreturn static MarkupAst *unindent (Parser *) { panic(); }
Noreturn static MarkupAst *blank    (Parser *) { panic(); }

static MarkupAst *parse_text (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_TEXT);

    while (true) {
        Token *token = lex_eat(p->lex);
        if (token->tag == '\n') break;
        if (get_inline_parselet(p) != parse_text) break;
    }

    return complete_node(p, node);
}

static String parse_text_until (Parser *p, Bool(*until)(Parser*, Token*)) {
    Auto result = astr_new(p->mem);
    Auto start  = lex_peek(p->lex)->pos.offset;

    while (true) {
        Token *token = lex_peek(p->lex);
        Auto end     = p->last_eaten_token.offset + p->last_eaten_token.length;

        if (token->tag == TOKEN_EOF) {
            astr_push_str(&result, str_slice(p->text, start, end-start));
            break;
        } else if (until(p, token)) {
            astr_push_str(&result, str_slice(p->text, start, end-start));
            break;
        } else if (token->tag == '\\') {
            astr_push_str(&result, str_slice(p->text, start, end-start));
            lex_eat(p->lex);
            start = lex_eat(p->lex)->pos.offset;
        } else {
            lex_eat(p->lex);
        }
    }

    return astr_to_str(&result);
}

static MarkupAst *parse_paragraph (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_PARAGRAPH);
    parse_inlines(p, &node->children, delim0());
    return complete_node(p, node);
}

static MarkupAst *parse_italic (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_ITALIC);
    lex_eat(p->lex);
    parse_inlines(p, &node->children, delim1(TOKEN_ASTERISK));
    return complete_node(p, node);
}

static MarkupAst *parse_bold (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_BOLD);
    lex_eat(p->lex);
    lex_eat(p->lex);
    parse_inlines(p, &node->children, delim2(TOKEN_ASTERISK, TOKEN_ASTERISK));
    return complete_node(p, node);
}

static MarkupAst *parse_highlight (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_HIGHLIGHT);
    lex_eat(p->lex);
    lex_eat(p->lex);
    lex_eat(p->lex);
    parse_inlines(p, &node->children, delim3(TOKEN_ASTERISK, TOKEN_ASTERISK, TOKEN_ASTERISK));
    return complete_node(p, node);
}

static MarkupAst *parse_sup (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_SUP);
    lex_eat(p->lex);
    p->stop_parsing_inline_at_newline++;
    parse_inlines(p, &node->children, delim1(TOKEN_CARET));
    p->stop_parsing_inline_at_newline--;
    return complete_node(p, node);
}

static MarkupAst *parse_sub (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_SUB);
    lex_eat(p->lex);
    p->stop_parsing_inline_at_newline++;
    parse_inlines(p, &node->children, delim1(TOKEN_TILDE));
    p->stop_parsing_inline_at_newline--;
    return complete_node(p, node);
}

static MarkupAst *parse_strike (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_STRIKE);
    lex_eat(p->lex);
    lex_eat(p->lex);
    parse_inlines(p, &node->children, delim2(TOKEN_TILDE, TOKEN_TILDE));
    return complete_node(p, node);
}

static MarkupAst *parse_raw_inline (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_RAW_INLINE);
    lex_eat(p->lex);

    Bool monospace = lex_try_eat(p->lex, TOKEN_BACKTICK);
    cast(MarkupAstRawInline*, node)->monospace = monospace;

    p->inside_raw_text_region = true;
    Delimiter d = monospace ? delim2(TOKEN_BACKTICK, TOKEN_BACKTICK) : delim1(TOKEN_BACKTICK);
    parse_inlines(p, &node->children, d);
    p->inside_raw_text_region = false;

    return complete_node(p, node);
}

static Bool parse_link_helper1 (Parser *p, Token *token) {
    if (token->tag == '>') {
        return true;
    } else if (token->tag == ';') {
        lex_eat(p->lex);
        lex_try_eat(p->lex, TOKEN_SPACES);
        return true;
    } else {
        return false;
    }
}

static Bool parse_link_helper2 (Parser *p, Token *token) {
    if (token->tag == '>') {
        lex_eat(p->lex);
        return true;
    } else {
        return false;
    }
}

static MarkupAst *parse_link (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_LINK);
    Auto n = cast(MarkupAstLink*, node);
    lex_eat(p->lex);
    n->link = parse_text_until(p, parse_link_helper1);
    n->alias = parse_text_until(p, parse_link_helper2);
    return complete_node(p, node);
}

static MarkupAst *parse_tag_ref (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_TAG_REF);
    MarkupAst *child = make_node(p, MARKUP_AST_TEXT);
    array_push(&node->children, child);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_WORD);
    complete_node(p, child);
    if (p->body_tags.umap.mem) map_add(&p->body_tags, markup_ast_get_text(child, p->text), 0);
    return complete_node(p, node);
}

static MarkupAst *parse_escape (Parser *p) {
    lex_eat(p->lex);
    return parse_text(p);
}

static Parselet get_inline_parselet (Parser *p) {
    Token *token = lex_peek(p->lex);

    if (token->tag == TOKEN_EOF) return 0;
    if (token->tag == '\n' && p->stop_parsing_inline_at_newline) return 0;
    if (p->start_of_line && get_block_parselet(p) != parse_paragraph) return 0;

    if (p->stop_parsing_inline_at.count) {
        Delimiter delim = array_get_last(&p->stop_parsing_inline_at);
        if (try_peek_delim(p->lex, delim)) return 0;
    }

    if (! p->inside_raw_text_region) {
        Token *token = lex_peek(p->lex);
        Auto repeats = lex_get_token_repeats(p->lex, token);

        switch (token->tag) {
        case '\\':
            return parse_escape;
        case '*':
            if (repeats == 1) return parse_italic;
            if (repeats == 2) return parse_bold;
            if (repeats == 3) return parse_highlight;
            break;
        case '~':
            if (repeats == 1) return parse_sub;
            if (repeats == 2) return parse_strike;
            break;
        case '`':
            if (repeats == 1 || repeats == 2) return parse_raw_inline;
            break;
        case '^':
            if (repeats == 1) return parse_sup;
            break;
        case '<':
            return parse_link;
        case '@':
            if (p->start_of_word && lex_try_peek_nth(p->lex, 2, TOKEN_WORD)) return parse_tag_ref;
            break;
        default:
            break;
        }
    }

    return parse_text;
}

static Void parse_inlines (Parser *p, ArrayMarkupAst *out, Delimiter delim) {
    if (delim.t1 != TOKEN_EOF) {
        array_push(&p->stop_parsing_inline_at, delim);
    }

    while (true) {
        Parselet parselet = get_inline_parselet(p);
        if (! parselet) break;
        array_push(out, parselet(p));
    }

    if (delim.t1 != TOKEN_EOF) {
        array_pop(&p->stop_parsing_inline_at);
        if (try_peek_delim(p->lex, delim)) {
            lex_eat(p->lex);
            if (delim.t2 != TOKEN_EOF) lex_eat(p->lex);
            if (delim.t3 != TOKEN_EOF) lex_eat(p->lex);
        }
    }
}

static MarkupAstTableCellConfig parse_table_cell_config (Parser *p) {
    if (! lex_try_eat(p->lex, TOKEN_OPEN_BRACKET)) {
        lex_try_eat(p->lex, TOKEN_SPACES);
        return default_table_cell_config;
    }

    MarkupAstTableCellConfig config = default_table_cell_config;

    while (true) {
        Token *token = lex_peek(p->lex);

        if (token->tag == TOKEN_EOF) {
            break;
        } else if (token->tag == '\n') {
            lex_eat(p->lex);
            break;
        } else if (token->tag == ']') {
            lex_eat(p->lex);
            lex_try_eat(p->lex, TOKEN_SPACES);
            lex_try_eat(p->lex, TOKEN_NEWLINE);
            break;
        } else if (token->tag == '#') {
            lex_eat(p->lex);
            config.no_bg = true;
        } else if (
            (token->tag == '*' || token->tag == TOKEN_U64) &&
            lex_try_peek_nth(p->lex, 2, TOKEN_COLON) &&
            (lex_try_peek_nth(p->lex, 3, TOKEN_ASTERISK) || lex_try_peek_nth(p->lex, 3, TOKEN_U64))
        ) {
            config.width = (token->tag == '*') ? -1 : cast(I64, max(1lu, token->u64));
            lex_eat(p->lex);
            lex_eat(p->lex);
            Token *t = lex_eat(p->lex);
            config.height = (t->tag == '*') ? -1 : cast(I64, max(1lu, t->u64));
        } else {
            lex_eat(p->lex);
        }
    }

    return config;
}

static MarkupAst *parse_table_cell (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_TABLE_CELL);
    lex_eat(p->lex);
    cast(MarkupAstTableCell*, node)->config = parse_table_cell_config(p);
    parse_blocks(p, &node->children);
    return complete_node(p, node);
}

static MarkupAst *parse_table_row (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_TABLE_ROW);

    while (true) {
        if (get_block_parselet(p) != parse_table) {
            complete_node(p, node);
            break;
        } else if (lex_try_peek_nth(p->lex, 2, TOKEN_MINUS)) {
            complete_node(p, node);
            lex_eat_line(p->lex);
            break;
        } else {
            array_push(&node->children, parse_table_cell(p));
        }
    }

    return node;
}

static MarkupAst *parse_table (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_TABLE);
    if (lex_try_peek_nth(p->lex, 2, TOKEN_MINUS)) lex_eat_line(p->lex);
    while (get_block_parselet(p) == parse_table) array_push(&node->children, parse_table_row(p));
    return complete_node(p, node);
}

static MarkupAst *parse_header (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_HEADER);
    Auto n = cast(MarkupAstHeader*, node);

    Token *token = lex_peek(p->lex);
    n->size = min(5lu, lex_get_token_repeats(p->lex, token));
    for (U64 i = 0; i < n->size; ++i) lex_eat(p->lex);

    lex_try_eat(p->lex, TOKEN_SPACES);
    array_push(&node->children, parse_paragraph(p));

    return complete_node(p, node);
}

static String try_parse_date (Parser *p) {
    Token *t1 = lex_peek_nth(p->lex, 1);
    Token *t5 = lex_peek_nth(p->lex, 5);

    U64 start   = t1->pos.offset;
    U64 end     = t5->pos.offset + t5->pos.length;
    String text = str_slice(p->text, start, end-start);

    if (! os_is_date_ymd_valid(os_str_to_date(text))) return (String){};

    for (U64 i = 0; i < 5; ++i) lex_eat(p->lex);
    return text;
}

static Bool is_meta_config_delimiter (Parser *p, Token *token) {
    TokenTag t = token->tag;
    return t == ']' || t == '\n' || t == TOKEN_EOF || t == TOKEN_SPACES;
}

static Bool try_peek_meta_config_delimiter (Parser *p, U64 n) {
    Token *token = lex_peek_nth(p->lex, n);
    return is_meta_config_delimiter(p, token);
}

static Bool is_closing_square_bracket (Parser *p, Token *token) {
    return token->tag == ']';
}

static Bool try_parse_meta_config (Parser *p, MarkupAstMetaConfig *config) {
    Token *token = lex_eat(p->lex);
    String txt   = token->text;

    if (token->tag == '@') {
        if (! lex_try_peek(p->lex, TOKEN_WORD)) return false;

        U64 start = token->pos.offset;
        lex_try_eat(p->lex, TOKEN_WORD);

        if (! (config->flags & MARKUP_AST_META_CONFIG_HAS_TAGS)) map_init(&config->tags, p->mem);
        config->flags |= MARKUP_AST_META_CONFIG_HAS_TAGS;

        U64 end = p->last_eaten_token.offset + p->last_eaten_token.length;
        map_add(&config->tags, str_slice(p->text, start, end-start), 0);
    } else if (token->tag == '#') {
        Token *t = lex_eat(p->lex);
        if (t->tag != TOKEN_U64) return false;
        config->priority = t->u64;
        config->flags |= MARKUP_AST_META_CONFIG_HAS_PRIORITY;
    } else if (str_match(txt, str("x"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_DONE;
    } else if (str_match(txt, str("hide"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_HIDE;
    } else if (str_match(txt, str("pin"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_PIN;
    } else if (str_match(txt, str("due")) || str_match(txt, str("created")) || str_match(txt, str("completed"))) {
        if (! lex_try_eat(p->lex, TOKEN_COLON)) return false;

        lex_try_eat(p->lex, TOKEN_SPACES);
        String date = try_parse_date(p);

        if (! date.data) {
            return false;
        } else if (str_match(txt, str("due"))) {
            config->flags |= MARKUP_AST_META_CONFIG_HAS_DUE;
            config->due = date;
        } else if (str_match(txt, str("created"))) {
            config->flags |= MARKUP_AST_META_CONFIG_HAS_CREATED;
            config->created = date;
        } else {
            config->flags |= MARKUP_AST_META_CONFIG_HAS_COMPLETED;
            config->completed = date;
        }
    } else if (str_match(txt, str("track"))) {
        if (! lex_try_eat(p->lex, TOKEN_COLON)) return false;
        lex_try_eat(p->lex, TOKEN_SPACES);
        Token *t = lex_eat(p->lex);
        if (t->tag != TOKEN_U64) return false;
        config->track = t->u64;
        config->flags |= MARKUP_AST_META_CONFIG_HAS_TRACK;
    } else if (str_match(txt, str("image"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_IMAGE_PATH;
        lex_try_eat(p->lex, TOKEN_SPACES);

        Token *t = lex_try_eat(p->lex, TOKEN_U64);
        if (t) {
            config->flags |= MARKUP_AST_META_CONFIG_HAS_IMAGE_WIDTH;
            config->image_width = max(1lu, t->u64);
        }

        lex_try_eat(p->lex, TOKEN_SPACES);
        config->image_path = parse_text_until(p, is_meta_config_delimiter);
    } else if (str_match(txt, str("tip"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_ADMONITION;
        config->admonition = MARKUP_AST_ADMONITION_TIP;
    } else if (str_match(txt, str("note"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_ADMONITION;
        config->admonition = MARKUP_AST_ADMONITION_NOTE;
    } else if (str_match(txt, str("warning"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_ADMONITION;
        config->admonition = MARKUP_AST_ADMONITION_WARNING;
    } else if (str_match(txt, str("important"))) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_ADMONITION;
        config->admonition = MARKUP_AST_ADMONITION_IMPORTANT;
    } else if (str_match(txt, str("spoiler"))) {
        if (! lex_try_eat(p->lex, TOKEN_COLON)) return false;
        lex_try_eat(p->lex, TOKEN_SPACES);
        config->flags |= MARKUP_AST_META_CONFIG_HAS_SPOILER;
        config->spoiler = parse_text_until(p, is_closing_square_bracket);
    }

    return true;
}

static U64 get_filter_precedence (Parser *p, MarkupAstTag tag) {
    switch (tag) {
    case MARKUP_AST_FILTER_NOT:
        return 4;
    case MARKUP_AST_FILTER_AND:
        return 3;
    case MARKUP_AST_FILTER_OR:
        return 2;
    case MARKUP_AST_FILTER_DUE:
    case MARKUP_AST_FILTER_DONE:
    case MARKUP_AST_FILTER_ANY:
    case MARKUP_AST_FILTER_PIN:
    case MARKUP_AST_FILTER_HIDE:
    case MARKUP_AST_FILTER_TAG:
    case MARKUP_AST_FILTER_TRACK:
    case MARKUP_AST_FILTER_FUZZY:
    case MARKUP_AST_FILTER_STRING:
    case MARKUP_AST_FILTER_PRIORITY:
        return 1;
    default: badpath;
    }
}

static MarkupAst *parse_filter_or (Parser *p, MarkupAst *lhs) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_OR);
    array_push(&node->children, lhs);
    lex_eat(p->lex);
    array_push(&node->children, parse_filter(p, get_filter_precedence(p, MARKUP_AST_FILTER_OR)));
    return complete_node(p, node);
}

static MarkupAst *parse_filter_and (Parser *p, MarkupAst *lhs) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_AND);
    array_push(&node->children, lhs);
    lex_eat(p->lex);
    array_push(&node->children, parse_filter(p, get_filter_precedence(p, MARKUP_AST_FILTER_AND)));
    return complete_node(p, node);
}

static MarkupAst *parse_filter_nop (Parser *p, MarkupAst *lhs) {
    return lhs;
}

static MarkupAst *parse_filter_any (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_ANY);
    lex_eat(p->lex);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_tag (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_TAG);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_WORD);
    complete_node(p, node);
    cast(MarkupAstFilterTag*, node)->text = markup_ast_get_text(node, p->text);
    return node;
}

static MarkupAst *parse_filter_not (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_NOT);
    lex_eat(p->lex);
    MarkupAst *lhs = parse_filter(p, get_filter_precedence(p, MARKUP_AST_FILTER_NOT));
    array_push(&node->children, lhs);
    return complete_node(p, node);
}

static Bool parse_filter_fuzzy_helper (Parser *p, Token *token) {
    if (token->tag != '/') return false;
    lex_eat(p->lex);
    return true;
}

static MarkupAst *parse_filter_fuzzy (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_FUZZY);
    Auto n = cast(MarkupAstFilterFuzzy*, node);
    lex_eat(p->lex);
    n->needle = parse_text_until(p, parse_filter_fuzzy_helper);
    return complete_node(p, node);
}

static Bool parse_filter_string_helper (Parser *p, Token *token) {
    if (token->tag != '\'') return false;
    lex_eat(p->lex);
    return true;
}

static MarkupAst *parse_filter_string (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_STRING);
    Auto n = cast(MarkupAstFilterString*, node);
    lex_eat(p->lex);
    n->needle = parse_text_until(p, parse_filter_string_helper);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_parens (Parser *p) {
    lex_eat(p->lex);
    MarkupAst *result = parse_filter(p, 0);
    lex_eat(p->lex);
    return result;
}

static MarkupAst *parse_filter_priority (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_PRIORITY);
    lex_eat(p->lex);
    if (lex_try_peek(p->lex, TOKEN_U64)) {
        cast(MarkupAstFilterPriority*, node)->priority = lex_eat(p->lex)->u64;
    }
    return complete_node(p, node);
}

static MarkupAst *parse_filter_done (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_DONE);
    lex_eat(p->lex);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_due (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_DUE);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);
    if (lex_try_eat(p->lex, '<')) {
        cast(MarkupAstFilterDue*, node)->cmp = MARKUP_CMP_LESS;
    } else if (lex_try_eat(p->lex, '>')) {
        cast(MarkupAstFilterDue*, node)->cmp = MARKUP_CMP_GREATER;
    }
    lex_try_eat(p->lex, TOKEN_SPACES);
    cast(MarkupAstFilterDue*, node)->date = try_parse_date(p);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_created (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_CREATED);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);
    if (lex_try_eat(p->lex, '<')) {
        cast(MarkupAstFilterCreated*, node)->cmp = MARKUP_CMP_LESS;
    } else if (lex_try_eat(p->lex, '>')) {
        cast(MarkupAstFilterCreated*, node)->cmp = MARKUP_CMP_GREATER;
    }
    lex_try_eat(p->lex, TOKEN_SPACES);
    cast(MarkupAstFilterCreated*, node)->date = try_parse_date(p);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_completed (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_COMPLETED);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);
    if (lex_try_eat(p->lex, '<')) {
        cast(MarkupAstFilterCompleted*, node)->cmp = MARKUP_CMP_LESS;
    } else if (lex_try_eat(p->lex, '>')) {
        cast(MarkupAstFilterCompleted*, node)->cmp = MARKUP_CMP_GREATER;
    }
    lex_try_eat(p->lex, TOKEN_SPACES);
    cast(MarkupAstFilterCompleted*, node)->date = try_parse_date(p);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_pin (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_PIN);
    lex_eat(p->lex);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_hide (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_HIDE);
    lex_eat(p->lex);
    return complete_node(p, node);
}

static MarkupAst *parse_filter_track (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_TRACK);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);

    Token *id = lex_eat(p->lex);
    if (id->tag == TOKEN_U64) {
        cast(MarkupAstFilterTrack*, node)->id = id->u64;
    } else {
        node->tag = MARKUP_AST_FILTER_ERROR;
    }

    return complete_node(p, node);
}

static MarkupAst *parse_filter_error (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_FILTER_ERROR);
    return complete_node(p, node);
}

static MarkupAst *parse_prefix_filter (Parser *p) {
    lex_eat_whitespace(p->lex);
    Token *token = lex_peek(p->lex);

    switch (token->tag) {
    case '*':  return parse_filter_any(p);
    case '@':  return parse_filter_tag(p);
    case '!':  return parse_filter_not(p);
    case '/':  return parse_filter_fuzzy(p);
    case '\'': return parse_filter_string(p);
    case '(':  return parse_filter_parens(p);
    case '#':  return parse_filter_priority(p);
    default:
        if (str_match(token->text, str("x")))         return parse_filter_done(p);
        if (str_match(token->text, str("due")))       return parse_filter_due(p);
        if (str_match(token->text, str("pin")))       return parse_filter_pin(p);
        if (str_match(token->text, str("hide")))      return parse_filter_hide(p);
        if (str_match(token->text, str("track")))     return parse_filter_track(p);
        if (str_match(token->text, str("created")))   return parse_filter_created(p);
        if (str_match(token->text, str("completed"))) return parse_filter_completed(p);
    }

    return parse_filter_error(p);
}

static InfixParselet get_infix_filter_parselet (Parser *p, U64 *out_prec) {
    lex_eat_whitespace(p->lex);
    Token *token = lex_peek(p->lex);

    if (token->tag == '|') {
        *out_prec = get_filter_precedence(p, MARKUP_AST_FILTER_OR);
        return parse_filter_or;
    } else if (token->tag == '&') {
        *out_prec = get_filter_precedence(p, MARKUP_AST_FILTER_AND);
        return parse_filter_and;
    } else {
        *out_prec = 0;
        return parse_filter_nop;
    }
}

static MarkupAst *parse_filter (Parser *p, U64 lhs_precedence) {
    MarkupAst *result = parse_prefix_filter(p);

    while (true) {
        U64 precedence; InfixParselet parselet = get_infix_filter_parselet(p, &precedence);
        if (precedence <= lhs_precedence) break;
        result = parselet(p, result);
    }

    return result;
}

MarkupAst *markup_filter_parse (Mem *mem, String text) {
    Parser *p = parser_new(mem, text);
    return parse_filter(p, 0);
}

static MarkupAst *parse_meta (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_META);

    lex_eat(p->lex);
    Auto config = mem_new(p->mem, MarkupAstMetaConfig);
    cast(MarkupAstMeta*, node)->config = config;

    while (true) {
        Token *token = lex_peek(p->lex);

        if (token->tag == TOKEN_EOF) {
            break;
        } else if (token->tag == ']') {
            lex_eat(p->lex);
            lex_try_eat(p->lex, TOKEN_SPACES);
            lex_try_eat(p->lex, TOKEN_NEWLINE);
            break;
        } else {
            Bool ok = try_parse_meta_config(p, config);
            if (! ok) eat_tokens_until(p, is_meta_config_delimiter);
        }
    }

    Bool top_level = !p->body_tags.umap.mem;
    if (top_level) map_init(&p->body_tags, p->mem);

    parse_blocks(p, &node->children);

    if (top_level && p->body_tags.umap.count) {
        config->flags |= MARKUP_AST_META_CONFIG_HAS_BODY_TAGS;
        config->body_tags.umap = p->body_tags.umap;
        p->body_tags.umap = (UMap){};
    }

    return complete_node(p, node);
}

static MarkupAst *parse_list_item (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_LIST_ITEM);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);
    lex_try_eat(p->lex, TOKEN_NEWLINE);
    parse_blocks(p, &node->children);
    return complete_node(p, node);
}

static MarkupAst *parse_list (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_LIST);
    while (get_block_parselet(p) == parse_list) array_push(&node->children, parse_list_item(p));
    return complete_node(p, node);
}

static MarkupAst *parse_ordered_list (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_ORDERED_LIST);

    while (get_block_parselet(p) == parse_ordered_list) {
        if (lex_peek(p->lex)->u64 < node->children.count) break;
        lex_eat(p->lex);
        array_push(&node->children, parse_list_item(p));
    }

    return complete_node(p, node);
}

static MarkupAst *parse_separator (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_SEPARATOR);
    lex_eat_line(p->lex);
    return complete_node(p, node);
}

static MarkupAst *parse_raw_block (Parser *p) {
    MarkupAst *node = make_node(p, MARKUP_AST_RAW_BLOCK);
    lex_eat(p->lex);
    lex_try_eat(p->lex, TOKEN_SPACES);
    p->inside_raw_text_region = true;
    parse_blocks(p, &node->children);
    p->inside_raw_text_region = false;
    return complete_node(p, node);
}

// If eof, returns 0.
// If indentation level lowered, unindent.
// If blank line, blank.
// Else the parselet for the block we're on.
static Parselet get_block_parselet (Parser *p) {
    Token *token = lex_peek(p->lex);

    if (token->tag == TOKEN_EOF) return 0;
    if (rest_of_line_is_blank(p)) return blank;
    if (p->start_of_line && !try_eat_indent(p)) return unindent;

    if (! p->inside_raw_text_region) {
        Token *token = lex_peek(p->lex);
        Auto repeats = lex_get_token_repeats(p->lex, token);

        if (token->tag == '#') return parse_header;
        if (token->tag == '[' && repeats == 1) return parse_meta;
        if (token->tag == '-' && repeats == 1) return parse_list;
        if (token->tag == '|' && repeats == 1) return parse_table;
        if (token->tag == '=' && repeats >= 3) return parse_separator;
        if (token->tag == '>' && repeats == 1) return parse_raw_block;
        if (token->tag == TOKEN_U64 && lex_try_peek_nth(p->lex, 2, TOKEN_DOT)) return parse_ordered_list;
    }

    return parse_paragraph;
}

static Void parse_blocks (Parser *p, ArrayMarkupAst *out) {
    p->wanted_indent++;

    while (true) {
        Parselet parselet = get_block_parselet(p);

        if (! parselet) {
            break;
        } else if (parselet == unindent) {
            break;
        } else if (parselet == blank) {
            eat_blank_line(p);
        } else {
            array_push(out, parselet(p));
        }
    }

    p->wanted_indent--;
}

static MarkupAst *parse_markup (Parser *p) {
    Auto node = make_node(p, MARKUP_AST_ROOT);
    parse_blocks(p, &node->children);
    return complete_node(p, node);
}

MarkupAst *markup_parse (Mem *mem, String text) {
    Parser *p = parser_new(mem, text);
    return parse_markup(p);
}

// Translates an index into the innermost ast node containing
// it as as the path starting from the input node.
//
// If the index is not contained within any of the nodes, a
// nearby node will be returned. You only get an empty path
// back if node->children is empty.
SliceMarkupAst markup_idx_to_ast_path (Mem *mem, MarkupAst *node, U64 idx) {
    ArrayMarkupAst result;
    array_init(&result, mem);

    if (node->children.count == 0) return result.as_slice;

    while (true) {
        array_iter (child, &node->children) {
            if (idx >= child->pos.offset + child->pos.length) {
                // Below the child.
                continue;
            } else if (idx >= child->pos.offset) {
                // On the child.
                array_push(&result, child);
                node = child;
                goto continue_outer;
            } else if (ARRAY_IDX > 0) {
                // Between children.
                array_push(&result, child);
                goto break_outer;
            } else {
                // Before first child.
                if (result.count == 0) array_push(&result, child);
                goto break_outer;
            }
        }

        { // Below the children.
            MarkupAst *last_child = array_try_get_last(&node->children);
            if (last_child) array_push(&result, last_child);
            break;
        }

        continue_outer:;
    } break_outer:;

    return result.as_slice;
}
