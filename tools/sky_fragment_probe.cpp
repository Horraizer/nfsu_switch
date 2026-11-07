// Read-only desktop research executable for issue #227; not part of the race
// application. It replays event 1 through the same RaceGpuScene frames the
// Switch renderer executes and counts, at 1280x720, how many sky fragments
// pass the depth test in each submission order the race renderer can use.
// Coverage is geometric: a depth-writing draw rejects every sky fragment it
// covers, because the sky sits at normalized depth 0.999999 behind all world
// geometry inside the far plane. Alpha-test holes are not modelled, so a
// cutout draw counts as full coverage; the "visible" floor is a lower bound.
// This is fragment accounting, not a GPU timing.
#include "nfsu/deko_draw_order.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_gpu_scene.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace nfsu;
namespace sm = shader_manifest;
constexpr int kWidth = 1280, kHeight = 720;
constexpr std::size_t kUncovered = std::numeric_limits<std::size_t>::max();

void require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

struct Clip { double x, y, z, w, u, v; };

// placed.vert.glsl and sky.vert.glsl, in double precision.
Clip project(const DrawFrameUniform& f, const DrawInstanceUniform& placement,
             const float* position, double u, double v, bool sky) {
    const double slot[3] = {position[0], position[2], position[1]};
    double moved[3];
    for (int k = 0; k < 3; ++k)
        moved[k] = slot[0]*placement.rows[k] + slot[1]*placement.rows[4+k] +
                   slot[2]*placement.rows[8+k] + placement.rows[12+k];
    const double d[3] = {moved[0]-f.eye[0], moved[2]-f.eye[1], -moved[1]-f.eye[2]};
    const auto dot = [&](const float* axis) { return axis[0]*d[0] + axis[1]*d[1] + axis[2]*d[2]; };
    const double vz = dot(f.forward), near_plane = f.projection[2], far_plane = f.projection[3];
    Clip c{dot(f.right)*f.projection[0], dot(f.up)*f.projection[1],
           (vz-near_plane)*far_plane/(far_plane-near_plane), vz, u, v};
    if (sky) c.z = c.w*0.999999;
    return c;
}

// The 0 <= z <= w depth clip; x and y are bounded by the pixel loop.
std::vector<Clip> clip_depth(std::vector<Clip> polygon) {
    for (int plane = 0; plane < 2 && !polygon.empty(); ++plane) {
        std::vector<Clip> out;
        const auto distance = [&](const Clip& p) { return plane == 0 ? p.z : p.w - p.z; };
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Clip& a = polygon[i];
            const Clip& b = polygon[(i+1) % polygon.size()];
            const double da = distance(a), db = distance(b);
            if (da >= 0) out.push_back(a);
            if ((da >= 0) != (db >= 0)) {
                const double t = da/(da-db);
                out.push_back({a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t,
                               a.w+(b.w-a.w)*t, a.u+(b.u-a.u)*t, a.v+(b.v-a.v)*t});
            }
        }
        polygon.swap(out);
    }
    return polygon;
}

// Pixel centres inside the triangle; the callback gets the texel footprint
// (log2 of the larger screen-axis derivative, in texels) that a mip
// selection would use.
template <class Pixel>
void rasterize(const Clip& a, const Clip& b, const Clip& c, double texels_u, double texels_v,
               Pixel&& pixel) {
    struct Screen { double x, y, inverse_w, u, v; };
    const auto screen = [](const Clip& p) {
        return Screen{(p.x/p.w*0.5+0.5)*kWidth, (0.5-p.y/p.w*0.5)*kHeight, 1/p.w, p.u/p.w, p.v/p.w};
    };
    const Screen s[3] = {screen(a), screen(b), screen(c)};
    const double area = (s[1].x-s[0].x)*(s[2].y-s[0].y) - (s[2].x-s[0].x)*(s[1].y-s[0].y);
    if (std::fabs(area) < 1e-12) return;
    const auto weights = [&](double x, double y, double* l) {
        l[0] = ((s[1].x-x)*(s[2].y-y) - (s[2].x-x)*(s[1].y-y))/area;
        l[1] = ((s[2].x-x)*(s[0].y-y) - (s[0].x-x)*(s[2].y-y))/area;
        l[2] = 1-l[0]-l[1];
    };
    const auto uv = [&](const double* l, double& u, double& v) {
        const double w = l[0]*s[0].inverse_w + l[1]*s[1].inverse_w + l[2]*s[2].inverse_w;
        u = (l[0]*s[0].u + l[1]*s[1].u + l[2]*s[2].u)/w;
        v = (l[0]*s[0].v + l[1]*s[1].v + l[2]*s[2].v)/w;
    };
    const int x0 = std::max(0, int(std::floor(std::min({s[0].x, s[1].x, s[2].x}))));
    const int x1 = std::min(kWidth-1, int(std::ceil(std::max({s[0].x, s[1].x, s[2].x}))));
    const int y0 = std::max(0, int(std::floor(std::min({s[0].y, s[1].y, s[2].y}))));
    const int y1 = std::min(kHeight-1, int(std::ceil(std::max({s[0].y, s[1].y, s[2].y}))));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double l[3], lx[3], ly[3], u, v, ux, vx, uy, vy;
            weights(x+0.5, y+0.5, l);
            if (l[0] < 0 || l[1] < 0 || l[2] < 0) continue;
            weights(x+1.5, y+0.5, lx);
            weights(x+0.5, y+1.5, ly);
            uv(l, u, v); uv(lx, ux, vx); uv(ly, uy, vy);
            const double fx = std::hypot((ux-u)*texels_u, (vx-v)*texels_v);
            const double fy = std::hypot((uy-u)*texels_u, (vy-v)*texels_v);
            pixel(y*kWidth+x, std::log2(std::max(1e-9, std::max(fx, fy))));
        }
}

bool is_sky(const RecordedDraw& d) { return std::strcmp(d.program->name, "sky_textured") == 0; }

// Submission order of the static track draws followed by the car draws.
// list: before #227 (no reordering). early/on: the race renderer's
// opaque-first order with the sky in or after the opaque pass.
std::vector<std::size_t> submission_order(const RecordedCommands& commands, int mode) {
    const auto track = commands.static_track_draw_count();
    std::vector<std::size_t> order;
    if (mode == 0) {
        for (std::size_t i = 0; i < track; ++i) order.push_back(i);
    } else {
        deko_visit_opaque_first(track, [&](std::size_t i) {
            const auto& d = commands.draw_view(i).definition;
            return deko_draw_in_opaque_pass(is_sky(d), d.pipeline.blend,
                d.pipeline.alpha_test != sm::CompareOp::always, d.pipeline.depth_write, mode == 1);
        }, [&](std::size_t i) { order.push_back(i); });
    }
    for (std::size_t i = track; i < commands.draw_count(); ++i) order.push_back(i);
    return order;
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 4, "usage: nfsu_sky_fragment_probe RETAIL tests/recordings/event1.txt EVERY_N_STEPS");
        const std::filesystem::path root(argv[1]);
        std::ifstream in(argv[2], std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        const long every = std::strtol(argv[3], nullptr, 10);
        require(bool(in) && every > 0, "cannot read the recording or the sampling interval");
        std::string error;
        RaceGpuScene scene; RaceRun race; RaceReplay replay; GamepadFrame pad;
        require(race.prepare_metadata(root, error), error);
        require(scene.load(posix_file_system(), root, error), error);
        require(race.start(root, scene.track(), error), error);
        require(replay.load(text, error), error);
        const char* modes[3] = {"list", "early", "on"};
        std::vector<std::size_t> first_cover[3];
        for (auto& buffer : first_cover) buffer.resize(std::size_t{kWidth}*kHeight);
        std::vector<unsigned char> any_cover(std::size_t{kWidth}*kHeight);
        double total = 0, visible = 0, minified = 0, lod = 0, passed[3] = {0, 0, 0};
        std::size_t frames = 0;
        while (race.session().timing().outcome == RaceOutcome::running) {
            require(replay.next(race.session().timing().step, pad, error), error);
            require(race.simulate(pad, error), error);
            require(race.commit(error), error);
            if (race.session().timing().step % static_cast<std::uint64_t>(every)) continue;
            RaceGpuFrame frame;
            require(scene.frame(race.session(), frame, error), error);
            const auto& commands = frame.commands;
            std::vector<std::size_t> rank[3];
            for (int m = 0; m < 3; ++m) {
                const auto order = submission_order(commands, m);
                rank[m].assign(commands.draw_count(), 0);
                for (std::size_t r = 0; r < order.size(); ++r) rank[m][order[r]] = r;
                std::fill(first_cover[m].begin(), first_cover[m].end(), kUncovered);
            }
            std::fill(any_cover.begin(), any_cover.end(), 0);
            struct Fragment { std::size_t pixel, draw; double lod; };
            std::vector<Fragment> sky;
            const auto& table = commands.resource_table();
            for (std::size_t i = 0; i < commands.draw_count(); ++i) {
                const auto view = commands.draw_view(i);
                const auto& d = view.definition;
                const bool sky_draw = is_sky(d);
                if (!sky_draw && !d.pipeline.depth_write) continue;
                const auto& vertices = table.resources.at(d.vertex_resource);
                const auto* indices = reinterpret_cast<const std::uint16_t*>(
                    table.resources.at(d.index_resource).data);
                const auto stride = d.program->buffers[0].stride;
                std::size_t uv_offset = kNoHandle;
                for (std::size_t a = 0; a < d.program->attribute_count; ++a)
                    if (std::strcmp(d.program->attributes[a].name, "texcoord") == 0)
                        uv_offset = d.program->attributes[a].offset;
                double texels_u = 1, texels_v = 1;
                if (sky_draw) {
                    const auto& texture = table.resources.at(d.texture_resource).texture;
                    texels_u = texture.width; texels_v = texture.height;
                }
                for (std::uint32_t t = 0; t + 2 < d.index_count; t += 3) {
                    std::vector<Clip> corners;
                    for (int k = 0; k < 3; ++k) {
                        const auto* vertex = vertices.data +
                            std::size_t(d.first_vertex + indices[d.first_index + t + k])*stride;
                        float position[3], u = 0, v = 0;
                        std::memcpy(position, vertex, sizeof position);
                        if (uv_offset != kNoHandle) {
                            std::memcpy(&u, vertex + uv_offset, sizeof u);
                            std::memcpy(&v, vertex + uv_offset + sizeof u, sizeof v);
                        }
                        corners.push_back(project(commands.frame, view.placement, position, u, v, sky_draw));
                    }
                    const auto polygon = clip_depth(corners);
                    for (std::size_t k = 1; k + 1 < polygon.size(); ++k)
                        rasterize(polygon[0], polygon[k], polygon[k+1], texels_u, texels_v,
                            [&](std::size_t pixel, double footprint) {
                                if (sky_draw) { sky.push_back({pixel, i, footprint}); return; }
                                any_cover[pixel] = 1;
                                for (int m = 0; m < 3; ++m)
                                    first_cover[m][pixel] = std::min(first_cover[m][pixel], rank[m][i]);
                            });
                }
            }
            for (const auto& f : sky) {
                total += 1;
                for (int m = 0; m < 3; ++m)
                    if (first_cover[m][f.pixel] == kUncovered || first_cover[m][f.pixel] > rank[m][f.draw])
                        passed[m] += 1;
                if (!any_cover[f.pixel]) { visible += 1; lod += f.lod; if (f.lod > 0) minified += 1; }
            }
            ++frames;
        }
        require(frames > 0, "no frame sampled");
        std::cout << "frames " << frames << " every " << every << " steps at " << kWidth << "x" << kHeight << '\n';
        std::cout << "sky fragments per frame: rasterized " << std::llround(total/frames);
        for (int m = 0; m < 3; ++m) std::cout << " | pass depth " << modes[m] << ' ' << std::llround(passed[m]/frames);
        std::cout << " | uncovered " << std::llround(visible/frames) << '\n';
        std::cout << "uncovered sky texel footprint: mean log2 " << (visible ? lod/visible : 0)
                  << " minified share " << (visible ? minified/visible : 0) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
