#include "commit.h"
#include <stdio.h>

int main() {
    ObjectID id;

    if (commit_create("my first commit", &id) != 0) {
        printf("Commit failed\n");
        return 1;
    }

    printf("Commit created successfully!\n");
    return 0;
}
