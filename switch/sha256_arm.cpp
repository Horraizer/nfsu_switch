#include <arm_neon.h>

#include <cstddef>
#include <cstdint>

namespace nfsu {

namespace {

uint32x4_t message_words(const std::uint8_t* bytes) noexcept {
    return vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(bytes)));
}

uint32x4_t expand_words(uint32x4_t a, uint32x4_t b, uint32x4_t c, uint32x4_t d) noexcept {
    return vsha256su1q_u32(vsha256su0q_u32(a, b), c, d);
}

void four_rounds(uint32x4_t& abcd, uint32x4_t& efgh, uint32x4_t words,
                 const std::uint32_t* rounds) noexcept {
    const auto input = vaddq_u32(words, vld1q_u32(rounds));
    const auto previous_abcd = abcd;
    abcd = vsha256hq_u32(abcd, efgh, input);
    efgh = vsha256h2q_u32(efgh, previous_abcd, input);
}

} // namespace

void sha256_compress_arm(std::uint32_t* state, const std::uint8_t* blocks,
                         std::size_t count, const std::uint32_t* rounds) noexcept {
    auto abcd = vld1q_u32(state);
    auto efgh = vld1q_u32(state + 4);
    for (std::size_t block = 0; block < count; ++block, blocks += 64) {
        const auto initial_abcd = abcd;
        const auto initial_efgh = efgh;
        auto w0 = message_words(blocks);
        auto w1 = message_words(blocks + 16);
        auto w2 = message_words(blocks + 32);
        auto w3 = message_words(blocks + 48);
        four_rounds(abcd, efgh, w0, rounds);
        four_rounds(abcd, efgh, w1, rounds + 4);
        four_rounds(abcd, efgh, w2, rounds + 8);
        four_rounds(abcd, efgh, w3, rounds + 12);
        // Each new group uses the preceding 16 schedule words. Keeping
        // the four vectors separate lets the compiler keep them in registers.
        for (unsigned group = 16; group < 64; group += 16) {
            w0 = expand_words(w0, w1, w2, w3);
            four_rounds(abcd, efgh, w0, rounds + group);
            w1 = expand_words(w1, w2, w3, w0);
            four_rounds(abcd, efgh, w1, rounds + group + 4);
            w2 = expand_words(w2, w3, w0, w1);
            four_rounds(abcd, efgh, w2, rounds + group + 8);
            w3 = expand_words(w3, w0, w1, w2);
            four_rounds(abcd, efgh, w3, rounds + group + 12);
        }
        abcd = vaddq_u32(initial_abcd, abcd);
        efgh = vaddq_u32(initial_efgh, efgh);
    }
    vst1q_u32(state, abcd);
    vst1q_u32(state + 4, efgh);
}

} // namespace nfsu
