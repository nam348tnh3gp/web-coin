#ifndef DSHA1_H
#define DSHA1_H

#include <Arduino.h>

class DSHA1 {
    
public:
    static const size_t OUTPUT_SIZE = 20;

    DSHA1() {
        bytes = 0;
        initialize(s);
    }

    DSHA1 &write(const uint8_t *data, size_t len) {
        size_t bufsize = bytes & 63;

        if (bufsize && bufsize + len >= 64) {
            memcpy(buf + bufsize, data, 64 - bufsize);
            bytes += 64 - bufsize;
            data += 64 - bufsize;
            len  -= 64 - bufsize;
            transform(s, buf);
            bufsize = 0;
        }

        while (len >= 64) {
            transform(s, data);
            bytes += 64;
            data += 64;
            len -= 64;
        }

        if (len > 0) {
            memcpy(buf + bufsize, data, len);
            bytes += len;
        }

        return *this;
    }

    void finalize(uint8_t hash[OUTPUT_SIZE]) {
        const uint8_t pad[64] = {0x80};
        uint8_t sizedesc[8];

        writeBE64(sizedesc, bytes << 3);

        write(pad, 1 + ((119 - (bytes & 63)) & 63));
        write(sizedesc, 8);

        writeBE32(hash,     s[0]);
        writeBE32(hash + 4, s[1]);
        writeBE32(hash + 8, s[2]);
        writeBE32(hash + 12,s[3]);
        writeBE32(hash + 16,s[4]);
    }

    DSHA1 &reset() {
        bytes = 0;
        initialize(s);
        return *this;
    }

    DSHA1 &warmup() {
        uint8_t tmp[20];
        this->write((uint8_t *)"warmupwarmupwa", 20).finalize(tmp);
        return *this;
    }

private:
    uint32_t s[5] __attribute__((aligned(4)));
    uint8_t  buf[64] __attribute__((aligned(4)));
    uint64_t bytes;

    // fast macros
    #define ROL(x,n) (((x) << (n)) | ((x) >> (32-(n))))
    #define F1(b,c,d) (d ^ (b & (c ^ d)))
    #define F2(b,c,d) (b ^ c ^ d)
    #define F3(b,c,d) ((b & c) | (d & (b | c)))

    // init
    inline void initialize(uint32_t *s) {
        s[0] = 0x67452301ul;
        s[1] = 0xEFCDAB89ul;
        s[2] = 0x98BADCFEul;
        s[3] = 0x10325476ul;
        s[4] = 0xC3D2E1F0ul;
    }

    // core
    void IRAM_ATTR transform(uint32_t *s, const uint8_t *chunk) {

        uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
        uint32_t w[16];

        // load (aligned read)
        for (int i = 0; i < 16; i++) {
            w[i] = __builtin_bswap32(((uint32_t*)chunk)[i]);
        }

        for (int i = 0; i < 80; i++) {

            uint32_t wi;

            if (i < 16) {
                wi = w[i];
            } else {
                wi = ROL(
                    w[(i-3)&15] ^ w[(i-8)&15] ^ w[(i-14)&15] ^ w[i&15],
                    1
                );
                w[i & 15] = wi;
            }

            uint32_t f, k;

            if (i < 20)      { f = F1(b,c,d); k = 0x5A827999; }
            else if (i < 40) { f = F2(b,c,d); k = 0x6ED9EBA1; }
            else if (i < 60) { f = F3(b,c,d); k = 0x8F1BBCDC; }
            else             { f = F2(b,c,d); k = 0xCA62C1D6; }

            uint32_t temp = ROL(a,5) + f + e + k + wi;

            e = d;
            d = c;
            c = ROL(b,30);
            b = a;
            a = temp;
        }

        s[0] += a;
        s[1] += b;
        s[2] += c;
        s[3] += d;
        s[4] += e;
    }

    // endian
    static inline uint32_t readBE32(const uint8_t *ptr) {
        return __builtin_bswap32(*(uint32_t *)ptr);
    }

    static inline void writeBE32(uint8_t *ptr, uint32_t x) {
        *(uint32_t *)ptr = __builtin_bswap32(x);
    }

    static inline void writeBE64(uint8_t *ptr, uint64_t x) {
        *(uint64_t *)ptr = __builtin_bswap64(x);
    }
};

#endif
