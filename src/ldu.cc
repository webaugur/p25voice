#include "ldu.h"

#include "presets.h"
#include "voice_bits.h"

#include <cstring>
#include <string>

namespace {

constexpr int16_t kLevel[4] = {8000, 24000, -8000, -24000};
constexpr unsigned kSamplesPerSymbol = 10;
constexpr uint64_t kFrameSync = 0x5575F5FF77FFLL;

uint32_t golay_23_encode(uint32_t code_word_in) {
    /* Golay(24,12) then drop the last bit. From boatbod/op25 op25_golay.h,
     * Copyright 2009-2014 KA1RBI, GPL-3.0-or-later.
     */
    static const uint32_t encoding[12] = {
        040006165, 020003073, 010007550, 04003664, 02001732, 01006631,
        0403315,   0201547,   0106706,   045227,   024476,   014353};
    uint32_t code_word_out = 0;
    for (uint16_t i = 0; i < 12; i++) {
        if (code_word_in & (1u << (11 - i))) {
            code_word_out ^= encoding[i];
        }
    }
    return code_word_out >> 1;
}

uint16_t hamming_15_encode(uint16_t code_word_in) {
    /* Hamming(15,11). From boatbod/op25 op25_hamming.h, same copyright. */
    static const uint16_t encoding[11] = {0x400f, 0x200e, 0x100d, 0x080c, 0x040b, 0x020a,
                                           0x0109, 0x0087, 0x0046, 0x0025, 0x0013};
    uint16_t code_word_out = 0;
    for (uint8_t i = 0; i < 11; i++) {
        if (code_word_in & (1u << (10 - i))) {
            code_word_out ^= encoding[i];
        }
    }
    return code_word_out;
}

uint32_t pngen15(uint32_t &pr) {
    int n = 0;
    for (int i = 14; i >= 0; --i) {
        pr = (173 * pr + 13849) & 0xffffu;
        if (pr & 32768) {
            n += (1 << i);
        }
    }
    return static_cast<uint32_t>(n);
}

uint32_t pngen23(uint32_t &pr) {
    int n = 0;
    for (int i = 22; i >= 0; --i) {
        pr = (173 * pr + 13849) & 0xffffu;
        if (pr & 32768) {
            n += (1 << i);
        }
    }
    return static_cast<uint32_t>(n);
}

void store_bits(uint8_t *cw, int start, int limit, uint32_t value) {
    for (int i = limit - 1; i >= start; i--) {
        cw[i] = static_cast<uint8_t>(value & 1u);
        value >>= 1;
    }
}

void header_encode(uint8_t *cw, uint32_t u0, uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5, uint32_t u6, uint32_t u7) {
    uint32_t pn = u0 << 4;
    store_bits(cw, 0, 23, golay_23_encode(u0));
    store_bits(cw, 23, 46, golay_23_encode(u1) ^ pngen23(pn));
    store_bits(cw, 46, 69, golay_23_encode(u2) ^ pngen23(pn));
    store_bits(cw, 69, 92, golay_23_encode(u3) ^ pngen23(pn));
    store_bits(cw, 92, 107, hamming_15_encode(static_cast<uint16_t>(u4)) ^ pngen15(pn));
    store_bits(cw, 107, 122, hamming_15_encode(static_cast<uint16_t>(u5)) ^ pngen15(pn));
    store_bits(cw, 122, 137, hamming_15_encode(static_cast<uint16_t>(u6)) ^ pngen15(pn));
    store_bits(cw, 137, 144, u7 >> 1);
}

uint64_t bch_64_16_23_encode(uint16_t data) {
    /* (64,16) BCH used for the NID. From boatbod/op25 p25p1_voice_encode.cc. */
    static const uint64_t matrix[16] = {
        0x8000cd930bdd3b2aULL, 0x4000ab5a8e33a6beULL, 0x2000983e4cc4e874ULL, 0x10004c1f2662743aULL,
        0x0800eb9c98ec0136ULL, 0x0400b85d47ab3bb0ULL, 0x02005c2ea3d59dd8ULL, 0x01002e1751eaceecULL,
        0x0080170ba8f56776ULL, 0x0040c616dfa78890ULL, 0x0020630b6fd3c448ULL, 0x00103185b7e9e224ULL,
        0x000818c2dbf4f112ULL, 0x0004c1f2662743a2ULL, 0x0002ad6a38ce9afbULL, 0x00019b2617ba7657ULL};
    uint64_t cw = 0;
    for (int i = 0; i < 16; i++) {
        if (data & (0x8000 >> i)) {
            cw ^= matrix[i];
        }
    }
    return cw;
}

uint8_t gf6mult(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 6; i++) {
        if (b & 1) {
            p ^= a;
        }
        a = static_cast<uint8_t>(a << 1);
        if (a & 0x40) {
            a ^= 0x43;
        }
        b >>= 1;
    }
    return p & 0x3f;
}

void rs_24_12_13_encode(const uint8_t hexbits[12], uint8_t cw[24]) {
    static const uint8_t matrix[12][24] = {
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 062, 044, 003, 025, 014, 016, 027, 003, 053, 004, 036, 047},
        {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 011, 012, 011, 011, 016, 064, 067, 055, 001, 076, 026, 073},
        {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 003, 001, 005, 075, 014, 006, 020, 044, 066, 006, 070, 066},
        {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 021, 070, 027, 045, 016, 067, 023, 064, 073, 033, 044, 021},
        {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 030, 022, 003, 075, 015, 015, 033, 015, 051, 003, 053, 050},
        {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 001, 041, 027, 056, 076, 064, 021, 053, 004, 025, 001, 012},
        {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 061, 076, 021, 055, 076, 001, 063, 035, 030, 013, 064, 070},
        {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 024, 022, 071, 056, 021, 035, 073, 042, 057, 074, 043, 076},
        {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 072, 042, 005, 020, 043, 047, 033, 056, 001, 016, 013, 076},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 072, 014, 065, 054, 035, 025, 041, 016, 015, 040, 071, 026},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 073, 065, 036, 061, 042, 022, 017, 004, 044, 020, 025, 005},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 071, 005, 055, 003, 071, 034, 060, 011, 074, 002, 041, 050}};
    for (int i = 0; i < 24; i++) {
        cw[i] = 0;
    }
    for (int i = 0; i < 24; i++) {
        for (int j = 0; j < 12; j++) {
            cw[i] ^= gf6mult(hexbits[j], matrix[j][i]);
        }
    }
}

uint16_t hamming_10_6_3_encode(uint8_t data) {
    static const uint16_t matrix[6] = {0x20e, 0x10d, 0x08b, 0x047, 0x023, 0x01c};
    uint16_t cw = 0;
    for (int i = 0; i < 6; i++) {
        if (data & (0x20 >> i)) {
            cw ^= matrix[i];
        }
    }
    return cw;
}

void setup_header(uint8_t *body, uint64_t hw) {
    uint64_t acc = kFrameSync;
    for (int i = 47; i >= 0; i--) {
        body[i] = static_cast<uint8_t>(acc & 1u);
        acc >>= 1;
    }
    acc = hw;
    for (int i = 113; i >= 72; i--) {
        body[i] = static_cast<uint8_t>(acc & 1u);
        acc >>= 1;
    }
    body[70] = 1;
    body[71] = 0;
    for (int i = 69; i >= 48; i--) {
        body[i] = static_cast<uint8_t>(acc & 1u);
        acc >>= 1;
    }
}

void overlay_group_lc(uint8_t *body, uint16_t talkgroup, uint32_t source) {
    unsigned __int128 lc = 0;
    lc |= (unsigned __int128)0 << 64; /* LCF: group voice, unencrypted */
    lc |= (unsigned __int128)0 << 56; /* MFID standard */
    lc |= (unsigned __int128)0 << 48; /* service options */
    lc |= (unsigned __int128)(talkgroup & 0xffff) << 24;
    lc |= source & 0xffffff;
    uint8_t hexbits[12];
    for (int j = 0; j < 12; j++) {
        hexbits[j] = static_cast<uint8_t>((lc >> ((11 - j) * 6)) & 0x3f);
    }
    uint8_t rs[24];
    rs_24_12_13_encode(hexbits, rs);
    int k = 0;
    for (int i = 0; i < 24; i++) {
        uint16_t h = hamming_10_6_3_encode(rs[i]);
        for (int b = 9; b >= 0; b--) {
            body[imbe_ldu_ls_data_bits[k++]] = static_cast<uint8_t>((h >> b) & 1);
        }
    }
}

void write_symbols(const uint8_t *body, std::vector<int16_t> &samples) {
    samples.reserve(samples.size() + (P25_VOICE_FRAME_BITS / 2) * kSamplesPerSymbol);
    for (unsigned i = 0; i < P25_VOICE_FRAME_BITS; i += 2) {
        unsigned dibit = (body[i] << 1) | body[i + 1];
        int16_t level = kLevel[dibit & 3];
        for (unsigned n = 0; n < kSamplesPerSymbol; n++) {
            samples.push_back(level);
        }
    }
}

}  // namespace

LduFramer::LduFramer(uint16_t nac, bool custom_lc, uint16_t talkgroup, uint32_t source)
    : nac_(nac),
      custom_lc_(custom_lc),
      talkgroup_(talkgroup),
      source_(source),
      codeword_ct_(0),
      ldu_ct_(0) {
    std::memset(body_, 0, sizeof body_);
}

void LduFramer::push(const int16_t frame_vector[8], std::vector<int16_t> &samples) {
    uint8_t cw[VOICE_CODEWORD_SZ];
    std::memset(cw, 0, sizeof cw);
    uint32_t u[8];
    for (int i = 0; i < 8; i++) {
        u[i] = static_cast<uint16_t>(frame_vector[i]);
    }
    header_encode(cw, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7]);
    for (size_t j = 0; j < VOICE_CODEWORD_SZ; j++) {
        body_[voice_codeword_bits[codeword_ct_][j]] = cw[j];
    }
    if (++codeword_ct_ >= NOF_VOICE_CODEWORDS) {
        close_ldu(samples);
    }
}

void LduFramer::close_ldu(std::vector<int16_t> &samples) {
    unsigned ldu_type = ldu_ct_ & 1u;
    static const int duids[2] = {0x5, 0xA};
    uint64_t hw = bch_64_16_23_encode(static_cast<uint16_t>((nac_ << 4) | duids[ldu_type]));
    setup_header(body_, hw);
    const uint16_t *ones = ldu_type == 0 ? LDU1_PRESET_ONES : LDU2_PRESET_ONES;
    unsigned ones_len = ldu_type == 0 ? LDU1_PRESET_ONES_LEN : LDU2_PRESET_ONES_LEN;
    for (unsigned i = 0; i < ones_len; i++) {
        body_[ones[i]] = 1;
    }
    if (custom_lc_ && ldu_type == 0) {
        overlay_group_lc(body_, talkgroup_, source_);
    }
    write_symbols(body_, samples);
    std::memset(body_, 0, sizeof body_);
    codeword_ct_ = 0;
    ldu_ct_++;
}

std::string LduFramer::self_test() {
    int16_t zero[8] = {};
    LduFramer framer(0x293, false, 0, 0);
    std::vector<int16_t> samples;
    for (int i = 0; i < 9; i++) {
        framer.push(zero, samples);
    }
    if (samples.size() != 8640) {
        return "expected 8640 samples in one LDU";
    }
    const char *expect = "111113113311333313133333";
    for (int s = 0; s < 24; s++) {
        int16_t level = samples[static_cast<size_t>(s) * kSamplesPerSymbol];
        char got = '0';
        if (level == 24000) {
            got = '1';
        } else if (level == 8000) {
            got = '0';
        } else if (level == -8000) {
            got = '2';
        } else if (level == -24000) {
            got = '3';
        } else {
            return "unexpected symbol level";
        }
        if (got != expect[s]) {
            return "frame sync dibits do not match dsd-fme P25P1_SYNC";
        }
    }
    return {};
}
