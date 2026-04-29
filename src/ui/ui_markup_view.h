#pragma once

#include "base/core.h"
#include "markup/markup.h"
#include "buffer/buffer.h"
#include "ui/ui.h"

istruct (ParagraphContext);
istruct (MarkupView);

typedef UiBox *(*MarkupBuilder) (MarkupView *, MarkupAst *);

// You can obtain this struct by calling ui_get_box_data()
// on the box returned by ui_markup_view_buf().
istruct (MarkupView) {
    UiBoxData header;
    Bool purge_cache;
    Bool build_ast_to_box;
    U64 buf_version;
    String *out_clicked_tag;
    String text;
    MarkupBuilder custom_builder;
    Buf *buf;
    Mem *ast_mem;
    UiBox *root;
    MarkupAst *ast;
    Map(MarkupAst*, UiBox*) ast_to_box;
    ParagraphContext *paragraph_context;
    MarkupAstHeader *inside_header;
    Bool inside_raw_block;
};

UiBox *ui_markup_view     (String id, String text, String *);
UiBox *ui_markup_view_buf (String id, Buf *, Bool build_ast_to_box, String *, MarkupBuilder);
