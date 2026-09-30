#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {
// Seven std140 vectors shared by the cheap static-environment approximation.
// Record values are decoded, not tuned. Only lighting/capture are approximated.
struct CarShadingMaterial {
    float diffuse_min[4]{};
    float diffuse_range[4]{};
    float specular_min[4]{}; // w: SpecularPower, traced initial multiplier 1.1
    float specular_range[4]{}; // w: EnvmapPower
    float environment_min[4]{};
    float environment_range[4]{};
    float gates[4]{}; // x: enabled, y: diffuse/specular, z: environment
};
bool valid_car_shading_material(const CarShadingMaterial& material) noexcept;
struct CarMaterialRecord {
    std::uint32_t hash = 0;
    CarShadingMaterial values;
};
struct PreparedCarScene;
// Bounded top-level GLOBALB.BUN walk. Invalid records fail transactionally.
bool parse_car_materials(const unsigned char* bytes, std::size_t size,
                         std::vector<CarMaterialRecord>& out, std::string& error);
// Join geometry material keys. Optional blue paint maps CARSKIN to
// REGPAINTBLUE: an explicit approximation of the untraced runtime paint object.
bool apply_car_materials(PreparedCarScene& scene, const unsigned char* bytes,
                         std::size_t size, bool blue_paint, std::string& error);
} // namespace nfsu
