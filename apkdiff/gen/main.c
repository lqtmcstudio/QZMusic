/*
 * gen/main.c - APK patch generation tool
 *
 * Usage: gen.exe <oldApk> <newApk> <output>
 *
 * Reads two APK (ZIP) files, compares their entries, and generates
 * a binary patch file that can be applied by the JNI bspatch module.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unzip.h"
#include "bsdiff.h"
#include "common/patch_format.h"
#include "common/zlib_stream.h"

/* ---- ZIP entry storage ---- */

typedef struct {
    char*    path;
    uint8_t* data;
    size_t   size;
    uLong    crc;
} file_entry;

typedef struct {
    file_entry* entries;
    int         count;
    int         capacity;
} file_set;

static void file_set_init(file_set* set)
{
    set->entries = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void file_set_free(file_set* set)
{
    for (int i = 0; i < set->count; i++) {
        free(set->entries[i].path);
        free(set->entries[i].data);
    }
    free(set->entries);
    set->entries = NULL;
    set->count = 0;
}

static void file_set_add(file_set* set, const char* path, uint8_t* data, size_t size, uLong crc)
{
    if (set->count >= set->capacity) {
        set->capacity = set->capacity ? set->capacity * 2 : 256;
        set->entries = (file_entry*)realloc(set->entries, set->capacity * sizeof(file_entry));
    }
    set->entries[set->count].path = strdup(path);
    set->entries[set->count].data = data;
    set->entries[set->count].size = size;
    set->entries[set->count].crc = crc;
    set->count++;
}

static int path_cmp(const void* a, const void* b)
{
    return strcmp(((const file_entry*)a)->path, ((const file_entry*)b)->path);
}

static void file_set_sort(file_set* set)
{
    if (set->count > 0)
        qsort(set->entries, set->count, sizeof(file_entry), path_cmp);
}

/* Binary search by path (set must be sorted) */
static file_entry* file_set_find(file_set* set, const char* path)
{
    int lo = 0, hi = set->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(set->entries[mid].path, path);
        if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid - 1;
        else return &set->entries[mid];
    }
    return NULL;
}

/* ---- Read all entries from a ZIP/APK into a file_set ---- */

static int read_zip_entries(const char* path, file_set* set)
{
    unzFile zf = unzOpen(path);
    if (!zf) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    unz_global_info gi;
    if (unzGetGlobalInfo(zf, &gi) != UNZ_OK) {
        fprintf(stderr, "Error: unzGetGlobalInfo failed\n");
        unzClose(zf);
        return -1;
    }

    if (unzGoToFirstFile(zf) != UNZ_OK) {
        unzClose(zf);
        return -1;
    }

    do {
        unz_file_info fi;
        char filename[1024];

        if (unzGetCurrentFileInfo(zf, &fi, filename, sizeof(filename),
                                  NULL, 0, NULL, 0) != UNZ_OK)
            continue;

        /* Skip directories */
        size_t len = strlen(filename);
        if (len > 0 && filename[len - 1] == '/')
            continue;

        if (unzOpenCurrentFile(zf) != UNZ_OK)
            continue;

        uint8_t* data = NULL;
        if (fi.uncompressed_size > 0) {
            data = (uint8_t*)malloc(fi.uncompressed_size + 1);
            if (!data) {
                unzCloseCurrentFile(zf);
                continue;
            }
            int read_bytes = unzReadCurrentFile(zf, data, (unsigned)fi.uncompressed_size);
            if (read_bytes != (int)fi.uncompressed_size) {
                fprintf(stderr, "Warning: short read on %s (%d/%lu)\n",
                        filename, read_bytes, fi.uncompressed_size);
                free(data);
                unzCloseCurrentFile(zf);
                continue;
            }
        }

        unzCloseCurrentFile(zf);
        file_set_add(set, filename, data, fi.uncompressed_size, fi.crc);

    } while (unzGoToNextFile(zf) == UNZ_OK);

    unzClose(zf);
    file_set_sort(set);
    return 0;
}

/* ---- zlib compress a buffer, returns malloc'd buffer and sets *out_size ---- */

static uint8_t* zlib_compress_buffer(const uint8_t* data, size_t in_size, uint32_t* out_size)
{
    uLongf dest_len = compressBound((uLong)in_size);
    uint8_t* dest = (uint8_t*)malloc(dest_len);
    if (!dest) return NULL;

    if (compress2(dest, &dest_len, data, (uLong)in_size, Z_BEST_COMPRESSION) != Z_OK) {
        free(dest);
        return NULL;
    }

    *out_size = (uint32_t)dest_len;
    return dest;
}

/* ---- Write a COPY entry (unchanged file) ---- */

static int write_copy_entry(FILE* patch, const char* path)
{
    return patch_write_entry_header(patch, path,
        ENTRY_TYPE_COPY, 0, 0, 0);
}

/* ---- Write a NEW entry (file only in new APK) ---- */

static int write_new_entry(FILE* patch, const char* path,
                           const uint8_t* data, size_t size)
{
    uint32_t comp_size = 0;
    uint8_t* comp_data = zlib_compress_buffer(data, size, &comp_size);
    if (!comp_data) return -1;

    int ret = patch_write_entry_header(patch, path,
        ENTRY_TYPE_NEW, 0, (int64_t)size, comp_size);
    if (ret == 0) {
        if (fwrite(comp_data, 1, comp_size, patch) != comp_size)
            ret = -1;
    }

    free(comp_data);
    return ret;
}

/* ---- Write a PATCH entry (modified file, bsdiff + zlib) ---- */

static int write_patch_entry(FILE* patch, const char* path,
                             const uint8_t* old_data, size_t old_size,
                             const uint8_t* new_data, size_t new_size)
{
    /* Use tmpfile to capture bsdiff output via zlib write callbacks */
    FILE* tmp = tmpfile();
    if (!tmp) {
        fprintf(stderr, "Error: tmpfile() failed\n");
        return -1;
    }

    zlib_write_ctx wctx;
    struct bsdiff_stream stream;
    zlib_setup_bsdiff_stream(&stream, &wctx, tmp);

    int bsdiff_ret = bsdiff(old_data, (int64_t)old_size,
                            new_data, (int64_t)new_size, &stream);
    zlib_write_finish(&wctx);

    if (bsdiff_ret != 0) {
        fprintf(stderr, "Error: bsdiff failed for %s\n", path);
        fclose(tmp);
        return -1;
    }

    /* Read compressed patch data from temp file */
    long comp_size = ftell(tmp);
    if (comp_size < 0) {
        fclose(tmp);
        return -1;
    }

    fseek(tmp, 0, SEEK_SET);
    uint8_t* comp_data = (uint8_t*)malloc(comp_size);
    if (!comp_data) {
        fclose(tmp);
        return -1;
    }

    if ((long)fread(comp_data, 1, comp_size, tmp) != comp_size) {
        free(comp_data);
        fclose(tmp);
        return -1;
    }
    fclose(tmp);

    /* Write entry header + compressed patch data */
    int ret = patch_write_entry_header(patch, path,
        ENTRY_TYPE_PATCH, (int64_t)old_size, (int64_t)new_size, (uint32_t)comp_size);
    if (ret == 0) {
        if (fwrite(comp_data, 1, comp_size, patch) != (size_t)comp_size)
            ret = -1;
    }

    free(comp_data);
    return ret;
}

/* ---- main ---- */

int main(int argc, char* argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <oldApk> <newApk> <output>\n", argv[0]);
        return 1;
    }

    const char* old_path = argv[1];
    const char* new_path = argv[2];
    const char* out_path = argv[3];

    /* Read both APKs */
    file_set old_set, new_set;
    file_set_init(&old_set);
    file_set_init(&new_set);

    printf("Reading old APK: %s\n", old_path);
    if (read_zip_entries(old_path, &old_set) != 0)
        return 1;
    printf("  %d entries\n", old_set.count);

    printf("Reading new APK: %s\n", new_path);
    if (read_zip_entries(new_path, &new_set) != 0) {
        file_set_free(&old_set);
        return 1;
    }
    printf("  %d entries\n", new_set.count);

    /* Open output patch file */
    FILE* patch = fopen(out_path, "wb");
    if (!patch) {
        fprintf(stderr, "Error: cannot create %s\n", out_path);
        file_set_free(&old_set);
        file_set_free(&new_set);
        return 1;
    }

    /* Write placeholder header (entry_count will be filled later) */
    patch_write_header(patch, 0);

    uint32_t entry_count = 0;
    int stats_copy = 0, stats_patch = 0, stats_new = 0;

    /* Process all entries in the new APK */
    for (int i = 0; i < new_set.count; i++) {
        file_entry* ne = &new_set.entries[i];
        file_entry* oe = file_set_find(&old_set, ne->path);

        if (!oe) {
            /* New file - only in new APK */
            printf("  NEW   %s (%zu bytes)\n", ne->path, ne->size);
            if (write_new_entry(patch, ne->path, ne->data, ne->size) != 0) {
                fprintf(stderr, "Error writing NEW entry: %s\n", ne->path);
                goto cleanup;
            }
            entry_count++;
            stats_new++;
        }
        else if (oe->size == ne->size && oe->crc == ne->crc) {
            /* Unchanged - mark as COPY */
            if (write_copy_entry(patch, ne->path) != 0) {
                fprintf(stderr, "Error writing COPY entry: %s\n", ne->path);
                goto cleanup;
            }
            entry_count++;
            stats_copy++;
        }
        else {
            /* Modified - generate bsdiff patch */
            printf("  PATCH %s (%zu -> %zu bytes)\n", ne->path, oe->size, ne->size);
            if (write_patch_entry(patch, ne->path,
                                  oe->data, oe->size,
                                  ne->data, ne->size) != 0) {
                fprintf(stderr, "Error writing PATCH entry: %s\n", ne->path);
                goto cleanup;
            }
            entry_count++;
            stats_patch++;
        }
    }

    /* Seek back and write actual entry count */
    fseek(patch, (long)APKDIFF_MAGIC_SIZE, SEEK_SET);
    fwrite(&entry_count, sizeof(uint32_t), 1, patch);

    printf("\nDone! Patch: %s\n", out_path);
    printf("  Entries: %u total (%d copy, %d patch, %d new)\n",
           entry_count, stats_copy, stats_patch, stats_new);

cleanup:
    fclose(patch);
    file_set_free(&old_set);
    file_set_free(&new_set);
    return 0;
}
