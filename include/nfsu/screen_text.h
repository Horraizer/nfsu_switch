#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Text drawn into an RGBA8 image with a built-in 5x7 pixel font.
//
// The Switch application shows its rendered frame through a libnx
// framebuffer, and libnx's text console draws into a framebuffer of its own
// on the same window, so the two cannot be on screen together; nxlink also
// takes stdout away from the console. The report - which file is missing,
// the render time, the frame's SHA-256 - is therefore drawn with this, into
// the same image as the frame. Standard library only, integer arithmetic
// only, no state.

// Each glyph is kScreenGlyphWidth x kScreenGlyphHeight font pixels in a
// kScreenCellWidth x kScreenCellHeight cell, so neighbouring characters and
// lines never touch.
constexpr std::uint32_t kScreenGlyphWidth = 5;
constexpr std::uint32_t kScreenGlyphHeight = 7;
constexpr std::uint32_t kScreenCellWidth = 6;
constexpr std::uint32_t kScreenCellHeight = 9;

// Pixels in R, G, B, A byte order, rows stride bytes apart; stride is at
// least width * 4. nfsu::Frame::rgba() is this with stride width * 4, and
// so is libnx's PIXEL_FORMAT_RGBA_8888 framebuffer with the stride
// framebufferBegin returns.
struct ScreenImage {
    unsigned char* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t stride = 0;
};

struct ScreenColor {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

// The rows of c's glyph, top first, bit 4 the leftmost column. Every
// printable ASCII character from 0x20 to 0x7E has one; a lower-case letter
// is drawn with its upper-case glyph. Anything else gets a hollow box, so a
// byte the font does not know is visible rather than dropped.
const std::uint8_t* screen_glyph_rows(char c) noexcept;

// True when c is printable ASCII, 0x20..0x7E.
bool screen_glyph_known(char c) noexcept;

// Fills a rectangle, clipped to the image. A null or empty image, or an
// empty rectangle, draws nothing.
void fill_screen_rect(
    const ScreenImage& image,
    std::int64_t x,
    std::int64_t y,
    std::int64_t width,
    std::int64_t height,
    ScreenColor color);

// Draws one line with the top-left corner of its first cell at (x, y), each
// font pixel a scale x scale square of ink over cells filled with paper, so
// the text reads over any frame. Everything is clipped to the image; a
// scale of 0 draws nothing. Returns the width of the line's cells in
// pixels, whether or not it fitted.
std::int64_t draw_screen_text(
    const ScreenImage& image,
    std::int64_t x,
    std::int64_t y,
    const std::string& text,
    std::uint32_t scale,
    ScreenColor ink,
    ScreenColor paper);

// Splits text into lines of at most columns characters, breaking after the
// last space in reach and otherwise mid-word; the spaces a line breaks at
// are dropped. Line breaks in the text are kept. columns 0 is treated as 1.
std::vector<std::string> wrap_screen_text(const std::string& text, std::size_t columns);

} // namespace nfsu
