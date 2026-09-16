/*
 * fcachefix - one-time fix of the OWB fontconfig cache checksum
 *
 * On AROS, fontconfig decides that a directory cache file is still valid by
 * comparing the 'checksum' field stored in the cache header with the
 * modification time (in seconds) of the font directory itself. For the OWB
 * that ships in the AROS ISO (built against fontconfig 2.11.0) this is:
 *
 *     FcCacheTimeValid: cache->checksum == (int) dir_stat->st_mtime
 *
 * (see FcCacheTimeValid()/FcDirCacheValidateHelper() in fontconfig 2.11.0's
 * src/fccache.c; the AROS port has no checksum_nano, that field was only
 * added in fontconfig 2.13.1).
 *
 * The AROS fontconfig port also shortens the cache file name to the first 8
 * characters of the MD5 of the font directory path string:
 *
 *     <md5hex8>-<arch>.cache-4        e.g. 29a5abb3-x86_64-aros.cache-4
 *
 * When a cache file is generated on AROS and then shipped inside an ISO, the
 * directory mtime at install time differs from the one recorded in the cache,
 * so fontconfig discards the cache and rescans all fonts on the first OWB run.
 *
 * This tool rewrites the 'checksum' field of an existing cache file to match
 * the current mtime of the font directory, so fontconfig reuses the pre-built
 * cache. It must be run once, right after installation, when the font
 * directory contents are known to match the cache contents.
 *
 * For cache versions >= 9 (fontconfig 2.13.1+, e.g. the 2.16.0 that this
 * repository builds against, which writes <md5hex32>-<arch>.cache-9 and a
 * checksum_nano field) it additionally zeroes checksum_nano, since the AROS
 * stat layer reports tv_nsec == 0 always.
 *
 * Usage: fcachefix [cachedir] [fontdir]
 *        defaults: cachedir = PROGDIR:Conf
 *                  fontdir  = Fonts:TrueType
 *
 * Copyright (C) 2026 The OWB contributors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of OWB nor the names of its contributors may be
 *     used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

/* fontconfig cache constants (same across 2.11.0 and 2.16.0) */
#define FC_CACHE_MAGIC_MMAP             0xFC02FC04u
#define FC_CACHE_CONTENT_VERSION_MIN    3

/*
 * Header of a fontconfig directory cache file, layout as of fontconfig
 * 2.11.0 (cache version 4). The 'checksum' field offset is the same in
 * fontconfig 2.16.0 (cache version 9); 2.13.1+ only appends checksum_nano
 * after it. Both are built with the same AROS toolchain ABI, so offsetof()
 * matches the on-disk layout.
 */
struct fc_cache_header_base {
    unsigned int magic;
    int          version;
    intptr_t     size;
    intptr_t     dir;
    intptr_t     dirs;
    int          dirs_count;
    intptr_t     set;
    int          checksum;
};

/* Layout as of fontconfig 2.13.1+ (cache version >= 9). */
struct fc_cache_header_nano {
    struct fc_cache_header_base base;
    int64_t                     checksum_nano;
};

/* -------------------------------------------------------------------------
 * MD5 (RFC 1321), public domain reference implementation
 * ---------------------------------------------------------------------- */

typedef struct {
    unsigned int state[4];
    unsigned int count[2];
    unsigned char buffer[64];
} md5_ctx;

static const unsigned char md5_padding[64] = { 0x80 };

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

#define STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = ROTL((a), (s)); \
    (a) += (b);

static void md5_transform(unsigned int state[4], const unsigned char block[64])
{
    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    const unsigned int *x = (const unsigned int *)block;
    unsigned int m[16];
    int i;

    /* block may be unaligned; copy it */
    for (i = 0; i < 16; i++)
        m[i] = x[i];

    STEP(F, a, b, c, d, m[0], 0xd76aa478, 7)
    STEP(F, d, a, b, c, m[1], 0xe8c7b756, 12)
    STEP(F, c, d, a, b, m[2], 0x242070db, 17)
    STEP(F, b, c, d, a, m[3], 0xc1bdceee, 22)
    STEP(F, a, b, c, d, m[4], 0xf57c0faf, 7)
    STEP(F, d, a, b, c, m[5], 0x4787c62a, 12)
    STEP(F, c, d, a, b, m[6], 0xa8304613, 17)
    STEP(F, b, c, d, a, m[7], 0xfd469501, 22)
    STEP(F, a, b, c, d, m[8], 0x698098d8, 7)
    STEP(F, d, a, b, c, m[9], 0x8b44f7af, 12)
    STEP(F, c, d, a, b, m[10], 0xffff5bb1, 17)
    STEP(F, b, c, d, a, m[11], 0x895cd7be, 22)
    STEP(F, a, b, c, d, m[12], 0x6b901122, 7)
    STEP(F, d, a, b, c, m[13], 0xfd987193, 12)
    STEP(F, c, d, a, b, m[14], 0xa679438e, 17)
    STEP(F, b, c, d, a, m[15], 0x49b40821, 22)

    STEP(G, a, b, c, d, m[1], 0xf61e2562, 5)
    STEP(G, d, a, b, c, m[6], 0xc040b340, 9)
    STEP(G, c, d, a, b, m[11], 0x265e5a51, 14)
    STEP(G, b, c, d, a, m[0], 0xe9b6c7aa, 20)
    STEP(G, a, b, c, d, m[5], 0xd62f105d, 5)
    STEP(G, d, a, b, c, m[10], 0x02441453, 9)
    STEP(G, c, d, a, b, m[15], 0xd8a1e681, 14)
    STEP(G, b, c, d, a, m[4], 0xe7d3fbc8, 20)
    STEP(G, a, b, c, d, m[9], 0x21e1cde6, 5)
    STEP(G, d, a, b, c, m[14], 0xc33707d6, 9)
    STEP(G, c, d, a, b, m[3], 0xf4d50d87, 14)
    STEP(G, b, c, d, a, m[8], 0x455a14ed, 20)
    STEP(G, a, b, c, d, m[13], 0xa9e3e905, 5)
    STEP(G, d, a, b, c, m[2], 0xfcefa3f8, 9)
    STEP(G, c, d, a, b, m[7], 0x676f02d9, 14)
    STEP(G, b, c, d, a, m[12], 0x8d2a4c8a, 20)

    STEP(H, a, b, c, d, m[5], 0xfffa3942, 4)
    STEP(H, d, a, b, c, m[8], 0x8771f681, 11)
    STEP(H, c, d, a, b, m[11], 0x6d9d6122, 16)
    STEP(H, b, c, d, a, m[14], 0xfde5380c, 23)
    STEP(H, a, b, c, d, m[1], 0xa4beea44, 4)
    STEP(H, d, a, b, c, m[4], 0x4bdecfa9, 11)
    STEP(H, c, d, a, b, m[7], 0xf6bb4b60, 16)
    STEP(H, b, c, d, a, m[10], 0xbebfbc70, 23)
    STEP(H, a, b, c, d, m[13], 0x289b7ec6, 4)
    STEP(H, d, a, b, c, m[0], 0xeaa127fa, 11)
    STEP(H, c, d, a, b, m[3], 0xd4ef3085, 16)
    STEP(H, b, c, d, a, m[6], 0x04881d05, 23)
    STEP(H, a, b, c, d, m[9], 0xd9d4d039, 4)
    STEP(H, d, a, b, c, m[12], 0xe6db99e5, 11)
    STEP(H, c, d, a, b, m[15], 0x1fa27cf8, 16)
    STEP(H, b, c, d, a, m[2], 0xc4ac5665, 23)

    STEP(I, a, b, c, d, m[0], 0xf4292244, 6)
    STEP(I, d, a, b, c, m[7], 0x432aff97, 10)
    STEP(I, c, d, a, b, m[14], 0xab9423a7, 15)
    STEP(I, b, c, d, a, m[5], 0xfc93a039, 21)
    STEP(I, a, b, c, d, m[12], 0x655b59c3, 6)
    STEP(I, d, a, b, c, m[3], 0x8f0ccc92, 10)
    STEP(I, c, d, a, b, m[10], 0xffeff47d, 15)
    STEP(I, b, c, d, a, m[1], 0x85845dd1, 21)
    STEP(I, a, b, c, d, m[8], 0x6fa87e4f, 6)
    STEP(I, d, a, b, c, m[15], 0xfe2ce6e0, 10)
    STEP(I, c, d, a, b, m[6], 0xa3014314, 15)
    STEP(I, b, c, d, a, m[13], 0x4e0811a1, 21)
    STEP(I, a, b, c, d, m[4], 0xf7537e82, 6)
    STEP(I, d, a, b, c, m[11], 0xbd3af235, 10)
    STEP(I, c, d, a, b, m[2], 0x2ad7d2bb, 15)
    STEP(I, b, c, d, a, m[9], 0xeb86d391, 21)

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(md5_ctx *ctx)
{
    ctx->count[0] = 0;
    ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md5_update(md5_ctx *ctx, const unsigned char *data, size_t len)
{
    unsigned int i, idx, partlen;

    idx = (ctx->count[0] >> 3) & 0x3f;
    if ((ctx->count[0] += (unsigned int)(len << 3)) < (unsigned int)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (unsigned int)(len >> 29);

    partlen = 64 - idx;
    if (len >= partlen)
    {
        memcpy(&ctx->buffer[idx], data, partlen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partlen; i + 63 < len; i += 64)
            md5_transform(ctx->state, &data[i]);
        idx = 0;
    }
    else
        i = 0;

    memcpy(&ctx->buffer[idx], &data[i], len - i);
}

static void md5_final(md5_ctx *ctx, unsigned char digest[16])
{
    unsigned char bits[8];
    unsigned int idx, padlen;
    int i;

    for (i = 0; i < 8; i++)
        bits[i] = (unsigned char)((ctx->count[i >> 2] >> ((i & 3) * 8)) & 0xff);

    idx = (ctx->count[0] >> 3) & 0x3f;
    padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(ctx, md5_padding, padlen);
    md5_update(ctx, bits, 8);

    for (i = 0; i < 4; i++)
    {
        digest[i * 4 + 0] = (unsigned char)(ctx->state[i] & 0xff);
        digest[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 8) & 0xff);
        digest[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 16) & 0xff);
        digest[i * 4 + 3] = (unsigned char)((ctx->state[i] >> 24) & 0xff);
    }
}

static void md5_hex(const char *str, char out[33])
{
    md5_ctx ctx;
    unsigned char digest[16];
    static const char hex[] = "0123456789abcdef";
    int i;

    md5_init(&ctx);
    md5_update(&ctx, (const unsigned char *)str, strlen(str));
    md5_final(&ctx, digest);

    for (i = 0; i < 16; i++)
    {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[32] = '\0';
}

/* -------------------------------------------------------------------------
 * fcachefix
 * ---------------------------------------------------------------------- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [cachedir] [fontdir]\n"
        "  cachedir : directory containing the fontconfig cache files\n"
        "             (default: PROGDIR:Conf)\n"
        "  fontdir  : font directory whose mtime must match the cache\n"
        "             (default: Fonts:TrueType)\n",
        prog);
}

/*
 * Match a cache file name: "<md5hex8>-<arch>.cache-<version>" on the AROS
 * fontconfig port (8 hex characters), or "<md5hex32>-<arch>.cache-<version>"
 * on stock fontconfig (32 hex characters).
 */
static int cache_name_matches(const char *name, const char *md5hex)
{
    size_t n = strlen(name);

    if (strstr(name, ".cache-") == NULL)
        return 0;

    if (n >= 9 && name[8] == '-' && strncmp(name, md5hex, 8) == 0)
        return 1;

    if (n >= 33 && name[32] == '-' && strncmp(name, md5hex, 32) == 0)
        return 1;

    return 0;
}

static int patch_cache(const char *cache_path, time_t mtime, int has_nano)
{
    struct fc_cache_header_base header;
    int fd;
    int ret = -1;

    fd = open(cache_path, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "fcachefix: cannot open %s: %s\n", cache_path, strerror(errno));
        return -1;
    }

    if (read(fd, &header, sizeof(header)) != (ssize_t)sizeof(header))
    {
        fprintf(stderr, "fcachefix: %s is too small to be a fontconfig cache\n", cache_path);
        goto out;
    }

    if (header.magic != FC_CACHE_MAGIC_MMAP)
    {
        fprintf(stderr, "fcachefix: %s: bad magic 0x%08x (not a fontconfig cache?)\n",
                cache_path, header.magic);
        goto out;
    }

    if (header.version < FC_CACHE_CONTENT_VERSION_MIN)
    {
        fprintf(stderr, "fcachefix: %s: unsupported cache version %d\n", cache_path, header.version);
        goto out;
    }

    {
        int new_checksum = (int)mtime;
        off_t checksum_off = (off_t)offsetof(struct fc_cache_header_base, checksum);

        if (lseek(fd, checksum_off, SEEK_SET) == (off_t)-1 ||
            write(fd, &new_checksum, sizeof(new_checksum)) != (ssize_t)sizeof(new_checksum))
        {
            fprintf(stderr, "fcachefix: failed to update %s: %s\n", cache_path, strerror(errno));
            goto out;
        }

        if (has_nano)
        {
            int64_t zero_nano = 0;
            off_t nano_off = (off_t)offsetof(struct fc_cache_header_nano, checksum_nano);

            if (lseek(fd, nano_off, SEEK_SET) == (off_t)-1 ||
                write(fd, &zero_nano, sizeof(zero_nano)) != (ssize_t)sizeof(zero_nano))
            {
                fprintf(stderr, "fcachefix: failed to update %s: %s\n", cache_path, strerror(errno));
                goto out;
            }
        }
    }

    printf("fcachefix: %s: checksum set to %d%s\n", cache_path, (int)mtime,
           has_nano ? " (nano 0)" : "");
    ret = 0;

out:
    close(fd);
    return ret;
}

int main(int argc, char *argv[])
{
    const char *primary = "PROGDIR:Conf";
    const char *fontdir = "Fonts:TrueType";
    const char *candidates[4];
    char md5hex[33];
    struct stat st;
    int ncan = 0;
    int i;
    int done = 0;
    int matched = 0;
    int ret = 0;

    if (argc > 3)
    {
        usage(argv[0]);
        return 20;
    }
    if (argc >= 2)
        primary = argv[1];
    if (argc >= 3)
        fontdir = argv[2];

    if (stat(fontdir, &st) != 0)
    {
        fprintf(stderr, "fcachefix: cannot stat font directory %s: %s\n",
                fontdir, strerror(errno));
        return 21;
    }

    md5_hex(fontdir, md5hex);

    /* Candidate cache directories, most likely first. */
    candidates[ncan++] = primary;
    if (strcmp(primary, "PROGDIR:Conf") != 0)
        candidates[ncan++] = "PROGDIR:Conf";
    candidates[ncan++] = "PROGDIR:Conf/font";
    candidates[ncan++] = "PROGDIR:fonts/cache";
    candidates[ncan++] = "T:fonts/cache";

    for (i = 0; i < ncan && !done; i++)
    {
        DIR *dir;
        struct dirent *ent;
        int this_matched = 0;
        int this_ok = 0;

        dir = opendir(candidates[i]);
        if (!dir)
            continue;

        while ((ent = readdir(dir)) != NULL)
        {
            const char *suffix;
            char path[512];
            int ver = 0;
            int has_nano;

            if (!cache_name_matches(ent->d_name, md5hex))
                continue;

            this_matched++;
            suffix = strstr(ent->d_name, ".cache-");
            if (suffix)
                sscanf(suffix + strlen(".cache-"), "%d", &ver);
            has_nano = (ver >= 9);

            snprintf(path, sizeof(path), "%s/%s", candidates[i], ent->d_name);
            if (patch_cache(path, st.st_mtime, has_nano) == 0)
                this_ok = 1;
            else
                ret = 21;
        }
        closedir(dir);

        matched += this_matched;
        if (this_matched && this_ok)
            done = 1;
    }

    if (!matched)
    {
        fprintf(stderr, "fcachefix: no cache file matching %s-*.cache-* found in any candidate dir.\n"
                        "Run OWB once first to build the font cache.\n",
                md5hex);
        return 21;
    }

    return ret;
}