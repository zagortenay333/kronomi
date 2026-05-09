#pragma once

#include "base/core.h"
#include "base/log.h"
#include "base/map.h"
#include "os/time.h"

// X(tag, type)
#define EACH_MARKUP_AST_NODE(X)\
    X(MARKUP_AST_ROOT, MarkupAstRoot)\
    X(MARKUP_AST_DUMMY, MarkupAstDummy)\
    X(MARKUP_AST_LIST, MarkupAstList)\
    X(MARKUP_AST_LIST_ITEM, MarkupAstListItem)\
    X(MARKUP_AST_ORDERED_LIST, MarkupAstOrderedList)\
    X(MARKUP_AST_SEPARATOR, MarkupAstSeparator)\
    X(MARKUP_AST_RAW_BLOCK, MarkupAstRawBlock)\
    X(MARKUP_AST_HEADER, MarkupAstHeader)\
    X(MARKUP_AST_PARAGRAPH, MarkupAstParagraph)\
    X(MARKUP_AST_TABLE, MarkupAstTable)\
    X(MARKUP_AST_TABLE_ROW, MarkupAstTableRow)\
    X(MARKUP_AST_TABLE_CELL, MarkupAstTableCell)\
    X(MARKUP_AST_META, MarkupAstMeta)\
    X(MARKUP_AST_TEXT, MarkupAstText)\
    X(MARKUP_AST_SUB, MarkupAstSub)\
    X(MARKUP_AST_SUP, MarkupAstSup)\
    X(MARKUP_AST_STRIKE, MarkupAstStrike)\
    X(MARKUP_AST_BOLD, MarkupAstBold)\
    X(MARKUP_AST_LINK, MarkupAstLink)\
    X(MARKUP_AST_ITALIC, MarkupAstItalic)\
    X(MARKUP_AST_HIGHLIGHT, MarkupAstHighlight)\
    X(MARKUP_AST_RAW_INLINE, MarkupAstRawInline)\
    X(MARKUP_AST_TAG_REF, MarkupAstTagRef)\
    X(MARKUP_AST_FILTER_NOT, MarkupAstFilterNot)\
    X(MARKUP_AST_FILTER_OR, MarkupAstFilterOr)\
    X(MARKUP_AST_FILTER_AND, MarkupAstFilterAnd)\
    X(MARKUP_AST_FILTER_ANY, MarkupAstFilterAny)\
    X(MARKUP_AST_FILTER_DUE, MarkupAstFilterDue)\
    X(MARKUP_AST_FILTER_CREATED, MarkupAstFilterCreated)\
    X(MARKUP_AST_FILTER_COMPLETED, MarkupAstFilterCompleted)\
    X(MARKUP_AST_FILTER_DONE, MarkupAstFilterDone)\
    X(MARKUP_AST_FILTER_PIN, MarkupAstFilterPin)\
    X(MARKUP_AST_FILTER_HIDE, MarkupAstFilterHide)\
    X(MARKUP_AST_FILTER_TAG, MarkupAstFilterTag)\
    X(MARKUP_AST_FILTER_ERROR, MarkupAstFilterError)\
    X(MARKUP_AST_FILTER_TRACK, MarkupAstFilterTrack)\
    X(MARKUP_AST_FILTER_FUZZY, MarkupAstFilterFuzzy)\
    X(MARKUP_AST_FILTER_STRING, MarkupAstFilterString)\
    X(MARKUP_AST_FILTER_PRIORITY, MarkupAstFilterPriority)

istruct (MarkupAstTableCellConfig) {
    I64 width;
    I64 height;
    Bool no_bg;
};

ienum (MarkupAstAdmonition, U8) {
    MARKUP_AST_ADMONITION_TIP,
    MARKUP_AST_ADMONITION_NOTE,
    MARKUP_AST_ADMONITION_WARNING,
    MARKUP_AST_ADMONITION_IMPORTANT,
};

fenum (MarkupAstMetaConfigFlags, U16) {
    MARKUP_AST_META_CONFIG_HAS_PRIORITY    = flag(0),
    MARKUP_AST_META_CONFIG_HAS_TRACK       = flag(1),
    MARKUP_AST_META_CONFIG_HAS_CREATED     = flag(2),
    MARKUP_AST_META_CONFIG_HAS_COMPLETED   = flag(3),
    MARKUP_AST_META_CONFIG_HAS_DUE         = flag(4),
    MARKUP_AST_META_CONFIG_HAS_PIN         = flag(5),
    MARKUP_AST_META_CONFIG_HAS_DONE        = flag(6),
    MARKUP_AST_META_CONFIG_HAS_HIDE        = flag(7),
    MARKUP_AST_META_CONFIG_HAS_SPOILER     = flag(8),
    MARKUP_AST_META_CONFIG_HAS_TAGS        = flag(9),
    MARKUP_AST_META_CONFIG_HAS_BODY_TAGS   = flag(10),
    MARKUP_AST_META_CONFIG_HAS_IMAGE_WIDTH = flag(11),
    MARKUP_AST_META_CONFIG_HAS_IMAGE_PATH  = flag(12),
    MARKUP_AST_META_CONFIG_HAS_ADMONITION  = flag(13),

    MARKUP_AST_META_TODO_FLAGS =
        MARKUP_AST_META_CONFIG_HAS_PRIORITY |
        MARKUP_AST_META_CONFIG_HAS_TRACK |
        MARKUP_AST_META_CONFIG_HAS_CREATED |
        MARKUP_AST_META_CONFIG_HAS_COMPLETED |
        MARKUP_AST_META_CONFIG_HAS_DUE |
        MARKUP_AST_META_CONFIG_HAS_PIN |
        MARKUP_AST_META_CONFIG_HAS_DONE |
        MARKUP_AST_META_CONFIG_HAS_HIDE |
        MARKUP_AST_META_CONFIG_HAS_TAGS
};

istruct (MarkupAstMetaConfig) {
    MarkupAstMetaConfigFlags flags;
    MarkupAstAdmonition admonition;
    U32 image_width;
    U64 priority;
    U64 track;
    String image_path;
    String due;
    String created;
    String completed;
    String spoiler;
    Set(String) tags; // Tags that appear in the header.
    Set(String) body_tags; // Tags that appear in the body.
};

ienum (MarkupCmp, U8) {
    MARKUP_CMP_EQUAL,
    MARKUP_CMP_LESS,
    MARKUP_CMP_GREATER,
};

ienum (MarkupAstTag, U8) {
    #define X(TAG, TYPE, ...) TAG, e##TYPE=TAG,
        EACH_MARKUP_AST_NODE(X)
    #undef X
    MARKUP_AST_TAG_COUNT
};

istruct (MarkupAst);
array_typedef(MarkupAst*, MarkupAst);

istruct (MarkupAst) {
    MarkupAstTag tag;
    SrcPos pos;
    ArrayMarkupAst children;
};

istruct (MarkupAstRoot)            { MarkupAst base; };
istruct (MarkupAstDummy)           { MarkupAst base; };
istruct (MarkupAstList)            { MarkupAst base; };
istruct (MarkupAstListItem)        { MarkupAst base; };
istruct (MarkupAstOrderedList)     { MarkupAst base; };
istruct (MarkupAstSeparator)       { MarkupAst base; };
istruct (MarkupAstRawBlock)        { MarkupAst base; };
istruct (MarkupAstHeader)          { MarkupAst base; U64 size; };
istruct (MarkupAstParagraph)       { MarkupAst base; };
istruct (MarkupAstTable)           { MarkupAst base; };
istruct (MarkupAstTableRow)        { MarkupAst base; };
istruct (MarkupAstTableCell)       { MarkupAst base; MarkupAstTableCellConfig config; };
istruct (MarkupAstMeta)            { MarkupAst base; MarkupAstMetaConfig *config; };
istruct (MarkupAstText)            { MarkupAst base; };
istruct (MarkupAstSub)             { MarkupAst base; };
istruct (MarkupAstSup)             { MarkupAst base; };
istruct (MarkupAstStrike)          { MarkupAst base; };
istruct (MarkupAstBold)            { MarkupAst base; };
istruct (MarkupAstLink)            { MarkupAst base; String link, alias; };
istruct (MarkupAstItalic)          { MarkupAst base; };
istruct (MarkupAstHighlight)       { MarkupAst base; };
istruct (MarkupAstRawInline)       { MarkupAst base; Bool monospace; };
istruct (MarkupAstTagRef)          { MarkupAst base; };
istruct (MarkupAstFilterNot)       { MarkupAst base; };
istruct (MarkupAstFilterOr)        { MarkupAst base; };
istruct (MarkupAstFilterAnd)       { MarkupAst base; };
istruct (MarkupAstFilterAny)       { MarkupAst base; };
istruct (MarkupAstFilterDue)       { MarkupAst base; MarkupCmp cmp; DateSpec date; };
istruct (MarkupAstFilterCreated)   { MarkupAst base; MarkupCmp cmp; DateSpec date; };
istruct (MarkupAstFilterCompleted) { MarkupAst base; MarkupCmp cmp; DateSpec date; };
istruct (MarkupAstFilterDone)      { MarkupAst base; };
istruct (MarkupAstFilterPin)       { MarkupAst base; };
istruct (MarkupAstFilterHide)      { MarkupAst base; };
istruct (MarkupAstFilterError)     { MarkupAst base; };
istruct (MarkupAstFilterTag)       { MarkupAst base; String text; };
istruct (MarkupAstFilterTrack)     { MarkupAst base; U64 id; };
istruct (MarkupAstFilterFuzzy)     { MarkupAst base; String needle; };
istruct (MarkupAstFilterString)    { MarkupAst base; String needle; };
istruct (MarkupAstFilterPriority)  { MarkupAst base; U64 priority; };

extern CString markup_ast_tag_to_cstr    [MARKUP_AST_TAG_COUNT];
extern U64     markup_ast_get_node_size  [MARKUP_AST_TAG_COUNT];
extern U8      markup_ast_get_node_align [MARKUP_AST_TAG_COUNT];

extern MarkupAstTableCellConfig default_table_cell_config;

MarkupAst     *markup_ast_alloc       (Mem *, MarkupAstTag);
String         markup_ast_get_text    (MarkupAst *, String);
SliceMarkupAst markup_idx_to_ast_path (Mem *, MarkupAst *, U64);
MarkupAst     *markup_parse           (Mem *, String);
MarkupAst     *markup_filter_parse    (Mem *, String);
U64            markup_ast_hash        (MarkupAst *);
