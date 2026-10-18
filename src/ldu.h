#ifndef P25VOICE_LDU_H
#define P25VOICE_LDU_H

#include <cstdint>
#include <string>
#include <vector>

/* One P25 Phase 1 voice superframe half (LDU1 or LDU2).
 * Nine IMBE codewords fill one 180 ms frame. The finished frame is 864
 * C4FM symbols, written as 48 kHz mono s16le (10 samples per symbol).
 */
class LduFramer {
public:
    LduFramer(uint16_t nac, bool custom_lc, uint16_t talkgroup, uint32_t source);

    /* frame_vector is the 8 words from imbe_vocoder::imbe_encode.
     * Samples are appended only when the ninth codeword closes an LDU.
     */
    void push(const int16_t frame_vector[8], std::vector<int16_t> &samples);

    /* Build one LDU from nine zero codewords and check the frame sync.
     * Returns an empty string on success, otherwise the reason.
     */
    static std::string self_test();

private:
    void close_ldu(std::vector<int16_t> &samples);

    uint16_t nac_;
    bool custom_lc_;
    uint16_t talkgroup_;
    uint32_t source_;
    unsigned codeword_ct_;
    unsigned ldu_ct_;
    uint8_t body_[1728];
};

#endif
