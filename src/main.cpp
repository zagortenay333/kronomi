#include "base/core.h"
#include "base/mem.h"
#include "base/array.h"

#include <stdio.h>

int main () {
    defer { printf("2\n"); };
    printf("1\n");
}
