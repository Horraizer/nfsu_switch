#include "nfsu/sha256.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string digest_of(const std::string& message) {
    return lower(nfsu::sha256_hex(nfsu::sha256(message.data(), message.size())));
}

// The examples FIPS 180-2 appendix B and the NIST example set publish for
// SHA-256: the empty message, "abc" (one block), the 448-bit message whose
// padding needs a second block, the 896-bit message, and one million 'a'.
void test_published_vectors() {
    check(digest_of("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty message");
    check(digest_of("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc");
    check(digest_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "448-bit message");
    check(digest_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklm"
                    "nopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
          "896-bit message");
    check(digest_of(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "one million 'a'");
}

std::vector<unsigned char> pattern(std::size_t size) {
    std::vector<unsigned char> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<unsigned char>((i * 7 + 3) & 0xFFU);
    }
    return bytes;
}

// The lengths on either side of the padding boundaries: 55 bytes is the
// longest message whose length field still fits in its last block, 56 the
// shortest that needs another, and 64 and 128 end exactly on a block. The
// expected digests were computed with Python's hashlib, an implementation
// independent of this one, over bytes (i * 7 + 3) mod 256.
void test_padding_boundaries() {
    struct Case {
        std::size_t size;
        const char* digest;
    };
    const Case cases[] = {
        {55, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b"},
        {56, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27"},
        {63, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055"},
        {64, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241"},
        {65, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e"},
        {119, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e"},
        {120, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5"},
        {127, "a8d23e75d936f303d248888d9b165ee543f4cbafcad3c9dd2a79bd84faa11d07"},
        {128, "d2742f1f4ac6bb7ca2b239ee18402ba8b3f9f8e652d2a72973c2b9ba11c08cf6"},
        {129, "307f8fc2c1622b92762e818d39a185d4d667ad49a4b07ceae1f4afa008a93ec4"},
    };
    for (const Case& c : cases) {
        const std::vector<unsigned char> bytes = pattern(c.size);
        check(lower(nfsu::sha256_hex(nfsu::sha256(bytes.data(), bytes.size()))) == c.digest,
              "pattern of " + std::to_string(c.size) + " bytes");
    }
}

// Splitting a message across update calls, anywhere, gives the digest of
// the whole: every split point of a 200-byte message, then the message fed
// one byte at a time.
void test_incremental_updates() {
    const std::vector<unsigned char> bytes = pattern(200);
    const nfsu::Sha256Digest whole = nfsu::sha256(bytes.data(), bytes.size());
    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        nfsu::Sha256 hash;
        hash.update(bytes.data(), split);
        hash.update(bytes.data() + split, bytes.size() - split);
        check(hash.finish() == whole, "split at " + std::to_string(split));
    }
    nfsu::Sha256 bytewise;
    for (const unsigned char byte : bytes) {
        bytewise.update(&byte, 1);
    }
    check(bytewise.finish() == whole, "one byte at a time");

    // A null pointer with no bytes is an empty update.
    nfsu::Sha256 empty;
    empty.update(nullptr, 0);
    check(lower(nfsu::sha256_hex(empty.finish())) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "null empty update");
}

// finish() leaves a new, empty message, so one object hashes several.
void test_reuse_after_finish() {
    nfsu::Sha256 hash;
    hash.update("abc", 3);
    const nfsu::Sha256Digest first = hash.finish();
    hash.update("abc", 3);
    check(hash.finish() == first, "the same message again");
    check(lower(nfsu::sha256_hex(hash.finish())) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "nothing after finish is the empty message");
}

void test_hex_spelling() {
    nfsu::Sha256Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::uint8_t>(i * 8 + 1);
    }
    const std::string text = nfsu::sha256_hex(digest);
    check(text.size() == 64, "64 digits");
    check(text.substr(0, 8) == "01091119", "leading bytes");
    check(text.substr(56) == "E1E9F1F9", "trailing bytes, upper case");
}

} // namespace

int main() {
    test_published_vectors();
    test_padding_boundaries();
    test_incremental_updates();
    test_reuse_after_finish();
    test_hex_spelling();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "sha256 tests passed\n";
    return 0;
}
