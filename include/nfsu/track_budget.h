#pragma once

#include "nfsu/render.h"
#include "nfsu/track.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_objects.h"
#include "nfsu/track_render.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nfsu {

// The desktop track slice's cost, separated by the stage that pays it, and
// a limit set a gate can check it against. See docs/TRACK_BUDGET.md.
//
// This header decodes nothing, renders nothing and READS NO CLOCK. It takes
// the reports the four stages already produce and rearranges their counters
// into one named, stage-tagged table, so that a budget is written against
// names rather than against a struct layout.
//
// WHY THERE IS NO CLOCK IN THIS HEADER
//
// A budget wants two different things and they have opposite stability.
//
//   * COUNTS - draw calls, triangles, texture binds, bytes retained - are
//     functions of the input. Two runs over the same region give the same
//     numbers on any machine, in any build configuration, under any load.
//     They can be pinned exactly, and a test that pins them fails only when
//     the code's behaviour changed.
//   * WALL-CLOCK TIME on the machine this project is developed on is not.
//     Several sessions build concurrently, so a stage timed while another
//     session is linking is slower for a reason that has nothing to do with
//     this code. A gate that trips on that gets deleted, and then it covers
//     nothing at all.
//
// So the two are split at the API boundary rather than mixed and then
// qualified. Every counter in TrackBudgetCost is exact and reproducible,
// and a budget limit can only name one of those: check_track_budget reads
// nothing else, so a limit on a time cannot be expressed here at all. No
// automated limit in this repository is a time.
//
// TWO FUNCTIONS HERE TAKE DOUBLES AND NEITHER IS AN EXCEPTION TO THAT.
// summarize_track_samples is pure arithmetic over numbers the caller hands
// it - it reads no clock and does not know its caller's samples are
// milliseconds - and it lives here rather than in the command because a
// statistic computed in a command cannot be tested: a summary of wall-clock
// times has no value a test can assert against, so every fault in it
// survives. track_budget_costs_equal is here for the mirror of that reason;
// its own comment says which. docs/TRACK_BUDGET.md records the mutation
// sweep that established both, and the measurements behind the split.
//
// WHAT "RETAINED BYTES" MEANS HERE, AND WHAT IT DOES NOT
//
// Every byte figure below is a STATED FORMULA over containers whose element
// count and element size are both known - never a heap or working-set
// total. `prepare.vertex_buffer_bytes` is the vertex bytes a backend
// uploads, because TrackDrawCost defines it that way; `frame.entry_bytes`
// is `entries * sizeof(TrackDrawListEntry)`. The comment on each field says
// which containers it covers.
//
// That is deliberate and it is a real limitation, stated rather than
// papered over: these figures do NOT include the report vectors' capacity
// slack, the std::string mesh names, the issue lists, the allocator's own
// per-block overhead, or anything the process allocated for another reason.
// They are the dominant arrays a backend has to size buffers against, which
// is what a budget is for. A process-wide memory number would cover the
// rest and would stop being reproducible in exchange, which is the same
// trade the clock paragraph above refuses.

// The stage a counter belongs to. The four are the ones issue #43 names,
// with the culler and the draw-list build together as `frame`: they are
// what is paid per frame, and separating them would suggest a backend can
// pay one without the other.
enum class TrackBudgetStage {
    // load_track_region and load_track_objects: bytes read off disk and
    // decoded into reports.
    parse,
    // build_track_scene: the region prepared once and placed once.
    prepare,
    // cull_track_region and build_track_draw_list: what one frame selects.
    frame,
    // render_track_draw_list: what one frame draws.
    render,
};

// "parse", "prepare", "frame" or "render". Stable: tests compare against
// it and the command prints it.
const char* track_budget_stage_name(TrackBudgetStage stage) noexcept;

// Every counter, by stage. Each field is a function of the stage's own
// report, so two runs over the same inputs give the same numbers.
//
// A cost is filled one stage at a time by the four functions below, and
// each of them writes ONLY its own stage's fields. A caller that runs three
// stages and not the fourth therefore gets three filled blocks and one of
// zeros, which is what `measured` records so the zeros are not read as
// measurements.
struct TrackBudgetCost {
    // Which stages were filled. A counter of an unmeasured stage is zero
    // because nothing measured it, not because the stage costs nothing, and
    // check_track_budget refuses a limit that names one.
    bool measured_parse = false;
    bool measured_prepare = false;
    bool measured_frame = false;
    bool measured_render = false;

    // ---- parse ----------------------------------------------------------
    // Region bytes the two loaders read, geometry plus objects. This is
    // what a streaming backend moves off disk for one region.
    std::uint64_t parse_region_bytes = 0;
    std::uint64_t parse_meshes = 0;
    std::uint64_t parse_valid_meshes = 0;
    std::uint64_t parse_vertices = 0;
    std::uint64_t parse_triangles = 0;
    std::uint64_t parse_materials = 0;
    // TrackRegionCost::decoded_vertex_bytes + decoded_index_bytes: what the
    // decoded meshes hold, which is not what the file stored.
    std::uint64_t parse_decoded_mesh_bytes = 0;
    std::uint64_t parse_sections = 0;
    std::uint64_t parse_valid_sections = 0;
    std::uint64_t parse_objects = 0;
    std::uint64_t parse_instances = 0;
    std::uint64_t parse_bound_nodes = 0;
    // TrackObjectCost's three source_*_bytes: the object, instance and
    // bounding-node records as stored.
    std::uint64_t parse_object_record_bytes = 0;

    // ---- prepare --------------------------------------------------------
    std::uint64_t prepare_meshes = 0;
    std::uint64_t prepare_draws = 0;
    std::uint64_t prepare_drawable_draws = 0;
    std::uint64_t prepare_textures = 0;
    std::uint64_t prepare_vertex_buffer_bytes = 0;
    std::uint64_t prepare_index_buffer_bytes = 0;
    std::uint64_t prepare_texture_bytes = 0;
    // instances * sizeof(PreparedTrackInstance), the placement table.
    std::uint64_t prepare_instance_bytes = 0;
    // The three buffer figures plus the instance table: everything a
    // backend holds for the region between frames. This is the number the
    // "memory proportional to the region, not to the frame" cost of
    // docs/TRACK_DRAW_LIST.md is paid in.
    std::uint64_t prepare_resident_bytes = 0;
    std::uint64_t prepare_placed_instances = 0;
    std::uint64_t prepare_unplaced_instances = 0;
    // The ceiling a backend sizes a per-frame buffer against: what a list
    // would hold if every placed instance were visible.
    std::uint64_t prepare_resident_entries = 0;
    std::uint64_t prepare_resident_triangles = 0;

    // ---- frame ----------------------------------------------------------
    std::uint64_t frame_visible_instances = 0;
    std::uint64_t frame_visible_placed = 0;
    std::uint64_t frame_visible_unplaced = 0;
    // One draw-list entry is one draw call: one copy of one material group.
    // This is the draw-call cost issue #35 asks for.
    std::uint64_t frame_draw_calls = 0;
    std::uint64_t frame_triangles = 0;
    std::uint64_t frame_groups = 0;
    std::uint64_t frame_meshes = 0;
    std::uint64_t frame_texture_binds = 0;
    // entries * sizeof(TrackDrawListEntry): the per-frame allocation, as
    // against prepare_resident_bytes, which is paid once.
    std::uint64_t frame_entry_bytes = 0;

    // ---- render ---------------------------------------------------------
    std::uint64_t render_entries = 0;
    std::uint64_t render_triangles_submitted = 0;
    std::uint64_t render_triangles_rasterized = 0;
    std::uint64_t render_pixel_tests = 0;
    std::uint64_t render_fragments = 0;
    std::uint64_t render_fragments_written = 0;
    std::uint64_t render_pixels_drawn = 0;
    std::uint64_t render_texture_binds = 0;
    // The Frame's own four buffers, measured from their sizes rather than
    // from width * height * a constant this header would have to keep in
    // step with nfsu::Frame.
    std::uint64_t render_frame_bytes = 0;
};

// One measured counter, as the table above exposes it.
struct TrackBudgetEntry {
    TrackBudgetStage stage = TrackBudgetStage::parse;
    // A stable, unique, dotted name: the stage, a '.', and the counter.
    // Tests and limit sets compare against these strings.
    std::string_view name;
    std::uint64_t value = 0;
};

// One ceiling to hold a counter to.
struct TrackBudgetLimit {
    std::string name;
    std::uint64_t ceiling = 0;
};

// One counter that is over its ceiling.
struct TrackBudgetExceedance {
    std::string name;
    TrackBudgetStage stage = TrackBudgetStage::parse;
    std::uint64_t value = 0;
    std::uint64_t ceiling = 0;
};

// Every counter of `cost`, in stage order and, inside a stage, in the order
// the struct declares them. Total: it never fails and the table is the same
// length whatever the cost holds, so a caller can print a whole budget
// without knowing which stages were measured. Counters of an unmeasured
// stage are present and zero; `measured` on the cost is what says so.
void track_budget_entries(
    const TrackBudgetCost& cost,
    std::vector<TrackBudgetEntry>& entries);

// The value of one counter by name, and its stage. False, with both
// outputs untouched, when no counter has that name.
bool track_budget_value(
    const TrackBudgetCost& cost,
    std::string_view name,
    std::uint64_t& value,
    TrackBudgetStage& stage) noexcept;

// Fills the parse block from the two loader reports and touches no other
// block. Total.
void track_budget_parse_cost(
    const TrackRegionReport& geometry,
    const TrackObjectReport& objects,
    TrackBudgetCost& cost) noexcept;

// Fills the prepare block from a prepared scene and touches no other
// block. Total. The scene is read as given; validate_prepared_track_scene
// belongs before it, exactly as it does before the draw-list build.
void track_budget_prepare_cost(
    const PreparedTrackScene& scene,
    TrackBudgetCost& cost) noexcept;

// Fills the frame block from one draw list and touches no other block.
// Total.
void track_budget_frame_cost(
    const TrackDrawList& list,
    TrackBudgetCost& cost) noexcept;

// Fills the render block from one rendered frame and touches no other
// block. Total. `frame` is read for its buffer sizes only.
void track_budget_render_cost(
    const RenderStats& stats,
    const TrackRenderStats& track_stats,
    const Frame& frame,
    TrackBudgetCost& cost) noexcept;

// Whether two costs hold the same numbers, and which counter differs first.
//
// This is what makes a repeated measurement's determinism a CHECKED property
// rather than an assertion: a caller that runs the pipeline twice compares
// the two costs with this and reports a disagreement instead of quietly
// averaging over it. `difference` is set, and false returned, on the first
// counter whose values differ or whose stage one cost measured and the other
// did not; it names the counter and prints both values.
//
// It lives here rather than in the command for a reason the mutation sweep
// in docs/TRACK_BUDGET.md records. A comparison that only ever runs over
// costs that ARE equal cannot be tested from outside: correct code never
// takes its failing branch. As a library function it is driven directly with
// two costs that differ, so the branch is exercised.
bool track_budget_costs_equal(
    const TrackBudgetCost& first,
    const TrackBudgetCost& second,
    std::string& difference);

// The spread of a set of samples. Pure arithmetic over numbers the caller
// supplies - it reads no clock, and it does not know or care that its
// caller's samples are milliseconds.
//
// Reporting a spread rather than one number is the point. A single figure
// taken on a machine several sessions build on reads like a fact and is not
// one, so nfsu_probe track-budget reports all five of these and this project
// pins none of them.
struct TrackSampleSummary {
    std::size_t samples = 0;
    double min = 0.0;
    double median = 0.0;
    double mean = 0.0;
    // The SAMPLE standard deviation, dividing by n-1: these repetitions are
    // a sample of the machine's behaviour and not its population. Zero for a
    // single sample, where the population form would divide by zero.
    double stddev = 0.0;
    double max = 0.0;
};

// Summarizes `samples`, which is taken by value and sorted in place. An
// empty set gives a summary of zeros with `samples` zero, which is the
// honest answer rather than a division by zero. The median of an even count
// is the mean of the two middle values.
TrackSampleSummary summarize_track_samples(std::vector<double> samples);

// Checks `cost` against `limits` and lists every counter that is over.
//
// THE RETURN VALUE SAYS WHETHER THE CHECK RAN, NOT WHETHER IT PASSED, and
// the two are kept apart deliberately: a limit set with a typo in it would
// otherwise read as a budget that passed. It returns false, with `error`
// set and `exceedances` left empty, when
//
//   * a limit names a counter that does not exist, which is a typo in the
//     limit set rather than a regression in the code; or
//   * a limit names a counter of a stage `cost` did not measure, whose
//     value is zero because nothing looked rather than because the stage is
//     free, and which would therefore pass every ceiling; or
//   * two limits name the same counter, where the later one would silently
//     decide and the earlier one would read as enforced.
//
// It returns true when the check ran. `exceedances` is then empty exactly
// when every counter is at or below its ceiling. A counter EQUAL to its
// ceiling is within budget: a ceiling is the largest acceptable value, so a
// budget written from a measurement holds at that measurement.
//
// Limits are checked in the order given and exceedances are reported in
// that order, so a limit set's own ordering is what a report reads back.
bool check_track_budget(
    const TrackBudgetCost& cost,
    const std::vector<TrackBudgetLimit>& limits,
    std::vector<TrackBudgetExceedance>& exceedances,
    std::string& error);

} // namespace nfsu
