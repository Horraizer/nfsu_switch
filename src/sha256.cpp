#include "nfsu/sha256.h"

#include <cstring>

namespace nfsu {

namespace {

// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the
// cube roots of the first 64 primes.
constexpr std::uint32_t kRound[64] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U,
    0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U,
    0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
    0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
    0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU,
    0x5B9CCA4FU, 0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U};

// Section 5.3.3: the first 32 bits of the fractional parts of the square
// roots of the first 8 primes.
constexpr std::uint32_t kInitial[8] = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U,
                                       0xA54FF53AU, 0x510E527FU, 0x9B05688CU,
                                       0x1F83D9ABU, 0x5BE0CD19U};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int count) noexcept {
    return (value >> count) | (value << (32U - count));
}

} // namespace

Sha256::Sha256() noexcept {
    reset();
}

void Sha256::reset() noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        state_[i] = kInitial[i];
    }
    block_.fill(0);
    block_used_ = 0;
    length_ = 0;
}

// Section 6.2.2, one 512-bit block, words read big-endian.
void Sha256::compress(const std::uint8_t* block) noexcept {
    std::uint32_t w[64];
    for (std::size_t t = 0; t < 16; ++t) {
        w[t] = (static_cast<std::uint32_t>(block[t * 4]) << 24U) |
               (static_cast<std::uint32_t>(block[t * 4 + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[t * 4 + 2]) << 8U) |
               static_cast<std::uint32_t>(block[t * 4 + 3]);
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t s0 =
            rotate_right(w[t - 15], 7) ^ rotate_right(w[t - 15], 18) ^ (w[t - 15] >> 3U);
        const std::uint32_t s1 =
            rotate_right(w[t - 2], 17) ^ rotate_right(w[t - 2], 19) ^ (w[t - 2] >> 10U);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t big1 =
            rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + big1 + choose + kRound[t] + w[t];
        const std::uint32_t big0 =
            rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = big0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) noexcept {
    if (size == 0) {
        return;
    }
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    length_ += static_cast<std::uint64_t>(size);
    while (size > 0) {
        if (block_used_ == 0 && size >= block_.size()) {
            compress(bytes);
            bytes += block_.size();
            size -= block_.size();
            continue;
        }
        std::size_t take = block_.size() - block_used_;
        if (take > size) {
            take = size;
        }
        std::memcpy(block_.data() + block_used_, bytes, take);
        block_used_ += take;
        bytes += take;
        size -= take;
        if (block_used_ == block_.size()) {
            compress(block_.data());
            block_used_ = 0;
        }
    }
}

Sha256Digest Sha256::finish() noexcept {
    const std::uint64_t bits = length_ * 8U;
    // A single 1 bit, then zeros up to 56 bytes into a block, then the
    // 64-bit big-endian bit length.
    block_[block_used_] = 0x80U;
    ++block_used_;
    if (block_used_ > 56) {
        std::memset(block_.data() + block_used_, 0, block_.size() - block_used_);
        compress(block_.data());
        block_used_ = 0;
    }
    std::memset(block_.data() + block_used_, 0, 56 - block_used_);
    for (std::size_t i = 0; i < 8; ++i) {
        block_[56 + i] = static_cast<std::uint8_t>(bits >> (8U * (7U - i)));
    }
    compress(block_.data());

    Sha256Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24U);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16U);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8U);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    reset();
    return digest;
}

Sha256Digest sha256(const void* data, std::size_t size) noexcept {
    Sha256 hash;
    hash.update(data, size);
    return hash.finish();
}

std::string sha256_hex(const Sha256Digest& digest) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

} // namespace nfsu
