#include "nfsu/screen_text.h"

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

bool same_rows(const std::uint8_t* a, const std::uint8_t* b) {
    for (std::uint32_t r = 0; r < nfsu::kScreenGlyphHeight; ++r) {
        if (a[r] != b[r]) {
            return false;
        }
    }
    return true;
}

// An image with a guard band on every side: pixels are only ever written
// inside [0, width) x [0, height), and the stride leaves padding at the end
// of every row that must never be touched either.
struct GuardedImage {
    static constexpr std::uint32_t kGuardRows = 2;
    static constexpr std::size_t kPadding = 12;
    std::uint32_t width;
    std::uint32_t height;
    std::size_t stride;
    std::vector<unsigned char> bytes;

    GuardedImage(std::uint32_t w, std::uint32_t h)
        : width(w), height(h), stride(std::size_t{w} * 4U + kPadding),
          bytes(stride * (h + 2U * kGuardRows), 0xA5U) {}

    nfsu::ScreenImage image() {
        return nfsu::ScreenImage{bytes.data() + stride * kGuardRows, width, height, stride};
    }

    const unsigned char* pixel(std::uint32_t x, std::uint32_t y) const {
        return bytes.data() + stride * (kGuardRows + y) + std::size_t{x} * 4U;
    }

    // True when every byte outside the image proper still holds the fill.
    bool guards_intact() const {
        for (std::size_t row = 0; row < height + 2U * kGuardRows; ++row) {
            const bool inside = row >= kGuardRows && row < kGuardRows + height;
            for (std::size_t column = 0; column < stride; ++column) {
                const bool in_image = inside && column < std::size_t{width} * 4U;
                if (!in_image && bytes[row * stride + column] != 0xA5U) {
                    return false;
                }
            }
        }
        return true;
    }
};

const nfsu::ScreenColor kInk{250, 240, 230, 255};
const nfsu::ScreenColor kPaper{1, 2, 3, 4};

bool is(const unsigned char* pixel, nfsu::ScreenColor color) {
    return pixel[0] == color.r && pixel[1] == color.g && pixel[2] == color.b &&
           pixel[3] == color.a;
}

// Every printable character has a glyph, five columns wide; the lower-case
// letters are the upper-case ones; the glyphs of distinct characters differ,
// which is what catches a row pasted into the wrong entry of the table.
void test_font_table() {
    const std::uint8_t* box = nfsu::screen_glyph_rows('\x01');
    for (int c = 0x20; c <= 0x7E; ++c) {
        const char ch = static_cast<char>(c);
        check(nfsu::screen_glyph_known(ch), "printable " + std::to_string(c) + " is known");
        const std::uint8_t* rows = nfsu::screen_glyph_rows(ch);
        for (std::uint32_t r = 0; r < nfsu::kScreenGlyphHeight; ++r) {
            check(rows[r] < (1U << nfsu::kScreenGlyphWidth),
                  "glyph " + std::to_string(c) + " fits five columns");
        }
        check(!same_rows(rows, box), "glyph " + std::to_string(c) + " is not the box");
    }
    for (char c = 'a'; c <= 'z'; ++c) {
        check(same_rows(nfsu::screen_glyph_rows(c),
                        nfsu::screen_glyph_rows(static_cast<char>(c - 'a' + 'A'))),
              std::string("lower case ") + c + " uses the upper-case glyph");
    }
    std::vector<int> drawn;
    for (int c = 0x20; c <= 0x7E; ++c) {
        if (c < 'a' || c > 'z') {
            drawn.push_back(c);
        }
    }
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            check(!same_rows(nfsu::screen_glyph_rows(static_cast<char>(drawn[i])),
                             nfsu::screen_glyph_rows(static_cast<char>(drawn[j]))),
                  "glyphs " + std::to_string(drawn[i]) + " and " + std::to_string(drawn[j]) +
                      " differ");
        }
    }
    // The box: a closed outline, for anything that is not printable ASCII.
    const std::uint8_t expected_box[7] = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F};
    check(same_rows(box, expected_box), "the box is an outline");
    for (const char c : {'\t', '\n', '\x7F', '\x80', '\xFF', '\0'}) {
        check(!nfsu::screen_glyph_known(c), "not printable is not known");
        check(same_rows(nfsu::screen_glyph_rows(c), box), "not printable draws the box");
    }
    // Two glyphs pinned row by row, so a table that is distinct and wrong
    // still fails.
    const std::uint8_t expected_a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    check(same_rows(nfsu::screen_glyph_rows('A'), expected_a), "A");
    const std::uint8_t expected_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    check(same_rows(nfsu::screen_glyph_rows('7'), expected_7), "7");
}

// One glyph at scale 1: its ink where the rows say, one pixel in from the
// cell's top-left corner, paper over the rest of the cell, nothing outside.
void test_draw_one_glyph() {
    GuardedImage target(16, 12);
    const std::int64_t width = nfsu::draw_screen_text(target.image(), 0, 0, "A", 1, kInk, kPaper);
    check(width == 6, "one cell is six pixels wide");
    const std::uint8_t* rows = nfsu::screen_glyph_rows('A');
    for (std::uint32_t y = 0; y < 12; ++y) {
        for (std::uint32_t x = 0; x < 16; ++x) {
            const unsigned char* pixel = target.pixel(x, y);
            if (x >= 6 || y >= 9) {
                check(pixel[0] == 0xA5U && pixel[3] == 0xA5U,
                      "outside the cell at " + std::to_string(x) + "," + std::to_string(y));
                continue;
            }
            const bool in_glyph = x >= 1 && x <= 5 && y >= 1 && y <= 7;
            const bool ink =
                in_glyph && ((rows[y - 1] >> (nfsu::kScreenGlyphWidth - x)) & 1U) != 0;
            check(is(pixel, ink ? kInk : kPaper),
                  "cell pixel at " + std::to_string(x) + "," + std::to_string(y));
        }
    }
    check(target.guards_intact(), "nothing written outside the image");
}

// Scale 3 multiplies everything: the cell, the offset and each font pixel.
void test_scale() {
    GuardedImage target(40, 30);
    const std::int64_t width = nfsu::draw_screen_text(target.image(), 2, 1, "-", 3, kInk, kPaper);
    check(width == 18, "a scale 3 cell is 18 pixels wide");
    // '-' inks row 3, all five columns: y = 1 + 3 * (3 + 1) .. +2, x = 2 + 3 .. 2 + 3 + 14.
    for (std::uint32_t y = 1; y < 1 + 27; ++y) {
        for (std::uint32_t x = 2; x < 2 + 18; ++x) {
            const bool ink = y >= 13 && y < 16 && x >= 5 && x < 20;
            check(is(target.pixel(x, y), ink ? kInk : kPaper),
                  "scaled pixel at " + std::to_string(x) + "," + std::to_string(y));
        }
    }
    check(target.guards_intact(), "nothing written outside the image");
}

// Text that runs off every edge is clipped, never written past the image,
// and the width returned is the whole line's.
void test_clipping() {
    GuardedImage target(20, 10);
    check(nfsu::draw_screen_text(target.image(), -7, -5, "WWWWWWWW", 2, kInk, kPaper) == 96,
          "the width of the whole line");
    check(nfsu::draw_screen_text(target.image(), 15, 6, "MMMM", 1, kInk, kPaper) == 24,
          "the width past the right edge");
    nfsu::draw_screen_text(target.image(), 1000, 1000, "far away", 4, kInk, kPaper);
    nfsu::fill_screen_rect(target.image(), -100, -100, 1000, 1000, kPaper);
    check(target.guards_intact(), "nothing written outside the image");
    check(is(target.pixel(0, 0), kPaper) && is(target.pixel(19, 9), kPaper),
          "the rectangle covered the whole image");

    // Degenerate images and rectangles draw nothing and do not fail.
    nfsu::fill_screen_rect(nfsu::ScreenImage{}, 0, 0, 10, 10, kInk);
    nfsu::ScreenImage narrow = target.image();
    narrow.stride = 4;  // shorter than a row
    nfsu::fill_screen_rect(narrow, 0, 0, 10, 10, kInk);
    nfsu::fill_screen_rect(target.image(), 5, 5, 0, 3, kInk);
    nfsu::fill_screen_rect(target.image(), 5, 5, -2, 3, kInk);
    check(nfsu::draw_screen_text(target.image(), 0, 0, "abc", 0, kInk, kInk) == 0,
          "scale 0 draws nothing");
    for (std::uint32_t y = 0; y < 10; ++y) {
        for (std::uint32_t x = 0; x < 20; ++x) {
            check(is(target.pixel(x, y), kPaper), "degenerate calls drew nothing");
        }
    }
    check(target.guards_intact(), "nothing written outside the image");
}

void test_wrap() {
    using Lines = std::vector<std::string>;
    check(nfsu::wrap_screen_text("hello world foo", 11) == Lines{"hello world", "foo"},
          "breaks at the last space in reach");
    check(nfsu::wrap_screen_text("hello world", 11) == Lines{"hello world"}, "fits exactly");
    check(nfsu::wrap_screen_text("abcdefghij", 4) == Lines{"abcd", "efgh", "ij"},
          "a word longer than a line is cut");
    check(nfsu::wrap_screen_text("one\ntwo three", 5) == Lines{"one", "two", "three"},
          "line breaks are kept");
    check(nfsu::wrap_screen_text("", 5) == Lines{""}, "empty text is one empty line");
    check(nfsu::wrap_screen_text("ab", 0) == Lines{"a", "b"}, "zero columns is one");
    check(nfsu::wrap_screen_text(" abcdef", 3) == Lines{" ab", "cde", "f"},
          "a leading space is not a break point");
}

} // namespace

int main() {
    test_font_table();
    test_draw_one_glyph();
    test_scale();
    test_clipping();
    test_wrap();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "screen text tests passed\n";
    return 0;
}
