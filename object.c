#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/sha.h>

#include "pes.h"

// ---------------- HASH UTILS ----------------

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + (i * 2), "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        if (sscanf(hex + (i * 2), "%2hhx", &id_out->hash[i]) != 1)
            return -1;
    }
    return 0;
}

// ---------------- PATH ----------------

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, ".pes/objects/%.2s/%s", hex, hex + 2);
}

// ---------------- TYPE CONVERSION ----------------

const char* type_to_str(ObjectType type) {
    switch (type) {
        case OBJ_BLOB: return "blob";
        case OBJ_TREE: return "tree";
        case OBJ_COMMIT: return "commit";
        default: return NULL;
    }
}

// ---------------- WRITE ----------------

int object_write(ObjectType type, const void *data, size_t size, ObjectID *out_id) {
    const char *type_str = type_to_str(type);
    if (!type_str) return -1;

    char header[64];
    int len = sprintf(header, "%s %zu", type_str, size);

    size_t total_size = len + 1 + size;

    unsigned char *buf = malloc(total_size);
    if (!buf) return -1;

    memcpy(buf, header, len);
    buf[len] = '\0';
    memcpy(buf + len + 1, data, size);

    // compute hash
    SHA256(buf, total_size, out_id->hash);

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(out_id, hex);

    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);

    char dir[256];
    sprintf(dir, ".pes/objects/%.2s", hex);
    mkdir(dir, 0755);

    char path[512];
    // ✅ FIX: use snprintf instead of sprintf
    snprintf(path, sizeof(path), "%s/%s", dir, hex + 2);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return -1;
    }

    fwrite(buf, 1, total_size, f);
    fclose(f);
    free(buf);

    return 0;
}

// ---------------- READ ----------------

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *size_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    unsigned char *buf = malloc(file_size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    // ✅ FIX: check fread
    if (fread(buf, 1, file_size, f) != (size_t)file_size) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);

    // verify hash
    unsigned char hash[HASH_SIZE];
    SHA256(buf, file_size, hash);

    if (memcmp(hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    char *null_pos = memchr(buf, '\0', file_size);
    if (!null_pos) {
        free(buf);
        return -1;
    }

    char type_str[16];
    if (sscanf((char *)buf, "%15s %zu", type_str, size_out) != 2) {
        free(buf);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0)
        *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0)
        *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0)
        *type_out = OBJ_COMMIT;
    else {
        free(buf);
        return -1;
    }

    *data_out = malloc(*size_out);
    memcpy(*data_out, null_pos + 1, *size_out);

    free(buf);
    return 0;
}

// ---------------- EXISTS ----------------

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
