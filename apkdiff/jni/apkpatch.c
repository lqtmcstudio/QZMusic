/*
 * jni/apkpatch.c - Android JNI: apply APK patch
 *
 * Java class: love.qz.apkpatcher.Patcher
 * Native method: applyPatch(String oldApkPath, String patchPath, String newApkPath)
 *
 * Reads the patch file, opens the old APK, and produces a new APK
 * by applying per-file bsdiff patches, copying unchanged files,
 * and adding new files.
 */

#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unzip.h"
#include "zip.h"
#include "bspatch.h"
#include "common/patch_format.h"
#include "common/zlib_stream.h"

#define LOG_TAG "ApkPatch"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Read a file entry from a ZIP into a malloc'd buffer.
 * Returns 0 on success, -1 on failure. Caller must free *out_data. */
static int read_zip_entry(unzFile zf, const char* path,
                          uint8_t** out_data, size_t* out_size)
{
    if (unzLocateFile(zf, path, 1) != UNZ_OK)
        return -1;

    unz_file_info fi;
    if (unzGetCurrentFileInfo(zf, &fi, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK)
        return -1;

    if (unzOpenCurrentFile(zf) != UNZ_OK)
        return -1;

    uint8_t* data = NULL;
    if (fi.uncompressed_size > 0) {
        data = (uint8_t*)malloc(fi.uncompressed_size);
        if (!data) {
            unzCloseCurrentFile(zf);
            return -1;
        }
        int rd = unzReadCurrentFile(zf, data, (unsigned)fi.uncompressed_size);
        if (rd != (int)fi.uncompressed_size) {
            free(data);
            unzCloseCurrentFile(zf);
            return -1;
        }
    }

    unzCloseCurrentFile(zf);
    *out_data = data;
    *out_size = fi.uncompressed_size;
    return 0;
}

/* Write data to a new ZIP entry using deflate compression.
 * Returns 0 on success, -1 on failure. */
static int write_zip_entry(zipFile zf_out, const char* path,
                           const uint8_t* data, size_t size)
{
    zip_fileinfo zi;
    memset(&zi, 0, sizeof(zi));

    int method = (size > 0) ? Z_DEFLATED : 0;
    if (zipOpenNewFileInZip(zf_out, path, &zi,
                            NULL, 0, NULL, 0, NULL,
                            method, Z_DEFAULT_COMPRESSION) != ZIP_OK)
        return -1;

    if (size > 0) {
        if (zipWriteInFileInZip(zf_out, data, (unsigned)size) != ZIP_OK) {
            zipCloseFileInZip(zf_out);
            return -1;
        }
    }

    zipCloseFileInZip(zf_out);
    return 0;
}

/* zlib decompress a buffer. Returns malloc'd buffer or NULL. */
static uint8_t* zlib_decompress_buffer(const uint8_t* comp_data, uint32_t comp_size,
                                       int64_t orig_size)
{
    uint8_t* out = (uint8_t*)malloc(orig_size + 1);
    if (!out) return NULL;

    zlib_read_ctx rctx;
    if (zlib_read_init(&rctx, comp_data, comp_size) != 0) {
        free(out);
        return NULL;
    }

    rctx.zs.next_out = (Bytef*)out;
    rctx.zs.avail_out = (uInt)orig_size;

    int ret;
    do {
        ret = inflate(&rctx.zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            zlib_read_cleanup(&rctx);
            free(out);
            return NULL;
        }
    } while (ret != Z_STREAM_END);

    zlib_read_cleanup(&rctx);
    return out;
}

/* Apply a single PATCH entry: pass compressed patch data to bspatch via zlib stream. */
static int apply_patch_entry(const uint8_t* old_data, size_t old_size,
                             const uint8_t* comp_patch, uint32_t comp_patch_size,
                             int64_t new_size,
                             uint8_t** out_new_data)
{
    uint8_t* new_data = (uint8_t*)malloc(new_size + 1);
    if (!new_data) return -1;

    /* Pass compressed data directly to bspatch; zlib_read_cb handles decompression */
    zlib_read_ctx rctx;
    struct bspatch_stream stream;
    zlib_setup_bspatch_stream(&stream, &rctx, comp_patch, comp_patch_size);

    int bspatch_ret = bspatch(old_data, (int64_t)old_size,
                               new_data, new_size, &stream);
    zlib_read_cleanup(&rctx);

    if (bspatch_ret != 0) {
        free(new_data);
        return -1;
    }

    *out_new_data = new_data;
    return 0;
}

/*
 * JNI: com.apkdiff.Patcher.applyPatch
 * Returns: 0 = success, -1 = error
 */
JNIEXPORT jint JNICALL
Java_love_qz_apkpatcher_Patcher_applyPatch(JNIEnv* env, jclass clazz,
                                     jstring jOldApkPath,
                                     jstring jPatchPath,
                                     jstring jNewApkPath)
{
    (void)clazz;

    const char* old_apk_path = (*env)->GetStringUTFChars(env, jOldApkPath, NULL);
    const char* patch_path   = (*env)->GetStringUTFChars(env, jPatchPath, NULL);
    const char* new_apk_path = (*env)->GetStringUTFChars(env, jNewApkPath, NULL);

    int result = -1;
    FILE* patch_fp = NULL;
    unzFile zf_old = NULL;
    zipFile zf_new = NULL;

    LOGI("applyPatch: %s + %s -> %s", old_apk_path, patch_path, new_apk_path);

    /* Open patch file */
    patch_fp = fopen(patch_path, "rb");
    if (!patch_fp) {
        LOGE("Cannot open patch file: %s", patch_path);
        goto done;
    }

    /* Read and validate patch header */
    uint32_t entry_count = 0;
    if (patch_read_header(patch_fp, &entry_count) != 0) {
        LOGE("Invalid patch file format");
        goto done;
    }
    LOGI("Patch: %u entries", entry_count);

    /* Open old APK */
    zf_old = unzOpen(old_apk_path);
    if (!zf_old) {
        LOGE("Cannot open old APK: %s", old_apk_path);
        goto done;
    }

    /* Create new APK */
    zf_new = zipOpen(new_apk_path, APPEND_STATUS_CREATE);
    if (!zf_new) {
        LOGE("Cannot create new APK: %s", new_apk_path);
        goto done;
    }

    /* Process each entry */
    for (uint32_t i = 0; i < entry_count; i++) {
        char* path = NULL;
        uint8_t type;
        int64_t old_size, new_size;
        uint32_t comp_size;

        if (patch_read_entry_header(patch_fp, &path, &type,
                                     &old_size, &new_size, &comp_size) != 0) {
            LOGE("Error reading entry %u", i);
            goto done;
        }

        uint8_t* comp_data = NULL;
        if (comp_size > 0) {
            comp_data = (uint8_t*)malloc(comp_size);
            if (!comp_data || fread(comp_data, 1, comp_size, patch_fp) != comp_size) {
                LOGE("Error reading compressed data for %s", path);
                free(comp_data);
                free(path);
                goto done;
            }
        }

        int entry_ok = 0;
        switch (type) {
        case ENTRY_TYPE_COPY: {
            /* Copy unchanged file from old APK */
            uint8_t* data = NULL;
            size_t size = 0;
            if (read_zip_entry(zf_old, path, &data, &size) == 0) {
                entry_ok = (write_zip_entry(zf_new, path, data, size) == 0);
                free(data);
            } else {
                LOGE("COPY: file not found in old APK: %s", path);
            }
            break;
        }

        case ENTRY_TYPE_NEW: {
            /* Decompress and write new file */
            uint8_t* data = zlib_decompress_buffer(comp_data, comp_size, new_size);
            if (data) {
                entry_ok = (write_zip_entry(zf_new, path, data, (size_t)new_size) == 0);
                free(data);
            } else {
                LOGE("NEW: decompress failed for %s", path);
            }
            break;
        }

        case ENTRY_TYPE_PATCH: {
            /* Read old file, apply patch, write new file */
            uint8_t* old_data = NULL;
            size_t old_file_size = 0;
            if (read_zip_entry(zf_old, path, &old_data, &old_file_size) != 0) {
                LOGE("PATCH: file not found in old APK: %s", path);
                break;
            }

            uint8_t* new_data = NULL;
            if (apply_patch_entry(old_data, old_file_size,
                                  comp_data, comp_size,
                                  new_size, &new_data) == 0) {
                entry_ok = (write_zip_entry(zf_new, path, new_data, (size_t)new_size) == 0);
                free(new_data);
            } else {
                LOGE("PATCH: bspatch failed for %s", path);
            }

            free(old_data);
            break;
        }

        default:
            LOGE("Unknown entry type %d for %s", type, path);
            break;
        }

        free(comp_data);
        free(path);

        if (!entry_ok) {
            LOGE("Failed to process entry %u", i);
            goto done;
        }
    }

    result = 0;
    LOGI("Patch applied successfully -> %s", new_apk_path);

done:
    if (zf_new) zipClose(zf_new, NULL);
    if (zf_old) unzClose(zf_old);
    if (patch_fp) fclose(patch_fp);

    (*env)->ReleaseStringUTFChars(env, jOldApkPath, old_apk_path);
    (*env)->ReleaseStringUTFChars(env, jPatchPath, patch_path);
    (*env)->ReleaseStringUTFChars(env, jNewApkPath, new_apk_path);

    return result;
}
