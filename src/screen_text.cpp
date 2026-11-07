#include "nfsu/screen_text.h"

#include <array>

namespace nfsu {

namespace {

// The font, drawn: '#' is ink, '.' is paper, five columns by seven rows.
// Lower-case letters are not listed; they use the upper-case rows.
struct GlyphArt {
    char c;
    const char* rows[kScreenGlyphHeight];
};

constexpr GlyphArt kArt[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'"', {".#.#.", ".#.#.", ".#.#.", ".....", ".....", ".....", "....."}},
    {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {'$', {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'*', {".....", "..#..", "#.#.#", ".###.", "#.#.#", "..#..", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'@', {".###.", "#...#", "....#", ".##.#", "#.#.#", "#.#.#", ".###."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
    {'\\', {".....", "#....", ".#...", "..#..", "...#.", "....#", "....."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
    {'^', {"..#..", ".#.#.", "#...#", ".....", ".....", ".....", "....."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {'`', {".#...", "..#..", "...#.", ".....", ".....", ".....", "....."}},
    {'{', {"...#.", "..#..", "..#..", ".#...", "..#..", "..#..", "...#."}},
    {'|', {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'}', {".#...", "..#..", "..#..", "...#.", "..#..", "..#..", ".#..."}},
    {'~', {".....", ".....", ".#...", "#.#.#", "...#.", ".....", "....."}},
};

constexpr GlyphArt kUnknownArt = {
    '?', {"#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#####"}};

using GlyphRows = std::array<std::uint8_t, kScreenGlyphHeight>;

GlyphRows to_rows(const GlyphArt& art) {
    GlyphRows rows{};
    for (std::uint32_t r = 0; r < kScreenGlyphHeight; ++r) {
        std::uint8_t bits = 0;
        for (std::uint32_t c = 0; c < kScreenGlyphWidth; ++c) {
            bits = static_cast<std::uint8_t>(bits << 1U);
            if (art.rows[r][c] == '#') {
                bits = static_cast<std::uint8_t>(bits | 1U);
            }
        }
        rows[r] = bits;
    }
    return rows;
}

// Index 0..94 for 0x20..0x7E, 95 for the box.
struct Font {
    std::array<GlyphRows, 96> glyphs{};

    Font() {
        for (GlyphRows& rows : glyphs) {
            rows = to_rows(kUnknownArt);
        }
        for (const GlyphArt& art : kArt) {
            const std::size_t index = static_cast<unsigned char>(art.c) - 0x20U;
            glyphs[index] = to_rows(art);
        }
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            glyphs[c - 0x20U] = glyphs[c - 'a' + 'A' - 0x20U];
        }
    }
};

const Font& font() {
    static const Font instance;
    return instance;
}

} // namespace

bool screen_glyph_known(char c) noexcept {
    const unsigned char byte = static_cast<unsigned char>(c);
    return byte >= 0x20U && byte <= 0x7EU;
}

const std::uint8_t* screen_glyph_rows(char c) noexcept {
    const Font& table = font();
    if (!screen_glyph_known(c)) {
        return table.glyphs[95].data();
    }
    return table.glyphs[static_cast<unsigned char>(c) - 0x20U].data();
}

void fill_screen_rect(
    const ScreenImage& image,
    std::int64_t x,
    std::int64_t y,
    std::int64_t width,
    std::int64_t height,
    ScreenColor color) {
    if (image.pixels == nullptr || image.width == 0 || image.height == 0 || width <= 0 ||
        height <= 0 || image.stride < std::size_t{image.width} * 4U) {
        return;
    }
    std::int64_t x0 = x;
    std::int64_t y0 = y;
    std::int64_t x1 = x + width;
    std::int64_t y1 = y + height;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > static_cast<std::int64_t>(image.width)) {
        x1 = image.width;
    }
    if (y1 > static_cast<std::int64_t>(image.height)) {
        y1 = image.height;
    }
    for (std::int64_t row = y0; row < y1; ++row) {
        unsigned char* out = image.pixels + static_cast<std::size_t>(row) * image.stride;
        for (std::int64_t column = x0; column < x1; ++column) {
            unsigned char* pixel = out + static_cast<std::size_t>(column) * 4U;
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
            pixel[3] = color.a;
        }
    }
}

std::int64_t draw_screen_text(
    const ScreenImage& image,
    std::int64_t x,
    std::int64_t y,
    const std::string& text,
    std::uint32_t scale,
    ScreenColor ink,
    ScreenColor paper) {
    const std::int64_t cell_width = static_cast<std::int64_t>(kScreenCellWidth) * scale;
    const std::int64_t cell_height = static_cast<std::int64_t>(kScreenCellHeight) * scale;
    const std::int64_t total = cell_width * static_cast<std::int64_t>(text.size());
    if (scale == 0) {
        return total;
    }
    fill_screen_rect(image, x, y, total, cell_height, paper);
    // The glyph sits one font pixel below the top of its cell and one right
    // of its left edge, with the rest of the cell as spacing.
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::uint8_t* rows = screen_glyph_rows(text[i]);
        const std::int64_t left = x + cell_width * static_cast<std::int64_t>(i) + scale;
        for (std::uint32_t r = 0; r < kScreenGlyphHeight; ++r) {
            for (std::uint32_t c = 0; c < kScreenGlyphWidth; ++c) {
                if ((rows[r] >> (kScreenGlyphWidth - 1U - c)) & 1U) {
                    fill_screen_rect(image, left + static_cast<std::int64_t>(c) * scale,
                                     y + static_cast<std::int64_t>(r + 1U) * scale, scale,
                                     scale, ink);
                }
            }
        }
    }
    return total;
}

std::vector<std::string> wrap_screen_text(const std::string& text, std::size_t columns) {
    if (columns == 0) {
        columns = 1;
    }
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', start);
        std::string paragraph = text.substr(
            start, newline == std::string::npos ? std::string::npos : newline - start);
        while (paragraph.size() > columns) {
            std::size_t cut = paragraph.rfind(' ', columns);
            if (cut == std::string::npos || cut == 0) {
                lines.push_back(paragraph.substr(0, columns));
                paragraph.erase(0, columns);
            } else {
                lines.push_back(paragraph.substr(0, cut));
                paragraph.erase(0, cut + 1);
            }
        }
        lines.push_back(paragraph);
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

} // namespace nfsu
