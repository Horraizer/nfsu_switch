#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
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
struct CarPartDatabase;
// Bounded top-level GLOBALB.BUN walk. Invalid records fail transactionally.
bool parse_car_materials(const unsigned char* bytes, std::size_t size,
                         std::vector<CarMaterialRecord>& out, std::string& error);
// Resolve slot 30's 0x6BA02C05 attribute. A color-only skin uses the nearest
// GLOSS_L1_COLOR swatch as its surrogate installed BASE_PAINT part.
// Zero means no attribute; apply_car_materials then uses DEFAULT.
std::uint32_t select_car_paint_material_hash(const CarPartDatabase& database,
    std::size_t installed_base_paint, std::optional<std::uint32_t> skin_color);
// Join CARSKIN to the selected paint record, or DEFAULT if it is absent.
bool apply_car_materials(PreparedCarScene& scene, const unsigned char* bytes,
                         std::size_t size, std::uint32_t paint_hash, std::string& error);
} // namespace nfsu
