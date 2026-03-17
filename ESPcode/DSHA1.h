#ifndef DSHA1_H
#define DSHA1_H

#include <Arduino.h>

class DSHA1 {
    
public:
    static const size_t OUTPUT_SIZE = 20;

    DSHA1() {
        bytes = 0; // FIX
        initialize(s);
    }

    DSHA1 &write(const unsigned char *data, size_t len) {
        size_t bufsize = bytes % 64;

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

    void finalize(unsigned char hash[OUTPUT_SIZE]) {
        const unsigned char pad[64] = {0x80};
        unsigned char sizedesc[8];

        writeBE64(sizedesc, bytes << 3);

        write(pad, 1 + ((119 - (bytes % 64)) % 64));
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
        uint8_t warmup[20];
        this->write((uint8_t *)"warmupwarmupwa", 20).finalize(warmup);
        return *this;
    }

private:
    uint32_t s[5];
    unsigned char buf[64];
    uint64_t bytes;

    const uint32_t k1 = 0x5A827999ul;
    const uint32_t k2 = 0x6ED9EBA1ul;
    const uint32_t k3 = 0x8F1BBCDCul;
    const uint32_t k4 = 0xCA62C1D6ul;

    inline uint32_t f1(uint32_t b, uint32_t c, uint32_t d) { return d ^ (b & (c ^ d)); }
    inline uint32_t f2(uint32_t b, uint32_t c, uint32_t d) { return b ^ c ^ d; }
    inline uint32_t f3(uint32_t b, uint32_t c, uint32_t d) { return (b & c) | (d & (b | c)); }

    inline uint32_t left(uint32_t x) { return (x << 1) | (x >> 31); }

    inline void Round(uint32_t a, uint32_t &b, uint32_t c, uint32_t d, uint32_t &e,
                      uint32_t f, uint32_t k, uint32_t w) {
        e += ((a << 5) | (a >> 27)) + f + k + w;
        b = (b << 30) | (b >> 2);
    }

    void initialize(uint32_t s[5]) {
        s[0] = 0x67452301ul;
        s[1] = 0xEFCDAB89ul;
        s[2] = 0x98BADCFEul;
        s[3] = 0x10325476ul;
        s[4] = 0xC3D2E1F0ul;
    }

    void transform(uint32_t *s, const unsigned char *chunk) {

        uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
        uint32_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

        // Load
        w0  = readBE32(chunk + 0);
        w1  = readBE32(chunk + 4);
        w2  = readBE32(chunk + 8);
        w3  = readBE32(chunk + 12);
        w4  = readBE32(chunk + 16);
        w5  = readBE32(chunk + 20);
        w6  = readBE32(chunk + 24);
        w7  = readBE32(chunk + 28);
        w8  = readBE32(chunk + 32);
        w9  = readBE32(chunk + 36);
        w10 = readBE32(chunk + 40);
        w11 = readBE32(chunk + 44);
        w12 = readBE32(chunk + 48);
        w13 = readBE32(chunk + 52);
        w14 = readBE32(chunk + 56);
        w15 = readBE32(chunk + 60);

        // fixed x)

        for (int i = 0; i < 80; i++) {
            uint32_t w;

            if (i >= 16) {
                switch (i % 16) {
                    case 0:  w0  = left(w13 ^ w8  ^ w2  ^ w0);  w = w0; break;
                    case 1:  w1  = left(w14 ^ w9  ^ w3  ^ w1);  w = w1; break;
                    case 2:  w2  = left(w15 ^ w10 ^ w4  ^ w2);  w = w2; break;
                    case 3:  w3  = left(w0  ^ w11 ^ w5  ^ w3);  w = w3; break;
                    case 4:  w4  = left(w1  ^ w12 ^ w6  ^ w4);  w = w4; break;
                    case 5:  w5  = left(w2  ^ w13 ^ w7  ^ w5);  w = w5; break;
                    case 6:  w6  = left(w3  ^ w14 ^ w8  ^ w6);  w = w6; break;
                    case 7:  w7  = left(w4  ^ w15 ^ w9  ^ w7);  w = w7; break;
                    case 8:  w8  = left(w5  ^ w0  ^ w10 ^ w8);  w = w8; break;
                    case 9:  w9  = left(w6  ^ w1  ^ w11 ^ w9);  w = w9; break;
                    case 10: w10 = left(w7  ^ w2  ^ w12 ^ w10); w = w10; break;
                    case 11: w11 = left(w8  ^ w3  ^ w13 ^ w11); w = w11; break;
                    case 12: w12 = left(w9  ^ w4  ^ w14 ^ w12); w = w12; break;
                    case 13: w13 = left(w10 ^ w5  ^ w15 ^ w13); w = w13; break;
                    case 14: w14 = left(w11 ^ w6  ^ w0  ^ w14); w = w14; break;
                    default: w15 = left(w12 ^ w7  ^ w1  ^ w15); w = w15; break;
                }
            } else {
                w = (&w0)[i];
            }

            uint32_t f, k;

            if (i < 20)      { f = f1(b,c,d); k = k1; }
            else if (i < 40) { f = f2(b,c,d); k = k2; }
            else if (i < 60) { f = f3(b,c,d); k = k3; }
            else             { f = f2(b,c,d); k = k4; }

            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w;
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        s[0] += a;
        s[1] += b;
        s[2] += c;
        s[3] += d;
        s[4] += e;
    }

    static inline uint32_t readBE32(const unsigned char *ptr) {
        return __builtin_bswap32(*(uint32_t *)ptr);
    }

    static inline void writeBE32(unsigned char *ptr, uint32_t x) {
        *(uint32_t *)ptr = __builtin_bswap32(x);
    }

    static inline void writeBE64(unsigned char *ptr, uint64_t x) {
        *(uint64_t *)ptr = __builtin_bswap64(x);
    }
};

#endif
