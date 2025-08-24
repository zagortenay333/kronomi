#include "base/core.h"
#include "base/mem.h"
#include "base/array.h"
#include "base/map.h"
#include "base/string.h"
#include "base/log.h"

#include <stdio.h>
#include <vector>

int main () {
    tmem_setup(&mem_root, 1*MB);
    log_setup(&mem_root, 4*KB);

    tmem_new(tm);
    Array<U32> a = array_new<U32>(tm);

    for (U32 i = 0; i < 10; ++i) {
        array_push(&a, i);
    }

    array_iter (x, &a) printf("%u ", x);
    printf("\n");

    array_reverse(&a);
    array_iter (x, &a) printf("%u ", x);
    printf("\n");

    array_find_remove_all(&a, [&](Auto it){
        return it % 2;
    });
    array_iter (x, &a) printf("%u ", x);
    printf("\n");

    array_iter_ptr (x, &a) printf("%u ", *x);
    printf("\n");

    array_iter_ptr_back (x, &a) printf("%u ", *x);
    printf("\n");

    array_push_n(&a, 9, 7, 5, 3, 1);
    array_sort(&a);
    array_iter_ptr (x, &a) printf("%u ", *x);
    printf("\n");

    Auto s = slice(&a);
    array_reverse(&s);
    array_iter_ptr (x, &a) printf("%u ", *x);
    printf("\n");

    String x = str("hello there sailor! how's it going?");

    Auto tokens = array_new<String>(tm);
    str_split(x, str(" "), false, false, &tokens);
    array_iter (token, &tokens) printf("[%.*s] ", STR(token));
    printf("\n");

    Auto map = map_new<U64, CString>(tm, 0);
    map_add(&map, 42lu, "Hello world!");
    map_add(&map, 420lu, "Foo bar baz!");
    map_iter (e, &map) printf("hash=%lu key=%lu val=%s\n", e->hash, e->key, e->val);

    Auto val = map_get_assert(&map, 420lu);
    printf("======== [%s] deleted\n", val);

    map_remove(&map, 420lu);
    map_iter (e, &map) printf("hash=%lu key=%lu val=%s\n", e->hash, e->key, e->val);

    {
        log_scope(ls, 1); // Var 'ls' closed at scope exit.
        
        log_msg_fmt(LOG_NOTE, "#0", 1, "A note.");
        log_msg_fmt(LOG_WARNING, "#1", 1, "A warning.");
        log_msg_fmt(LOG_NOTE, "#2", 0, "Non-iterable note.");
        log_msg_fmt(LOG_ERROR, "#3", 1, "An error.");
        
        log_msg(msg, LOG_PLAIN, "", 0); // Var 'msg' freed at scope exit.
        astr_push_cstr(msg, "\nIterable messages:\n");
        
        array_iter_ptr (it, &ls->iter) {
            String body = str_slice(astr_to_str(&ls->iterable_data), it->body_offset, it->trace_offset - it->body_offset - 1);
            astr_push_fmt(msg, "    [%s] [%.*s] [%.*s]\n", log_tag_str[it->tag], STR(it->user_tag), STR(body));
        }
    }
}
