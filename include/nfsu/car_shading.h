#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nfsu {
// Seven decoded std140 vectors shared by car render consumers.
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
// Original FE harmonic record (+0x120), supplied independently of materials.
// The startup record below has explicit unresolved zero-BSS input assumptions;
// it is a bounded diagnostic state, not a retail profile-default claim.
struct FrontendLighting {
    float harmonic[10][4]{};
};
struct FrontendPointLight {
    float position[3]{};
    float rgb[3]{};
    float radius=23.3700008392334F;
    float attenuation[3]{};
};
// Original FE main-camera/reflection basis, with explicit startup assumptions.
// Point records come from the moving road producer, never invented lights.
FrontendLighting frontend_point_lighting(const FrontendPointLight* lights,std::size_t count,
    const float (&raw_car_position)[3]) noexcept;
FrontendLighting frontend_startup_lighting() noexcept;
// Native face projection, with samples clamped to each logical128-square
// face's texel centers before atlas packing; unrelated tiles cannot bleed.
bool frontend_cube_coordinates(const double (&direction)[3],unsigned& face,
    double& u,double& v) noexcept;
void frontend_local_eye(const float* placement,const double* raw_eye,float (&out)[4]) noexcept;
void frontend_surface(const FrontendLighting& lighting,const CarShadingMaterial& material,
    const float* raw_position,const float* raw_normal,const float* local_eye,
    double (&out)[7]) noexcept;
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
