#include "base/core.h"
#include "base/mem.h"
#include "base/array.h"
#include "base/map.h"
#include "base/string.h"

#include <stdio.h>
#include <vector>

int main () {
    tmem_setup(&mem_root, 1*MB);

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
}
