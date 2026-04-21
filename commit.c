#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

// forward declarations
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);
void hash_to_hex(const ObjectID *id, char *hex_out);

// 🔴 local hex → hash (fix)
 static void hex_to_hash_local(const char *hex, ObjectID *id) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sscanf(hex + 2*i, "%2hhx", &id->hash[i]);
    }
}

// ================= PARSE =================
int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len;
    const char *p = (const char *)data;
    char hex[HASH_HEX_SIZE + 1];

    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    hex_to_hash_local(hex, &commit_out->tree);
    p = strchr(p, '\n') + 1;

    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        hex_to_hash_local(hex, &commit_out->parent);
        commit_out->has_parent = 1;
        p = strchr(p, '\n') + 1;
    } else {
        commit_out->has_parent = 0;
    }

    char author_buf[256];
    uint64_t ts;

    if (sscanf(p, "author %255[^\n]\n", author_buf) != 1) return -1;

    char *last_space = strrchr(author_buf, ' ');
    if (!last_space) return -1;

    ts = strtoull(last_space + 1, NULL, 10);
    *last_space = '\0';

    snprintf(commit_out->author, sizeof(commit_out->author), "%s", author_buf);
    commit_out->timestamp = ts;

    p = strchr(p, '\n') + 1; // author
    p = strchr(p, '\n') + 1; // committer
    p = strchr(p, '\n') + 1; // blank

    snprintf(commit_out->message, sizeof(commit_out->message), "%s", p);

    return 0;
}

// ================= SERIALIZE =================
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];

    hash_to_hex(&commit->tree, tree_hex);

    char buf[8192];
    int n = 0;

    n += snprintf(buf + n, sizeof(buf) - n, "tree %s\n", tree_hex);

    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
        n += snprintf(buf + n, sizeof(buf) - n, "parent %s\n", parent_hex);
    }

    n += snprintf(buf + n, sizeof(buf) - n,
                  "author %s %" PRIu64 "\n"
                  "committer %s %" PRIu64 "\n"
                  "\n"
                  "%s",
                  commit->author, commit->timestamp,
                  commit->author, commit->timestamp,
                  commit->message);

    *data_out = malloc(n + 1);
    if (!*data_out) return -1;

    memcpy(*data_out, buf, n + 1);
    *len_out = (size_t)n;

    return 0;
}

// ================= WALK =================
int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;

    while (1) {
        ObjectType type;
        void *raw;
        size_t raw_len;

        if (object_read(&id, &type, &raw, &raw_len) != 0) return -1;

        Commit c;
        int rc = commit_parse(raw, raw_len, &c);
        free(raw);
        if (rc != 0) return -1;

        callback(&id, &c, ctx);

        if (!c.has_parent) break;
        id = c.parent;
    }
    return 0;
}

// ================= HEAD READ =================
int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;

    char line[512];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    line[strcspn(line, "\r\n")] = '\0';

    char ref_path[512];

    if (strncmp(line, "ref: ", 5) == 0) {
        snprintf(ref_path, sizeof(ref_path), "%s/%s", PES_DIR, line + 5);

        f = fopen(ref_path, "r");
        if (!f) {
            // first commit → no parent
            return -1;
        }

        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return -1;
        }
        fclose(f);

        line[strcspn(line, "\r\n")] = '\0';
    }

    hex_to_hash_local(line, id_out);
    return 0;
}
// ================= HEAD UPDATE =================
int head_update(const ObjectID *new_commit) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;

    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);

    line[strcspn(line, "\r\n")] = '\0';

    char target_path[520];

    if (strncmp(line, "ref: ", 5) == 0) {
        snprintf(target_path, sizeof(target_path), "%s/%s", PES_DIR, line + 5);
    } else {
        snprintf(target_path, sizeof(target_path), "%s", HEAD_FILE);
    }

    char tmp_path[528];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target_path);

    f = fopen(tmp_path, "w");
    if (!f) return -1;

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);

    fprintf(f, "%s\n", hex);

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return rename(tmp_path, target_path);
}

// ================= FINAL =================
int commit_create(const char *message, ObjectID *commit_id_out) {
    printf("STEP 1: building tree\n");

    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) {
        printf("ERROR: tree_from_index failed\n");
        return -1;
    }

    printf("STEP 2: reading parent\n");

    ObjectID parent_id;
    int has_parent = (head_read(&parent_id) == 0);

    printf("STEP 3: serialize commit\n");

    Commit c;
    memset(&c, 0, sizeof(c));

    c.tree = tree_id;
    c.has_parent = has_parent;
    if (has_parent) c.parent = parent_id;

    snprintf(c.author, sizeof(c.author), "%s", pes_author());
    c.timestamp = (uint64_t)time(NULL);
    snprintf(c.message, sizeof(c.message), "%s", message);

    void *data;
    size_t len;

    if (commit_serialize(&c, &data, &len) != 0) {
        printf("ERROR: serialize failed\n");
        return -1;
    }

    printf("STEP 4: writing object\n");

    if (object_write(OBJ_COMMIT, data, len, commit_id_out) != 0) {
        printf("ERROR: object_write failed\n");
        free(data);
        return -1;
    }

    free(data);

    printf("STEP 5: updating HEAD\n");

    if (head_update(commit_id_out) != 0) {
        printf("ERROR: head_update failed\n");
        return -1;
    }

    printf("SUCCESS\n");
    return 0;
}
