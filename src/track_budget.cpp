#include "nfsu/track_budget.h"

#include <cstddef>

namespace nfsu {
namespace {

// The one table. track_budget_entries, track_budget_value and
// check_track_budget all read it, so a counter added to TrackBudgetCost and
// not added here is invisible to every one of them rather than to some of
// them, and a name cannot mean one counter in the listing and another in a
// lookup. A pointer to member, not an offset, so the compiler checks that
// every row names a field of the right type.
struct CounterRef {
    TrackBudgetStage stage;
    std::string_view name;
    std::uint64_t TrackBudgetCost::*member;
};

constexpr CounterRef kCounters[] = {
    {TrackBudgetStage::parse, "parse.region_bytes",
     &TrackBudgetCost::parse_region_bytes},
    {TrackBudgetStage::parse, "parse.meshes", &TrackBudgetCost::parse_meshes},
    {TrackBudgetStage::parse, "parse.valid_meshes",
     &TrackBudgetCost::parse_valid_meshes},
    {TrackBudgetStage::parse, "parse.vertices",
     &TrackBudgetCost::parse_vertices},
    {TrackBudgetStage::parse, "parse.triangles",
     &TrackBudgetCost::parse_triangles},
    {TrackBudgetStage::parse, "parse.materials",
     &TrackBudgetCost::parse_materials},
    {TrackBudgetStage::parse, "parse.decoded_mesh_bytes",
     &TrackBudgetCost::parse_decoded_mesh_bytes},
    {TrackBudgetStage::parse, "parse.sections", &TrackBudgetCost::parse_sections},
    {TrackBudgetStage::parse, "parse.valid_sections",
     &TrackBudgetCost::parse_valid_sections},
    {TrackBudgetStage::parse, "parse.objects", &TrackBudgetCost::parse_objects},
    {TrackBudgetStage::parse, "parse.instances",
     &TrackBudgetCost::parse_instances},
    {TrackBudgetStage::parse, "parse.bound_nodes",
     &TrackBudgetCost::parse_bound_nodes},
    {TrackBudgetStage::parse, "parse.object_record_bytes",
     &TrackBudgetCost::parse_object_record_bytes},

    {TrackBudgetStage::prepare, "prepare.meshes",
     &TrackBudgetCost::prepare_meshes},
    {TrackBudgetStage::prepare, "prepare.draws",
     &TrackBudgetCost::prepare_draws},
    {TrackBudgetStage::prepare, "prepare.drawable_draws",
     &TrackBudgetCost::prepare_drawable_draws},
    {TrackBudgetStage::prepare, "prepare.textures",
     &TrackBudgetCost::prepare_textures},
    {TrackBudgetStage::prepare, "prepare.vertex_buffer_bytes",
     &TrackBudgetCost::prepare_vertex_buffer_bytes},
    {TrackBudgetStage::prepare, "prepare.index_buffer_bytes",
     &TrackBudgetCost::prepare_index_buffer_bytes},
    {TrackBudgetStage::prepare, "prepare.texture_bytes",
     &TrackBudgetCost::prepare_texture_bytes},
    {TrackBudgetStage::prepare, "prepare.instance_bytes",
     &TrackBudgetCost::prepare_instance_bytes},
    {TrackBudgetStage::prepare, "prepare.resident_bytes",
     &TrackBudgetCost::prepare_resident_bytes},
    {TrackBudgetStage::prepare, "prepare.placed_instances",
     &TrackBudgetCost::prepare_placed_instances},
    {TrackBudgetStage::prepare, "prepare.unplaced_instances",
     &TrackBudgetCost::prepare_unplaced_instances},
    {TrackBudgetStage::prepare, "prepare.resident_entries",
     &TrackBudgetCost::prepare_resident_entries},
    {TrackBudgetStage::prepare, "prepare.resident_triangles",
     &TrackBudgetCost::prepare_resident_triangles},

    {TrackBudgetStage::frame, "frame.visible_instances",
     &TrackBudgetCost::frame_visible_instances},
    {TrackBudgetStage::frame, "frame.visible_placed",
     &TrackBudgetCost::frame_visible_placed},
    {TrackBudgetStage::frame, "frame.visible_unplaced",
     &TrackBudgetCost::frame_visible_unplaced},
    {TrackBudgetStage::frame, "frame.draw_calls",
     &TrackBudgetCost::frame_draw_calls},
    {TrackBudgetStage::frame, "frame.triangles",
     &TrackBudgetCost::frame_triangles},
    {TrackBudgetStage::frame, "frame.groups", &TrackBudgetCost::frame_groups},
    {TrackBudgetStage::frame, "frame.meshes", &TrackBudgetCost::frame_meshes},
    {TrackBudgetStage::frame, "frame.texture_binds",
     &TrackBudgetCost::frame_texture_binds},
    {TrackBudgetStage::frame, "frame.entry_bytes",
     &TrackBudgetCost::frame_entry_bytes},

    {TrackBudgetStage::render, "render.entries",
     &TrackBudgetCost::render_entries},
    {TrackBudgetStage::render, "render.triangles_submitted",
     &TrackBudgetCost::render_triangles_submitted},
    {TrackBudgetStage::render, "render.triangles_rasterized",
     &TrackBudgetCost::render_triangles_rasterized},
    {TrackBudgetStage::render, "render.pixel_tests",
     &TrackBudgetCost::render_pixel_tests},
    {TrackBudgetStage::render, "render.fragments",
     &TrackBudgetCost::render_fragments},
    {TrackBudgetStage::render, "render.fragments_written",
     &TrackBudgetCost::render_fragments_written},
    {TrackBudgetStage::render, "render.pixels_drawn",
     &TrackBudgetCost::render_pixels_drawn},
    {TrackBudgetStage::render, "render.texture_binds",
     &TrackBudgetCost::render_texture_binds},
    {TrackBudgetStage::render, "render.frame_bytes",
     &TrackBudgetCost::render_frame_bytes},
};

bool stage_measured(
    const TrackBudgetCost& cost, TrackBudgetStage stage) noexcept {
    switch (stage) {
        case TrackBudgetStage::parse:
            return cost.measured_parse;
        case TrackBudgetStage::prepare:
            return cost.measured_prepare;
        case TrackBudgetStage::frame:
            return cost.measured_frame;
        case TrackBudgetStage::render:
            return cost.measured_render;
    }
    return false;
}

} // namespace

const char* track_budget_stage_name(TrackBudgetStage stage) noexcept {
    switch (stage) {
        case TrackBudgetStage::parse:
            return "parse";
        case TrackBudgetStage::prepare:
            return "prepare";
        case TrackBudgetStage::frame:
            return "frame";
        case TrackBudgetStage::render:
            return "render";
    }
    return "unknown";
}

void track_budget_entries(
    const TrackBudgetCost& cost, std::vector<TrackBudgetEntry>& entries) {
    entries.clear();
    entries.reserve(sizeof(kCounters) / sizeof(kCounters[0]));
    for (const CounterRef& ref : kCounters) {
        TrackBudgetEntry entry;
        entry.stage = ref.stage;
        entry.name = ref.name;
        entry.value = cost.*(ref.member);
        entries.push_back(entry);
    }
}

bool track_budget_value(
    const TrackBudgetCost& cost,
    std::string_view name,
    std::uint64_t& value,
    TrackBudgetStage& stage) noexcept {
    for (const CounterRef& ref : kCounters) {
        if (ref.name == name) {
            value = cost.*(ref.member);
            stage = ref.stage;
            return true;
        }
    }
    return false;
}

void track_budget_parse_cost(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    TrackBudgetCost& cost) noexcept {
    const TrackRegionCost& g = geometry.cost;
    const TrackObjectCost& o = objects.cost;
    cost.parse_region_bytes = g.region_bytes + o.region_bytes;
    cost.parse_meshes = g.meshes;
    cost.parse_valid_meshes = g.valid_meshes;
    cost.parse_vertices = g.vertices;
    cost.parse_triangles = g.triangles;
    cost.parse_materials = g.materials;
    cost.parse_decoded_mesh_bytes =
        g.decoded_vertex_bytes + g.decoded_index_bytes;
    cost.parse_sections = o.sections;
    cost.parse_valid_sections = o.valid_sections;
    cost.parse_objects = o.objects;
    cost.parse_instances = o.instances;
    cost.parse_bound_nodes = o.bound_nodes;
    cost.parse_object_record_bytes = o.source_object_bytes +
                                     o.source_instance_bytes +
                                     o.source_bound_node_bytes;
    cost.measured_parse = true;
}

void track_budget_prepare_cost(
    const PreparedTrackScene& scene, TrackBudgetCost& cost) noexcept {
    const TrackDrawCost& region = scene.region.cost;
    const TrackScenePlacementCost& placement = scene.cost;
    cost.prepare_meshes = region.meshes;
    cost.prepare_draws = region.draws;
    cost.prepare_drawable_draws = region.drawable_draws;
    cost.prepare_textures = region.textures;
    cost.prepare_vertex_buffer_bytes = region.vertex_buffer_bytes;
    cost.prepare_index_buffer_bytes = region.index_buffer_bytes;
    cost.prepare_texture_bytes = region.texture_bytes;
    cost.prepare_instance_bytes =
        static_cast<std::uint64_t>(scene.instances.size()) *
        static_cast<std::uint64_t>(sizeof(PreparedTrackInstance));
    cost.prepare_resident_bytes =
        region.vertex_buffer_bytes + region.index_buffer_bytes +
        region.texture_bytes + cost.prepare_instance_bytes;
    cost.prepare_placed_instances = placement.placed_instances;
    cost.prepare_unplaced_instances = placement.unplaced_empty_slot +
                                      placement.unplaced_skipped_hash +
                                      placement.unplaced_unresolved;
    cost.prepare_resident_entries = placement.resident_entries;
    cost.prepare_resident_triangles = placement.resident_triangles;
    cost.measured_prepare = true;
}

void track_budget_frame_cost(
    const TrackDrawList& list, TrackBudgetCost& cost) noexcept {
    const TrackDrawListCost& c = list.cost;
    cost.frame_visible_instances = c.visible;
    cost.frame_visible_placed = c.visible_placed;
    cost.frame_visible_unplaced = c.visible_unplaced;
    cost.frame_draw_calls = c.entries;
    cost.frame_triangles = c.triangles;
    cost.frame_groups = c.groups;
    cost.frame_meshes = c.meshes;
    cost.frame_texture_binds = c.texture_binds;
    cost.frame_entry_bytes =
        static_cast<std::uint64_t>(list.entries.size()) *
        static_cast<std::uint64_t>(sizeof(TrackDrawListEntry));
    cost.measured_frame = true;
}

void track_budget_render_cost(
    const RenderStats& stats,
    const TrackRenderStats& track_stats,
    const Frame& frame,
    TrackBudgetCost& cost) noexcept {
    cost.render_entries = track_stats.entries;
    cost.render_triangles_submitted = stats.triangles_submitted;
    cost.render_triangles_rasterized = stats.triangles_rasterized;
    cost.render_pixel_tests = stats.pixel_tests;
    cost.render_fragments = stats.fragments;
    cost.render_fragments_written = stats.fragments_written;
    cost.render_pixels_drawn = stats.pixels_drawn;
    cost.render_texture_binds = track_stats.texture_binds;
    // From the buffers themselves. width * height * a constant would be the
    // same number today and would have to be kept in step with Frame by
    // hand; these four sizes move with it.
    cost.render_frame_bytes =
        static_cast<std::uint64_t>(frame.rgba().size()) +
        static_cast<std::uint64_t>(frame.depth().size()) *
            static_cast<std::uint64_t>(sizeof(double)) +
        static_cast<std::uint64_t>(frame.ids().size()) *
            static_cast<std::uint64_t>(sizeof(std::uint32_t)) +
        static_cast<std::uint64_t>(frame.blended().size());
    cost.measured_render = true;
}

bool check_track_budget(
    const TrackBudgetCost& cost,
    const std::vector<TrackBudgetLimit>& limits,
    std::vector<TrackBudgetExceedance>& exceedances,
    std::string& error) {
    exceedances.clear();
    // Every refusal is checked over the WHOLE limit set before a single
    // exceedance is reported, so a set holding both a typo and a real
    // regression reports the typo rather than a partial verdict that looks
    // like a budget result.
    for (std::size_t i = 0; i < limits.size(); ++i) {
        const TrackBudgetLimit& limit = limits[i];
        std::uint64_t value = 0;
        TrackBudgetStage stage = TrackBudgetStage::parse;
        if (!track_budget_value(cost, limit.name, value, stage)) {
            error = "no track budget counter is named '" + limit.name + "'";
            return false;
        }
        if (!stage_measured(cost, stage)) {
            error = "the limit on '" + limit.name +
                    "' names the " + track_budget_stage_name(stage) +
                    " stage, which this cost did not measure, so its value is"
                    " zero because nothing looked and the limit would pass"
                    " whatever the stage costs";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (limits[j].name == limit.name) {
                error = "'" + limit.name +
                        "' carries more than one limit, so only the last would"
                        " decide and the rest would read as enforced";
                return false;
            }
        }
    }
    for (const TrackBudgetLimit& limit : limits) {
        std::uint64_t value = 0;
        TrackBudgetStage stage = TrackBudgetStage::parse;
        // Already proven to resolve by the loop above.
        track_budget_value(cost, limit.name, value, stage);
        if (value > limit.ceiling) {
            TrackBudgetExceedance over;
            over.name = limit.name;
            over.stage = stage;
            over.value = value;
            over.ceiling = limit.ceiling;
            exceedances.push_back(over);
        }
    }
    return true;
}

} // namespace nfsu
