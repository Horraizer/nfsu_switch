// PC1.4 USA6EF2D0 base materials and441D80 positional height modulation.
namespace {
constexpr std::array<PlayerSurfaceFactors,29> kPlayerSurfaces{{
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 0 NONE
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 1 ROAD
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 2 ROAD_WET
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 3 ROAD_DRIFT
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 4 ROAD_SMOKE_1
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 5 ROAD_SMOKE_2
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 6 ROAD_SMOKE_3
    {1.0F,1.0F,1.0F,1.0F,0.0F}, // 7 ROAD_BRIDGE
    {0.9700000286102295F,0.9578750133514404F,1.0F,1.0F,0.05000000074505806F}, // 8 DIRT
    {0.9900000095367432F,0.9812999963760376F,1.0F,1.0F,0.019999999552965164F}, // 9 GRAVEL
    {1.0F,1.0F,1.0099999904632568F,1.0F,0.019999999552965164F}, // 10 ROUGH_ROAD
    {1.0F,1.0F,1.0199999809265137F,3.0F,0.04800000041723251F}, // 11 COBBLESTONE
    {1.0F,1.0F,1.0199999809265137F,3.0F,0.04800000041723251F}, // 12 STAIRS
    {0.9599999785423279F,0.9513000249862671F,1.100000023841858F,1.0F,0.009999999776482582F}, // 13 PUDDLE
    {0.9599999785423279F,0.9480000138282776F,1.5F,1.0F,0.029999999329447746F}, // 14 DEEP_WATER
    {0.9599999785423279F,0.9480000138282776F,1.0499999523162842F,1.0F,0.05000000074505806F}, // 15 GRASS
    {1.0F,1.0F,1.0F,1.100000023841858F,0.029999999329447746F}, // 16 SIDEWALK
    {1.0F,1.0F,1.0F,1.0F,0.009999999776482582F}, // 17 WOOD
    {1.0F,1.0F,1.0F,1.0F,0.009999999776482582F}, // 18 PLASTIC
    {1.0F,1.0F,1.0F,1.0F,0.009999999776482582F}, // 19 GLASS
    {1.0F,1.0F,1.0F,1.0F,0.009999999776482582F}, // 20 SOLID_WALL
    {1.0F,1.0F,1.0F,1.0F,0.009999999776482582F}, // 21 SEE_THROUGH_WALL
    {0.9900000095367432F,0.9810000061988831F,1.0F,1.0F,0.0F}, // 22 PLANT
    {0.9900000095367432F,0.9810000061988831F,1.0F,1.0F,0.0F}, // 23 POST
    {0.9900000095367432F,0.9810000061988831F,1.0F,1.0F,0.0F}, // 24 PILLAR
    {0.9900000095367432F,0.9812999963760376F,1.0F,1.0F,0.0F}, // 25 METAL_GRATE
    {0.9900000095367432F,0.9810000061988831F,1.0F,1.0F,0.0F}, // 26 METAL
    {0.9900000095367432F,0.9810000061988831F,1.0F,1.0F,0.0F}, // 27 CHAINLINK
    {0.9900000095367432F,0.9812999963760376F,1.0F,1.0F,0.0F}, // 28 CAR
}};
constexpr std::array<float,16> kSurfaceNoise{{
    -0.5F,-0.20000000298023224F,0.5F,0.20000000298023224F,-0.4000000059604645F,-0.10000000149011612F,0.20000000298023224F,0.5F,-0.20000000298023224F,-0.5F,0.4000000059604645F,-0.30000001192092896F,0.30000001192092896F,0.0F,-0.20000000298023224F,0.10000000149011612F
}};
} // namespace

bool player_surface_factors(std::uint32_t material,PlayerSurfaceFactors& out) noexcept {
    if(material>=kPlayerSurfaces.size())return false;
    out=kPlayerSurfaces[material];return true;
}

bool player_surface_height(std::uint32_t material,float x,float y,float& out) noexcept {
    if(material>=kPlayerSurfaces.size())return false;
    const auto& s=kPlayerSurfaces[material];
    const float sx=x*s.bump_frequency,sy=y*s.bump_frequency;
    if(!std::isfinite(sx)||!std::isfinite(sy)||sx< -2147483648.F||sx>=2147483648.F||sy< -2147483648.F||sy>=2147483648.F)return false;
    const float ix=std::floor(sx),iy=std::floor(sy),fx=sx-ix,fy=sy-iy;
    const auto ax=static_cast<std::uint32_t>(static_cast<std::int32_t>(ix));
    const auto ay=static_cast<std::uint32_t>(static_cast<std::int32_t>(iy));
    const auto sample=[](std::uint32_t a,std::uint32_t b) {
        return kSurfaceNoise[(-5U*a*b-3U*(a+1U)*(b+1U))&15U];
    };
    const float a=(sample(ax,ay)*(1.F-fx))*(1.F-fy);
    const float b=(sample(ax+1U,ay)*(1.F-fy))*fx;
    const float c=(sample(ax,ay+1U)*(1.F-fx))*fy;
    const float d=(sample(ax+1U,ay+1U)*fy)*fx;
    out=(((a+b)+c)+d)*s.bump_amplitude;return true;
}
