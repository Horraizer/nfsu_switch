#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace nfsu {
struct CareerMagazineRecipe {
    std::uint32_t key=0,background=0,front=0,chick=0,flags=0;
    std::array<float,11> camera{};
    std::array<std::uint32_t,40> caption{};
    std::array<int,6> choices{};
};
// Authored30230 lists. Linked pointers in retail are loader fixups, never
// offsets or portable addresses. Selection consumes the original sound RNG.
class CareerMagazineCatalog {
public:
    bool load(const std::vector<unsigned char>& globals,std::string& error);
    bool make(std::uint32_t key,std::uint32_t& random,CareerMagazineRecipe& out,std::string& error)const;
    static std::uint32_t key(unsigned index);
private:
    std::array<std::vector<std::vector<std::uint32_t>>,8> pools_;
};
}
