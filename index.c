#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <openssl/sha.h>

#include "pes.h"
#include "index.h"

#define INDEX_PATH ".pes/index"

int object_write(ObjectType type, const void *data, size_t size, ObjectID *out_id);

int object_hash_file(const char *path, ObjectID *id) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) { fclose(f); return -1; }
    unsigned char *buf = malloc(size);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return -1; }
    SHA256(buf, (size_t)size, id->hash);
    free(buf);
    fclose(f);
    return 0;
}

IndexEntry* index_find(Index *idx, const char *path) {
    for (int i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].path, path) == 0) return &idx->entries[i];
    }
    return NULL;
}

int index_load(Index *idx) {
    if (!idx) return -1;
    idx->count = 0;
    FILE *f = fopen(INDEX_PATH, "r");
    if (!f) return (errno == ENOENT) ? 0 : -1;
    char hash_hex[HASH_HEX_SIZE + 1];
    while (idx->count < MAX_INDEX_ENTRIES) {
        int res = fscanf(f, "%o %64s %lu %u ", &idx->entries[idx->count].mode, hash_hex, &idx->entries[idx->count].mtime_sec, &idx->entries[idx->count].size);
        if (res != 4) break;
        if (fgets(idx->entries[idx->count].path, sizeof(idx->entries[idx->count].path), f)) {
            idx->entries[idx->count].path[strcspn(idx->entries[idx->count].path, "\n")] = 0;
            if (idx->entries[idx->count].path[0] == ' ') memmove(idx->entries[idx->count].path, idx->entries[idx->count].path + 1, strlen(idx->entries[idx->count].path));
        }
        hex_to_hash(hash_hex, &idx->entries[idx->count].hash); 
        idx->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *idx) {
    if (!idx) return -1;
    char temp_path[] = INDEX_PATH ".tmp";
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;
    for (int i = 0; i < idx->count; i++) {
        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&idx->entries[i].hash, hash_hex);
        fprintf(f, "%o %s %lu %u %s\n", idx->entries[i].mode, hash_hex, idx->entries[i].mtime_sec, idx->entries[i].size, idx->entries[i].path);
    }
    fclose(f);
    return rename(temp_path, INDEX_PATH);
}

int index_add(Index *idx, const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    unsigned char *content = malloc(st.st_size);
    fread(content, 1, st.st_size, f);
    fclose(f);
    ObjectID oid;
    object_write(OBJ_BLOB, content, st.st_size, &oid);
    free(content);
    IndexEntry *entry = index_find(idx, filename);
    if (!entry) {
        entry = &idx->entries[idx->count++];
        strncpy(entry->path, filename, sizeof(entry->path)-1);
    }
    entry->mode = st.st_mode;
    entry->mtime_sec = (uint64_t)st.st_mtime;
    entry->size = (uint32_t)st.st_size;
    memcpy(entry->hash.hash, oid.hash, HASH_SIZE);
    return index_save(idx);
}

int index_remove(Index *idx, const char *path) {
    for (int i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].path, path) == 0) {
            memmove(&idx->entries[i], &idx->entries[i+1], (idx->count - i - 1) * sizeof(IndexEntry));
            idx->count--;
            return index_save(idx);
        }
    }
    return -1;
}

/* Phase 3: Status Implementation */
int index_status(const Index *idx) {
    printf("Staged changes:\n");
    for (int i = 0; i < idx->count; i++) {
        printf("  staged:   %s\n", idx->entries[i].path);
    }

    printf("\nUnstaged changes:\n");
    for (int i = 0; i < idx->count; i++) {
        struct stat st;
        if (stat(idx->entries[i].path, &st) == 0) {
            if ((uint64_t)st.st_mtime != idx->entries[i].mtime_sec || (uint32_t)st.st_size != idx->entries[i].size) {
                printf("  modified: %s\n", idx->entries[i].path);
            }
        } else {
            printf("  deleted:  %s\n", idx->entries[i].path);
        }
    }

    printf("\nUntracked files:\n");
    DIR *d = opendir(".");
    struct dirent *dir;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) { // Regular files only
                if (strcmp(dir->d_name, "pes") == 0 || dir->d_name[0] == '.') continue;
                if (!index_find((Index*)idx, dir->d_name)) {
                    printf("  untracked: %s\n", dir->d_name);
                }
            }
        }
        closedir(d);
    }
    return 0;
}
