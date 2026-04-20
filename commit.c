#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pes.h"
#include "index.h"
#include "tree.h"
#include "commit.h"

#define HEAD_PATH ".pes/HEAD"

extern int object_write(ObjectType type, const void *data, size_t size, ObjectID *out_id);
extern int object_read(const ObjectID *id, ObjectType *out_type, void **out_data, size_t *out_size);
extern int tree_write_from_index(const Index *idx, ObjectID *out_id);

int get_head_id(ObjectID *id) {
    FILE *f = fopen(HEAD_PATH, "r");
    if (!f) return -1;
    char hex[HASH_HEX_SIZE + 1];
    int res = fscanf(f, "%64s", hex);
    fclose(f);
    return (res == 1) ? hex_to_hash(hex, id) : -1;
}

int set_head_id(const ObjectID *id) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    FILE *f = fopen(HEAD_PATH, "w");
    if (f) { fprintf(f, "%s\n", hex); fclose(f); }
    if (system("mkdir -p .pes/refs/heads") == 0) {
        FILE *f_ref = fopen(".pes/refs/heads/main", "w");
        if (f_ref) { fprintf(f_ref, "%s\n", hex); fclose(f_ref); }
    }
    return 0;
}

int commit_create(const char *message, ObjectID *commit_id_out) {
    Index idx;
    if (index_load(&idx) != 0 || idx.count == 0) {
        printf("error: nothing to commit\n");
        return -1;
    }
    ObjectID tid;
    if (tree_write_from_index(&idx, &tid) != 0) return -1;

    char content[4096];
    char thex[HASH_HEX_SIZE + 1];
    hash_to_hex(&tid, thex);
    int len = sprintf(content, "tree %s\n", thex);

    ObjectID pid;
    if (get_head_id(&pid) == 0) {
        char phex[HASH_HEX_SIZE + 1];
        hash_to_hex(&pid, phex);
        len += sprintf(content + len, "parent %s\n", phex);
    }
    len += sprintf(content + len, "author %ld\n\n%s\n", (long)time(NULL), message);

    ObjectID cid;
    if (object_write(OBJ_COMMIT, content, strlen(content), &cid) != 0) return -1;
    set_head_id(&cid);
    if (commit_id_out) memcpy(commit_id_out->hash, cid.hash, HASH_SIZE);
    
    char chex[HASH_HEX_SIZE + 1];
    hash_to_hex(&cid, chex);
    printf("[main %s] %s\n", chex, message);
    return 0;
}

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID curr;
    if (get_head_id(&curr) != 0) return 0;

    while (1) {
        char *raw_data; 
        size_t sz; 
        ObjectType t;
        
        if (object_read(&curr, &t, (void**)&raw_data, &sz) != 0) break;

        Commit cm;
        char tree_hex[HASH_HEX_SIZE + 1];
        
        // Parse tree - Updated to use .tree to match your struct
        if (sscanf(raw_data, "tree %64s", tree_hex) == 1) {
            hex_to_hash(tree_hex, &cm.tree);
        }
        
        // Parse message
        char *msg_ptr = strstr(raw_data, "\n\n");
        if (msg_ptr) {
            strncpy(cm.message, msg_ptr + 2, sizeof(cm.message) - 1);
            cm.message[sizeof(cm.message) - 1] = '\0';
        }

        // Execute callback
        callback(&curr, &cm, ctx);

        // Find parent to continue loop
        char *p = strstr(raw_data, "parent ");
        if (p) {
            char parent_hex[HASH_HEX_SIZE + 1];
            sscanf(p + 7, "%64s", parent_hex);
            hex_to_hash(parent_hex, &curr);
            free(raw_data);
        } else {
            free(raw_data);
            break;
        }
    }
    return 0;
}
