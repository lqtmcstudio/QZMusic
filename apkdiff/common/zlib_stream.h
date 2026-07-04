/*
 * zlib_stream.h - zlib compression callbacks for bsdiff/bspatch streams
 */

#ifndef ZLIB_STREAM_H
#define ZLIB_STREAM_H

#include <stdio.h>
#include <stdint.h>
#include <zlib.h>
#include "bsdiff.h"
#include "bspatch.h"

#define ZLIB_STREAM_BUF_SIZE 8192

/* ---- Write context (used by bsdiff in gen.exe) ---- */

typedef struct {
    FILE*    fp;
    z_stream zs;
    uint8_t  out_buf[ZLIB_STREAM_BUF_SIZE];
} zlib_write_ctx;

/* Initialize zlib deflate write context. Must be called before bsdiff(). */
int zlib_write_init(zlib_write_ctx* ctx, FILE* fp);

/* Flush and clean up. Must be called after bsdiff() completes. */
int zlib_write_finish(zlib_write_ctx* ctx);

/* bsdiff_stream.write callback */
int zlib_write_cb(struct bsdiff_stream* stream, const void* buffer, int size);

/* ---- Read context (used by bspatch in JNI) ---- */

typedef struct {
    const uint8_t* data;
    size_t         data_len;
    z_stream       zs;
    int            inited;
} zlib_read_ctx;

/* Initialize zlib inflate read context from compressed data buffer. */
int zlib_read_init(zlib_read_ctx* ctx, const uint8_t* data, size_t len);

/* Clean up zlib read context. */
void zlib_read_cleanup(zlib_read_ctx* ctx);

/* bspatch_stream.read callback */
int zlib_read_cb(const struct bspatch_stream* stream, void* buffer, int length);

/* ---- Convenience setup functions ---- */

/* Fill a bsdiff_stream struct for zlib-compressed output to fp.
 * Caller must call zlib_write_finish() on ctx after bsdiff() completes. */
void zlib_setup_bsdiff_stream(struct bsdiff_stream* stream, zlib_write_ctx* ctx, FILE* fp);

/* Fill a bspatch_stream struct for zlib-decompressed input from data buffer.
 * Caller must call zlib_read_cleanup() on ctx when done. */
void zlib_setup_bspatch_stream(struct bspatch_stream* stream, zlib_read_ctx* ctx,
                               const uint8_t* data, size_t len);

#endif /* ZLIB_STREAM_H */
