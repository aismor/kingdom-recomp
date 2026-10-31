// Public-domain SHA-256 (Brad Conte), used on Linux instead of BCrypt.
#include <stdint.h>
#include <string.h>

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(uint32_t st[8], const uint8_t chunk[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)chunk[i * 4] << 24) | ((uint32_t)chunk[i * 4 + 1] << 16) |
               ((uint32_t)chunk[i * 4 + 2] << 8) | chunk[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = st[0]; b = st[1]; c = st[2]; d = st[3]; e = st[4]; f = st[5]; g = st[6]; h = st[7];
    for (int i = 0; i < 64; i++) {
        t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

typedef struct {
    uint32_t st[8];
    uint64_t total;
    uint8_t buf[64];
    size_t buf_n;
} RtSha256Ctx;

// The last one or two blocks: the buffered tail, 0x80, zeros, and the message length in bits (big-endian) at the end.
static void sha256_pad(RtSha256Ctx *c) {
    uint8_t block[128] = {0};
    size_t n = c->buf_n;
    memcpy(block, c->buf, n);
    block[n] = 0x80;
    size_t len = n + 9 <= 64 ? 64 : 128;
    uint64_t bits = c->total * 8;
    for (int j = 0; j < 8; j++) block[len - 1 - j] = (uint8_t)(bits >> (8 * j));
    sha256_transform(c->st, block);
    if (len == 128) sha256_transform(c->st, block + 64);
}

void rt_sha256_begin(RtSha256Ctx *c) {
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(c->st, iv, sizeof iv);
    c->total = 0;
    c->buf_n = 0;
}

void rt_sha256_feed(RtSha256Ctx *c, const void *data, size_t len) {
    const uint8_t *p = data;
    c->total += len;
    if (c->buf_n) {
        size_t take = 64 - c->buf_n;
        if (take > len) take = len;
        memcpy(c->buf + c->buf_n, p, take);
        c->buf_n += take;
        p += take;
        len -= take;
        if (c->buf_n == 64) {
            sha256_transform(c->st, c->buf);
            c->buf_n = 0;
        }
    }
    for (; len >= 64; len -= 64, p += 64) sha256_transform(c->st, p);
    if (len) {
        memcpy(c->buf, p, len);
        c->buf_n = len;
    }
}

void rt_sha256_end(RtSha256Ctx *c, uint8_t out[32]) {
    sha256_pad(c);
    for (int j = 0; j < 8; j++) {
        out[j * 4] = (uint8_t)(c->st[j] >> 24);
        out[j * 4 + 1] = (uint8_t)(c->st[j] >> 16);
        out[j * 4 + 2] = (uint8_t)(c->st[j] >> 8);
        out[j * 4 + 3] = (uint8_t)c->st[j];
    }
}

void rt_sha256(const void *data, size_t len, uint8_t out[32]) {
    RtSha256Ctx c;
    rt_sha256_begin(&c);
    rt_sha256_feed(&c, data, len);
    rt_sha256_end(&c, out);
}
