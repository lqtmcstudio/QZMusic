/*
 * patch_format.h - APK diff patch file format definitions
 *
 * Patch file layout:
 *   [16 bytes] Magic "APKDIFF/1.0\0\0\0"
 *   [4 bytes]  uint32_t entry_count
 *   [entries...]
 *
 * Each entry:
 *   [2 bytes]  uint16_t path_len
 *   [N bytes]  path (no null terminator)
 *   [1 byte]   type (0=patch, 1=new_file, 2=copy)
 *   [8 bytes]  int64_t old_size  (0 for new_file and copy)
 *   [8 bytes]  int64_t new_size  (0 for copy)
 *   [4 bytes]  uint32_t compressed_size (0 for copy)
 *   [N bytes]  compressed_data (only for patch and new_file)
 *
 * Entry types:
 *   PATCH (0): bsdiff patch data compressed with zlib
 *   NEW   (1): full new file content compressed with zlib
 *   COPY  (2): unchanged file, copy from old APK (no data follows)
 */

#ifndef PATCH_FORMAT_H
#define PATCH_FORMAT_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define APKDIFF_MAGIC      "APKDIFF/1.0\0\0\0"
#define APKDIFF_MAGIC_SIZE 16
#define APKDIFF_VERSION    1

#define ENTRY_TYPE_PATCH   0
#define ENTRY_TYPE_NEW     1
#define ENTRY_TYPE_COPY    2

#pragma pack(push, 1)

typedef struct {
    char     magic[APKDIFF_MAGIC_SIZE];
    uint32_t entry_count;
} patch_header_t;

typedef struct {
    uint16_t path_len;
    uint8_t  type;
    int64_t  old_size;
    int64_t  new_size;
    uint32_t compressed_size;
} patch_entry_header_t;

#pragma pack(pop)

/* Write the patch file header */
static inline int patch_write_header(FILE* fp, uint32_t entry_count)
{
    patch_header_t hdr;
    memcpy(hdr.magic, APKDIFF_MAGIC, APKDIFF_MAGIC_SIZE);
    hdr.entry_count = entry_count;
    return (fwrite(&hdr, sizeof(hdr), 1, fp) == 1) ? 0 : -1;
}

/* Read and validate patch file header */
static inline int patch_read_header(FILE* fp, uint32_t* entry_count)
{
    patch_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1)
        return -1;
    if (memcmp(hdr.magic, APKDIFF_MAGIC, APKDIFF_MAGIC_SIZE) != 0)
        return -1;
    *entry_count = hdr.entry_count;
    return 0;
}

/* Write a single entry header (call before writing compressed data) */
static inline int patch_write_entry_header(FILE* fp, const char* path,
    uint8_t type, int64_t old_size, int64_t new_size, uint32_t compressed_size)
{
    patch_entry_header_t eh;
    uint16_t path_len = (uint16_t)strlen(path);

    eh.path_len = path_len;
    eh.type = type;
    eh.old_size = old_size;
    eh.new_size = new_size;
    eh.compressed_size = compressed_size;

    if (fwrite(&eh, sizeof(eh), 1, fp) != 1) return -1;
    if (path_len > 0) {
        if (fwrite(path, 1, path_len, fp) != path_len) return -1;
    }
    return 0;
}

/* Read a single entry header. Caller must free *out_path. */
static inline int patch_read_entry_header(FILE* fp, char** out_path,
    uint8_t* type, int64_t* old_size, int64_t* new_size, uint32_t* compressed_size)
{
    patch_entry_header_t eh;
    char* path;

    if (fread(&eh, sizeof(eh), 1, fp) != 1)
        return -1;

    path = (char*)malloc(eh.path_len + 1);
    if (!path) return -1;

    if (eh.path_len > 0) {
        if (fread(path, 1, eh.path_len, fp) != eh.path_len) {
            free(path);
            return -1;
        }
    }
    path[eh.path_len] = '\0';

    *out_path = path;
    *type = eh.type;
    *old_size = eh.old_size;
    *new_size = eh.new_size;
    *compressed_size = eh.compressed_size;
    return 0;
}

#endif /* PATCH_FORMAT_H */
