#include "ldu.h"

#include <imbe_vocoder.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr unsigned kDecimate = 6;
constexpr int kImbeFrame = 160;

void usage(const char *argv0) {
    std::cerr
        << "usage: " << argv0
        << " [--nac 0x293] [--tg 0x5555] [--src 0xAAAAAA] [--self-test]\n"
        << "stdin and stdout are 48 kHz mono signed 16-bit little-endian.\n"
        << "stdout is P25 Phase 1 C4FM, one LDU (180 ms) at a time.\n";
}

bool parse_u32(const char *text, unsigned max, unsigned &out) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    unsigned long value = std::strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > max) {
        return false;
    }
    out = static_cast<unsigned>(value);
    return true;
}

bool write_all(const int16_t *samples, size_t count) {
    const char *bytes = reinterpret_cast<const char *>(samples);
    size_t left = count * sizeof(int16_t);
    while (left > 0) {
        size_t n = std::fwrite(bytes, 1, left, stdout);
        if (n == 0) {
            return false;
        }
        bytes += n;
        left -= n;
    }
    return std::fflush(stdout) == 0;
}

}  // namespace

int main(int argc, char **argv) {
    unsigned nac = 0x293;
    unsigned talkgroup = 0x5555;
    unsigned source = 0xAAAAAA;
    bool custom_lc = false;
    bool self_test = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto need = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "--self-test") {
            self_test = true;
            continue;
        }
        if (arg == "--nac") {
            if (!parse_u32(need("--nac"), 0xfff, nac)) {
                std::cerr << "nac must be 0..0xfff\n";
                return 2;
            }
            continue;
        }
        if (arg == "--tg") {
            if (!parse_u32(need("--tg"), 0xffff, talkgroup)) {
                std::cerr << "talkgroup must be 0..0xffff\n";
                return 2;
            }
            custom_lc = true;
            continue;
        }
        if (arg == "--src") {
            if (!parse_u32(need("--src"), 0xffffff, source)) {
                std::cerr << "source must be 0..0xffffff\n";
                return 2;
            }
            custom_lc = true;
            continue;
        }
        std::cerr << "unknown argument " << arg << "\n";
        usage(argv[0]);
        return 2;
    }

    if (self_test) {
        std::string err = LduFramer::self_test();
        if (!err.empty()) {
            std::cerr << "self-test: " << err << "\n";
            return 1;
        }
        std::cerr << "self-test: frame sync matches\n";
        return 0;
    }

    imbe_vocoder vocoder;
    LduFramer framer(static_cast<uint16_t>(nac), custom_lc, static_cast<uint16_t>(talkgroup),
                     source);

    int16_t pcm[kImbeFrame];
    unsigned pcm_fill = 0;
    int32_t bucket = 0;
    unsigned bucket_n = 0;
    std::vector<int16_t> pending;
    pending.reserve(8640);

    while (true) {
        int16_t in[2048];
        size_t got = std::fread(in, sizeof(int16_t), 2048, stdin);
        if (got == 0) {
            if (std::ferror(stdin)) {
                std::cerr << "stdin read failed\n";
                return 1;
            }
            break;
        }
        for (size_t i = 0; i < got; i++) {
            bucket += in[i];
            if (++bucket_n < kDecimate) {
                continue;
            }
            pcm[pcm_fill++] = static_cast<int16_t>(bucket / static_cast<int32_t>(kDecimate));
            bucket = 0;
            bucket_n = 0;
            if (pcm_fill < kImbeFrame) {
                continue;
            }
            int16_t frame_vector[8];
            vocoder.imbe_encode(frame_vector, pcm);
            pcm_fill = 0;
            framer.push(frame_vector, pending);
            if (!pending.empty()) {
                if (!write_all(pending.data(), pending.size())) {
                    std::cerr << "stdout write failed\n";
                    return 1;
                }
                pending.clear();
            }
        }
    }
    return 0;
}
