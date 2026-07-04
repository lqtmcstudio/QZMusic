/*
 * zlib_stream.c - zlib compression callbacks for bsdiff/bspatch streams
 */

#include "zlib_stream.h"
#include <string.h>
#include <stdlib.h>

/* ============ Write (deflate) ============ */

static int zlib_flush_output(zlib_write_ctx* ctx)
{
    int ret;
    do {
        ctx->zs.next_out = ctx->out_buf;
        ctx->zs.avail_out = ZLIB_STREAM_BUF_SIZE;
        ret = deflate(&ctx->zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR)
            return -1;

        size_t have = ZLIB_STREAM_BUF_SIZE - ctx->zs.avail_out;
        if (have > 0) {
            if (fwrite(ctx->out_buf, 1, have, ctx->fp) != have)
                return -1;
        }
    } while (ctx->zs.avail_out == 0);

    return 0;
}

int zlib_write_init(zlib_write_ctx* ctx, FILE* fp)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->fp = fp;

    ctx->zs.zalloc = Z_NULL;
    ctx->zs.zfree = Z_NULL;
    ctx->zs.opaque = Z_NULL;

    if (deflateInit(&ctx->zs, Z_BEST_COMPRESSION) != Z_OK)
        return -1;

    return 0;
}

int zlib_write_cb(struct bsdiff_stream* stream, const void* buffer, int size)
{
    zlib_write_ctx* ctx = (zlib_write_ctx*)stream->opaque;

    ctx->zs.next_in = (Bytef*)(uintptr_t)buffer;
    ctx->zs.avail_in = (uInt)size;

    while (ctx->zs.avail_in > 0) {
        ctx->zs.next_out = ctx->out_buf;
        ctx->zs.avail_out = ZLIB_STREAM_BUF_SIZE;

        int ret = deflate(&ctx->zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR)
            return -1;

        size_t have = ZLIB_STREAM_BUF_SIZE - ctx->zs.avail_out;
        if (have > 0) {
            if (fwrite(ctx->out_buf, 1, have, ctx->fp) != have)
                return -1;
        }
    }

    return 0;
}

int zlib_write_finish(zlib_write_ctx* ctx)
{
    int ret;
    do {
        ctx->zs.next_out = ctx->out_buf;
        ctx->zs.avail_out = ZLIB_STREAM_BUF_SIZE;
        ret = deflate(&ctx->zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR)
            return -1;

        size_t have = ZLIB_STREAM_BUF_SIZE - ctx->zs.avail_out;
        if (have > 0) {
            if (fwrite(ctx->out_buf, 1, have, ctx->fp) != have)
                return -1;
        }
    } while (ret != Z_STREAM_END);

    deflateEnd(&ctx->zs);
    return 0;
}

/* ============ Read (inflate) ============ */

int zlib_read_init(zlib_read_ctx* ctx, const uint8_t* data, size_t len)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->data = data;
    ctx->data_len = len;

    ctx->zs.zalloc = Z_NULL;
    ctx->zs.zfree = Z_NULL;
    ctx->zs.opaque = Z_NULL;
    ctx->zs.next_in = (Bytef*)(uintptr_t)data;
    ctx->zs.avail_in = (uInt)len;

    if (inflateInit(&ctx->zs) != Z_OK)
        return -1;

    ctx->inited = 1;
    return 0;
}

void zlib_read_cleanup(zlib_read_ctx* ctx)
{
    if (ctx->inited) {
        inflateEnd(&ctx->zs);
        ctx->inited = 0;
    }
}

int zlib_read_cb(const struct bspatch_stream* stream, void* buffer, int length)
{
    zlib_read_ctx* ctx = (zlib_read_ctx*)stream->opaque;

    ctx->zs.next_out = (Bytef*)buffer;
    ctx->zs.avail_out = (uInt)length;

    while (ctx->zs.avail_out > 0) {
        if (ctx->zs.avail_in == 0)
            return -1; /* ran out of compressed data */

        int ret = inflate(&ctx->zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            if (ctx->zs.avail_out > 0)
                return -1; /* needed more data but stream ended */
            break;
        }
        if (ret != Z_OK)
            return -1;
    }

    return 0;
}

/* ============ Convenience setup ============ */

void zlib_setup_bsdiff_stream(struct bsdiff_stream* stream, zlib_write_ctx* ctx, FILE* fp)
{
    zlib_write_init(ctx, fp);
    stream->opaque = ctx;
    stream->malloc = malloc;
    stream->free = free;
    stream->write = zlib_write_cb;
}

void zlib_setup_bspatch_stream(struct bspatch_stream* stream, zlib_read_ctx* ctx,
                               const uint8_t* data, size_t len)
{
    zlib_read_init(ctx, data, len);
    stream->opaque = ctx;
    stream->read = zlib_read_cb;
}
