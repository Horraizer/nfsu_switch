#pragma once
#include "nfsu/car_shadow.h"
namespace nfsu {
struct CarNeonSettings {std::array<int,4> base{},delta{};};
struct CarNeonAsset {CarShadowAsset ground;CarNeonSettings color;bool enabled=false;};
// Native56AC73 resource and56F061 color controller. Reuse the shadow's
// sampled terrain; an installed light adds no terrain/physics query.
bool prepare_car_neon(const StockFrameInputs&,const StockFrame&,const CarShadowAsset&,
                      CarNeonAsset&,std::string& error);
std::uint32_t car_neon_color(const CarNeonSettings&,float seconds,float strength) noexcept;
void update_car_neon_draw(const CarNeonSettings&,float seconds,const CarShadowPlacement&,RecordedDraw&);
bool record_car_neon_commands(const CarNeonAsset&,float seconds,const CarShadowPlacement&,
                              const RenderOptions&,RecordedCommands&,std::string& error);
bool render_car_neon(const CarNeonAsset&,float seconds,const CarShadowPlacement&,
                     const RenderOptions&,Frame&,RenderStats&,std::string& error);
}
