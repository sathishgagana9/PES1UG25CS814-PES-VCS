
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/evp.h>

// =======================
// HASH → HEX
// =======================
void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

// =======================
// COMPUTE HASH
// =======================
void compute_hash(const void *data, size_t size, ObjectID *id_out) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, size);
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, id_out->hash, &len);
    EVP_MD_CTX_free(ctx);
}

// =======================
// OBJECT PATH
// =======================
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, ".pes/objects/%.2s/%s", hex, hex + 2);
}

// =======================
// OBJECT EXISTS
// =======================
int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

// =======================
// WRITE OBJECT (FIXED)
// =======================
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    if (!data || !id_out) return -1;

    // create header: "blob <size>\0"
    char header[64];
    const char *type_str = (type == OBJ_BLOB) ? "blob" : "unknown";
    int header_len = sprintf(header, "%s %zu", type_str, len) + 1;

    size_t total = header_len + len;
    char *buffer = malloc(total);

    memcpy(buffer, header, header_len);
    memcpy(buffer + header_len, data, len);

    // hash full object
    compute_hash(buffer, total, id_out);

    char path[512];
    object_path(id_out, path, sizeof(path));

    mkdir(".pes", 0777);
    mkdir(".pes/objects", 0777);

    char dir[512];
    strcpy(dir, path);
    char *p = strrchr(dir, '/');
    if (p) {
        *p = '\0';
        mkdir(dir, 0777);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buffer);
        return -1;
    }

    if (fwrite(buffer, 1, total, f) != total) {
        fclose(f);
        free(buffer);
        return -1;
    }

    fclose(f);
    free(buffer);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    if (!id || !data_out || !len_out || !type_out) return -1;

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t total = ftell(f);
    rewind(f);

    char *buffer = malloc(total);
    if (!buffer) {
        fclose(f);
        return -1;
    }

    if (fread(buffer, 1, total, f) != total) {
        free(buffer);
        fclose(f);
        return -1;
    }

    fclose(f);

    // 🔴 integrity check FIRST
    ObjectID new_id;
    compute_hash(buffer, total, &new_id);

    if (memcmp(new_id.hash, id->hash, HASH_SIZE) != 0) {
        free(buffer);
        return -1;
    }

    // 🔴 parse header safely
    char *null_pos = memchr(buffer, '\0', total);
    if (!null_pos) {
        free(buffer);
        return -1;
    }

    // 🔴 determine type
    if (strncmp(buffer, "blob", 4) == 0) {
        *type_out = OBJ_BLOB;
    } else {
        free(buffer);
        return -1;
    }

    char *data = null_pos + 1;
    size_t data_len = total - (data - buffer);

    char *result = malloc(data_len);
    memcpy(result, data, data_len);

    free(buffer);

    *data_out = result;
    *len_out = data_len;

    return 0;
}
