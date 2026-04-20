#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "tree.h"
#include "pes.h"
#include "index.h"

/* Helper to write a tree object to the database */
int write_tree(const TreeEntry *entries, int count, ObjectID *out_id) {
    // Standard buffer size for tree serialization
    unsigned char buffer[65536];
    int offset = 0;

    for (int i = 0; i < count; i++) {
        const TreeEntry *e = &entries[i];
        
        // 1. Copy mode (4 bytes, network byte order)
        uint32_t mode_net = htonl(e->mode);
        memcpy(buffer + offset, &mode_net, 4);
        offset += 4;

        // 2. Copy the ObjectID hash (32 bytes for SHA-256)
        // Accessing e->hash (the ObjectID) and then .hash (the array inside)
        memcpy(buffer + offset, e->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;

        // 3. Copy name (null-terminated string)
        int name_len = strlen(e->name) + 1;
        memcpy(buffer + offset, e->name, name_len);
        offset += name_len;
    }

    extern int object_write(ObjectType type, const void *data, size_t size, ObjectID *out_id);
    return object_write(OBJ_TREE, buffer, (size_t)offset, out_id);
}

/* Bridging function for Phase 4: translates Index to Tree */
int tree_write_from_index(const Index *idx, ObjectID *out_id) {
    if (!idx || idx->count == 0) return -1;

    TreeEntry entries[MAX_TREE_ENTRIES];
    for (int i = 0; i < idx->count; i++) {
        entries[i].mode = idx->entries[i].mode;
        
        // Copy hash from index entry to tree entry
        memcpy(entries[i].hash.hash, idx->entries[i].hash.hash, HASH_SIZE);
        
        // Ensure name fits and is null-terminated
        strncpy(entries[i].name, idx->entries[i].path, sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
    }

    return write_tree(entries, idx->count, out_id);
}
