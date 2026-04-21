#include "index.h"
#include <stdio.h>

int main() {
    Index idx;

    index_load(&idx);

    if (index_add(&idx, "test.txt") != 0) {
        printf("Failed\n");
        return 1;
    }

    index_save(&idx);

    printf("Success\n");
    return 0;
}
