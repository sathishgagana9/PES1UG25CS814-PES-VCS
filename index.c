#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_FILE ".pes/index"

// externs (since no object.h)
extern int object_write(ObjectType, const void *, size_t, ObjectID *);
extern void hash_to_hex(const ObjectID *, char *);

// 🔴 local hex → hash (fixes your error)
  static void hex_to_hash_local(const char *hex, ObjectID *id) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sscanf(hex + 2*i, "%2hhx", &id->hash[i]);
    }
}

// ================= LOAD =================
int index_load(Index *idx) {
    idx->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;

    while (idx->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &idx->entries[idx->count];
        char hex[HASH_HEX_SIZE + 1];

        if (fscanf(f, "%o %s %s\n", &e->mode, hex, e->path) != 3)
            break;

        hex_to_hash_local(hex, &e->hash);
        idx->count++;
    }

    fclose(f);
    return 0;
}

// ================= SAVE =================
int index_save(const Index *idx) {
    FILE *f = fopen(INDEX_FILE, "w");
    if (!f) return -1;

    for (int i = 0; i < idx->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&idx->entries[i].hash, hex);

        fprintf(f, "%o %s %s\n",
                idx->entries[i].mode,
                hex,
                idx->entries[i].path);
    }

    fclose(f);
    return 0;
}

// ================= ADD =================
int index_add(Index *idx, const char *path) {

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    void *buffer = malloc(size);
    fread(buffer, 1, size, f);
    fclose(f);

    ObjectID id;

    if (object_write(OBJ_BLOB, buffer, size, &id) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);

    // update existing entry
    for (int i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].path, path) == 0) {
            idx->entries[i].hash = id;
            return 0;
        }
    }

    // add new entry
    IndexEntry *e = &idx->entries[idx->count++];
    strcpy(e->path, path);
    e->hash = id;
    e->mode = 0100644;

    return 0;
}
