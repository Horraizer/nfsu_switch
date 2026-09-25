#include "nfsu/track_budget.h"

#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_eq_u64(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

std::uint64_t value_of(const nfsu::TrackBudgetCost& cost, const char* name) {
    std::uint64_t value = 0;
    nfsu::TrackBudgetStage stage = nfsu::TrackBudgetStage::render;
    if (!nfsu::track_budget_value(cost, name, value, stage)) {
        check(false, std::string("no counter named ") + name);
        return 0;
    }
    return value;
}

// ---------------------------------------------------------------------------
// The counter table itself.
//
// Every one of the three public entry points reads one table, so these
// checks are what makes "a name means the same thing in a listing and in a
// lookup" a tested property rather than a convention. A row whose name does
// not match its stage, a duplicate name, or a listing and a lookup that
// disagree are each caught here.
// ---------------------------------------------------------------------------

void test_counter_table() {
    // Distinct values in every field, so that a row reading the wrong member
    // gives the wrong number rather than coincidentally the right one. The
    // values are the counter's own position in this list, plus one, so no
    // two are equal and none is zero.
    nfsu::TrackBudgetCost cost;
    cost.measured_parse = true;
    cost.measured_prepare = true;
    cost.measured_frame = true;
    cost.measured_render = true;

    std::vector<nfsu::TrackBudgetEntry> entries;
    nfsu::track_budget_entries(cost, entries);
    check(!entries.empty(), "the counter table is not empty");

    // THE WHOLE TABLE, EXACTLY, IN ORDER. The header promises the entries
    // come in stage order and, inside a stage, in the order the struct
    // declares them, and a caller printing a budget straight through relies
    // on it. The checks further down pin the table's SHAPE - unique names,
    // a stage prefix, contiguous blocks - and every one of them is satisfied
    // by two counters of one stage swapped with each other, which is what an
    // independent mutation pass used. Listing the names is the only thing
    // that is not.
    //
    // It is deliberately brittle. A counter added, removed or moved fails
    // here and has to be acknowledged, which for a table that is a
    // documented contract is the behaviour worth having.
    static const char* const kExpected[] = {
        "parse.region_bytes",
        "parse.meshes",
        "parse.valid_meshes",
        "parse.vertices",
        "parse.triangles",
        "parse.materials",
        "parse.decoded_mesh_bytes",
        "parse.sections",
        "parse.valid_sections",
        "parse.objects",
        "parse.instances",
        "parse.bound_nodes",
        "parse.object_record_bytes",
        "prepare.meshes",
        "prepare.draws",
        "prepare.drawable_draws",
        "prepare.textures",
        "prepare.vertex_buffer_bytes",
        "prepare.index_buffer_bytes",
        "prepare.texture_bytes",
        "prepare.instance_bytes",
        "prepare.resident_bytes",
        "prepare.placed_instances",
        "prepare.unplaced_instances",
        "prepare.resident_entries",
        "prepare.resident_triangles",
        "frame.visible_instances",
        "frame.visible_placed",
        "frame.visible_unplaced",
        "frame.draw_calls",
        "frame.triangles",
        "frame.groups",
        "frame.meshes",
        "frame.texture_binds",
        "frame.entry_bytes",
        "render.entries",
        "render.triangles_submitted",
        "render.triangles_rasterized",
        "render.pixel_tests",
        "render.fragments",
        "render.fragments_written",
        "render.pixels_drawn",
        "render.texture_binds",
        "render.frame_bytes",
    };
    const std::size_t expected_count =
        sizeof(kExpected) / sizeof(kExpected[0]);
    check(entries.size() == expected_count,
          "the table holds " + std::to_string(expected_count) +
              " counters, and it holds " + std::to_string(entries.size()));
    for (std::size_t i = 0; i < entries.size() && i < expected_count; ++i) {
        check(entries[i].name == kExpected[i],
              "counter " + std::to_string(i) + " is " +
                  std::string(kExpected[i]) + ", and it is " +
                  std::string(entries[i].name));
    }

    // Names are unique.
    std::set<std::string> names;
    for (const nfsu::TrackBudgetEntry& entry : entries) {
        const std::string name(entry.name);
        check(names.insert(name).second, "counter name " + name + " is unique");
    }

    // Every name is its own stage's name, a '.', and a non-empty tail. A row
    // filed under the wrong stage is then a failure here rather than a
    // silently mis-grouped report, and it is also what makes the stage of a
    // limit readable from the limit's own name.
    for (const nfsu::TrackBudgetEntry& entry : entries) {
        const std::string name(entry.name);
        const std::string prefix =
            std::string(nfsu::track_budget_stage_name(entry.stage)) + ".";
        check(name.rfind(prefix, 0) == 0,
              "counter " + name + " is named for its stage " + prefix);
        check(name.size() > prefix.size(),
              "counter " + name + " has a name after its stage prefix");
    }

    // The stages appear in order and each is contiguous, which is what lets
    // a report print the table straight through and still read as four
    // blocks.
    std::vector<nfsu::TrackBudgetStage> seen;
    for (const nfsu::TrackBudgetEntry& entry : entries) {
        if (seen.empty() || seen.back() != entry.stage) {
            for (const nfsu::TrackBudgetStage earlier : seen) {
                check(earlier != entry.stage,
                      std::string("stage ") +
                          nfsu::track_budget_stage_name(entry.stage) +
                          " is contiguous in the table");
            }
            seen.push_back(entry.stage);
        }
    }
    check(seen.size() == 4, "the table covers four stages, in blocks");
    check(seen.size() == 4 && seen[0] == nfsu::TrackBudgetStage::parse &&
              seen[1] == nfsu::TrackBudgetStage::prepare &&
              seen[2] == nfsu::TrackBudgetStage::frame &&
              seen[3] == nfsu::TrackBudgetStage::render,
          "the four stages are in pipeline order");

    // TWO ROWS NAMING ONE MEMBER is caught by the four gatherer tests below
    // rather than here, and it is worth saying where so that the gap is not
    // looked for twice. Each of those tests gives every source field of one
    // stage a different value and then reads every counter of that stage
    // back BY NAME, so two names aliased onto one member report one value
    // where two different ones are required. Aliasing ACROSS stages is
    // caught by the same tests' "leaves the other blocks alone" checks,
    // which read the aliased member through a sentinel. Between them every
    // row of the table is covered; what this function pins is the table's
    // shape, which those tests do not look at.

    // Names resolve; an unknown one does not.
    std::uint64_t value = 0;
    nfsu::TrackBudgetStage stage = nfsu::TrackBudgetStage::render;
    check(nfsu::track_budget_value(cost, "frame.draw_calls", value, stage),
          "frame.draw_calls resolves");
    check(stage == nfsu::TrackBudgetStage::frame,
          "frame.draw_calls is a frame counter");
    check(!nfsu::track_budget_value(cost, "frame.draw_call", value, stage),
          "a near-miss name does not resolve");
    check(!nfsu::track_budget_value(cost, "", value, stage),
          "the empty name does not resolve");
    check(!nfsu::track_budget_value(cost, "draw_calls", value, stage),
          "a counter's tail alone does not resolve");

    // A failed lookup leaves both outputs untouched, which the header
    // promises and which a caller reusing one variable relies on.
    std::uint64_t keep_value = 12345;
    nfsu::TrackBudgetStage keep_stage = nfsu::TrackBudgetStage::prepare;
    check(!nfsu::track_budget_value(cost, "nope.nothing", keep_value,
                                    keep_stage),
          "an unknown name does not resolve");
    check_eq_u64(keep_value, 12345, "a failed lookup leaves the value alone");
    check(keep_stage == nfsu::TrackBudgetStage::prepare,
          "a failed lookup leaves the stage alone");

    // The listing is the same length whatever was measured, so a caller can
    // print a whole budget without knowing which stages ran.
    nfsu::TrackBudgetCost nothing_measured;
    std::vector<nfsu::TrackBudgetEntry> unmeasured;
    nfsu::track_budget_entries(nothing_measured, unmeasured);
    check(unmeasured.size() == entries.size(),
          "the table has the same rows whether or not a stage was measured");
    for (const nfsu::TrackBudgetEntry& entry : unmeasured) {
        check_eq_u64(entry.value, 0,
                     std::string("unmeasured ") + std::string(entry.name) +
                         " is zero");
    }

    // track_budget_entries clears what it is handed rather than appending,
    // so a caller reusing one vector across two costs does not accumulate.
    std::vector<nfsu::TrackBudgetEntry> reused;
    nfsu::track_budget_entries(cost, reused);
    const std::size_t once = reused.size();
    nfsu::track_budget_entries(cost, reused);
    check(reused.size() == once, "a reused entry vector is cleared, not grown");

    check(std::string(nfsu::track_budget_stage_name(
              nfsu::TrackBudgetStage::parse)) == "parse",
          "stage name parse");
    check(std::string(nfsu::track_budget_stage_name(
              nfsu::TrackBudgetStage::prepare)) == "prepare",
          "stage name prepare");
    check(std::string(nfsu::track_budget_stage_name(
              nfsu::TrackBudgetStage::frame)) == "frame",
          "stage name frame");
    check(std::string(nfsu::track_budget_stage_name(
              nfsu::TrackBudgetStage::render)) == "render",
          "stage name render");
}

// ---------------------------------------------------------------------------
// The four gatherers.
//
// Each reads one report and fills one block. Every source field is given a
// DIFFERENT value, so a gatherer that reads `triangles` where `vertices`
// belongs produces the wrong number rather than the right one by accident -
// which is the whole failure mode a field-copy layer has. Each test also
// pre-fills the other three blocks with sentinels and requires them
// untouched, because "fills only its own block" is a promise the header
// makes and a caller running three stages relies on.
// ---------------------------------------------------------------------------

// Sentinels in every block but `keep`, so that a gatherer writing outside
// its own block is caught.
nfsu::TrackBudgetCost sentinel_cost() {
    nfsu::TrackBudgetCost cost;
    // Written through the struct rather than the table, because the table
    // exposes values and not references; these are the fields a gatherer
    // must not touch.
    cost.parse_region_bytes = 900001;
    cost.parse_meshes = 900002;
    cost.prepare_meshes = 900101;
    cost.prepare_texture_bytes = 900102;
    cost.frame_draw_calls = 900201;
    cost.frame_triangles = 900202;
    cost.render_pixels_drawn = 900301;
    cost.render_frame_bytes = 900302;
    return cost;
}

void test_parse_gatherer() {
    nfsu::TrackRegionReport geometry;
    geometry.cost.region_bytes = 1001;
    geometry.cost.meshes = 1002;
    geometry.cost.valid_meshes = 1003;
    geometry.cost.vertices = 1004;
    geometry.cost.triangles = 1005;
    geometry.cost.materials = 1006;
    geometry.cost.decoded_vertex_bytes = 1007;
    geometry.cost.decoded_index_bytes = 1008;
    // Fields the parse block must NOT read. Distinct, and large enough that
    // picking one up instead would be unmistakable.
    geometry.cost.source_vertex_bytes = 700001;
    geometry.cost.source_index_bytes = 700002;
    geometry.cost.chunks = 700003;
    geometry.cost.empty_meshes = 700004;

    nfsu::TrackObjectReport objects;
    objects.cost.region_bytes = 2001;
    objects.cost.sections = 2002;
    objects.cost.valid_sections = 2003;
    objects.cost.objects = 2004;
    objects.cost.instances = 2005;
    objects.cost.bound_nodes = 2006;
    objects.cost.source_object_bytes = 2007;
    objects.cost.source_instance_bytes = 2008;
    objects.cost.source_bound_node_bytes = 2009;
    objects.cost.chunks = 700005;
    objects.cost.mesh_slots_used = 700006;

    nfsu::TrackBudgetCost cost = sentinel_cost();
    nfsu::track_budget_parse_cost(geometry, objects, cost);

    check(cost.measured_parse, "the parse gatherer marks its stage measured");
    // The two region sizes are SUMMED, not taken from one report: a loader
    // reads both.
    check_eq_u64(value_of(cost, "parse.region_bytes"), 1001 + 2001,
                 "parse.region_bytes sums the two regions");
    check_eq_u64(value_of(cost, "parse.meshes"), 1002, "parse.meshes");
    check_eq_u64(value_of(cost, "parse.valid_meshes"), 1003,
                 "parse.valid_meshes");
    check_eq_u64(value_of(cost, "parse.vertices"), 1004, "parse.vertices");
    check_eq_u64(value_of(cost, "parse.triangles"), 1005, "parse.triangles");
    check_eq_u64(value_of(cost, "parse.materials"), 1006, "parse.materials");
    // DECODED, not source: the two differ in a retail region and the header
    // says which this is.
    check_eq_u64(value_of(cost, "parse.decoded_mesh_bytes"), 1007 + 1008,
                 "parse.decoded_mesh_bytes sums the decoded vertex and index"
                 " bytes");
    check_eq_u64(value_of(cost, "parse.sections"), 2002, "parse.sections");
    check_eq_u64(value_of(cost, "parse.valid_sections"), 2003,
                 "parse.valid_sections");
    check_eq_u64(value_of(cost, "parse.objects"), 2004, "parse.objects");
    check_eq_u64(value_of(cost, "parse.instances"), 2005, "parse.instances");
    check_eq_u64(value_of(cost, "parse.bound_nodes"), 2006,
                 "parse.bound_nodes");
    check_eq_u64(value_of(cost, "parse.object_record_bytes"),
                 2007 + 2008 + 2009,
                 "parse.object_record_bytes sums the three record totals");

    // The other three blocks are untouched.
    check_eq_u64(cost.prepare_meshes, 900101, "parse leaves prepare alone");
    check_eq_u64(cost.prepare_texture_bytes, 900102,
                 "parse leaves prepare texture bytes alone");
    check_eq_u64(cost.frame_draw_calls, 900201, "parse leaves frame alone");
    check_eq_u64(cost.frame_triangles, 900202,
                 "parse leaves frame triangles alone");
    check_eq_u64(cost.render_pixels_drawn, 900301, "parse leaves render alone");
    check_eq_u64(cost.render_frame_bytes, 900302,
                 "parse leaves render frame bytes alone");
    check(!cost.measured_prepare && !cost.measured_frame &&
              !cost.measured_render,
          "parse marks no stage but its own");
}

void test_prepare_gatherer() {
    nfsu::PreparedTrackScene scene;
    scene.region.cost.meshes = 3001;
    scene.region.cost.draws = 3002;
    scene.region.cost.drawable_draws = 3003;
    scene.region.cost.textures = 3004;
    scene.region.cost.vertex_buffer_bytes = 3005;
    scene.region.cost.index_buffer_bytes = 3006;
    scene.region.cost.texture_bytes = 3007;
    // Must not be read here.
    scene.region.cost.drawn_triangles = 700101;
    scene.region.cost.texture_binds = 700102;
    scene.region.cost.resolved_draws = 700103;

    scene.cost.placed_instances = 3008;
    scene.cost.unplaced_empty_slot = 3009;
    scene.cost.unplaced_skipped_hash = 3010;
    scene.cost.unplaced_unresolved = 3011;
    scene.cost.resident_entries = 3012;
    scene.cost.resident_triangles = 3013;
    scene.cost.slot_fallbacks = 700104;
    scene.cost.drawn_meshes = 700105;

    // Three placed instances: the instance table's bytes are a function of
    // the vector's length, so the length has to be something other than the
    // placed count above or the two could not be told apart.
    scene.instances.resize(3);

    nfsu::TrackBudgetCost cost = sentinel_cost();
    nfsu::track_budget_prepare_cost(scene, cost);

    check(cost.measured_prepare,
          "the prepare gatherer marks its stage measured");
    check_eq_u64(value_of(cost, "prepare.meshes"), 3001, "prepare.meshes");
    check_eq_u64(value_of(cost, "prepare.draws"), 3002, "prepare.draws");
    check_eq_u64(value_of(cost, "prepare.drawable_draws"), 3003,
                 "prepare.drawable_draws");
    check_eq_u64(value_of(cost, "prepare.textures"), 3004, "prepare.textures");
    check_eq_u64(value_of(cost, "prepare.vertex_buffer_bytes"), 3005,
                 "prepare.vertex_buffer_bytes");
    check_eq_u64(value_of(cost, "prepare.index_buffer_bytes"), 3006,
                 "prepare.index_buffer_bytes");
    check_eq_u64(value_of(cost, "prepare.texture_bytes"), 3007,
                 "prepare.texture_bytes");
    check_eq_u64(value_of(cost, "prepare.placed_instances"), 3008,
                 "prepare.placed_instances");
    // All three unplaced reasons, summed. Dropping any one arm is a
    // different number.
    check_eq_u64(value_of(cost, "prepare.unplaced_instances"),
                 3009 + 3010 + 3011,
                 "prepare.unplaced_instances sums the three reasons");
    check_eq_u64(value_of(cost, "prepare.resident_entries"), 3012,
                 "prepare.resident_entries");
    check_eq_u64(value_of(cost, "prepare.resident_triangles"), 3013,
                 "prepare.resident_triangles");

    const std::uint64_t instance_bytes =
        3ULL * static_cast<std::uint64_t>(sizeof(nfsu::PreparedTrackInstance));
    check_eq_u64(value_of(cost, "prepare.instance_bytes"), instance_bytes,
                 "prepare.instance_bytes is the instance table's length times"
                 " its element size");
    // The resident total is the three buffers PLUS the instance table. Every
    // one of the four is needed: dropping any arm gives a different number
    // because all four values above are distinct.
    check_eq_u64(value_of(cost, "prepare.resident_bytes"),
                 3005 + 3006 + 3007 + instance_bytes,
                 "prepare.resident_bytes sums the three buffers and the"
                 " instance table");

    check_eq_u64(cost.parse_region_bytes, 900001, "prepare leaves parse alone");
    check_eq_u64(cost.parse_meshes, 900002, "prepare leaves parse meshes alone");
    check_eq_u64(cost.frame_draw_calls, 900201, "prepare leaves frame alone");
    check_eq_u64(cost.render_pixels_drawn, 900301,
                 "prepare leaves render alone");
    check(!cost.measured_parse && !cost.measured_frame && !cost.measured_render,
          "prepare marks no stage but its own");
}

void test_frame_gatherer() {
    nfsu::TrackDrawList list;
    list.cost.visible = 4001;
    list.cost.visible_placed = 4002;
    list.cost.visible_unplaced = 4003;
    list.cost.entries = 4004;
    list.cost.triangles = 4005;
    list.cost.groups = 4006;
    list.cost.meshes = 4007;
    list.cost.texture_binds = 4008;
    list.cost.instances_without_entries = 700201;
    // A length that is not cost.entries, so that frame.entry_bytes cannot be
    // read out of the cost figure by accident. A real list holds the two in
    // step; validate_track_draw_list is what checks that, and this layer is
    // a reporter rather than a second validator.
    list.entries.resize(7);

    nfsu::TrackBudgetCost cost = sentinel_cost();
    nfsu::track_budget_frame_cost(list, cost);

    check(cost.measured_frame, "the frame gatherer marks its stage measured");
    check_eq_u64(value_of(cost, "frame.visible_instances"), 4001,
                 "frame.visible_instances");
    check_eq_u64(value_of(cost, "frame.visible_placed"), 4002,
                 "frame.visible_placed");
    check_eq_u64(value_of(cost, "frame.visible_unplaced"), 4003,
                 "frame.visible_unplaced");
    check_eq_u64(value_of(cost, "frame.draw_calls"), 4004,
                 "frame.draw_calls is the entry count");
    check_eq_u64(value_of(cost, "frame.triangles"), 4005, "frame.triangles");
    check_eq_u64(value_of(cost, "frame.groups"), 4006, "frame.groups");
    check_eq_u64(value_of(cost, "frame.meshes"), 4007, "frame.meshes");
    check_eq_u64(value_of(cost, "frame.texture_binds"), 4008,
                 "frame.texture_binds");
    check_eq_u64(value_of(cost, "frame.entry_bytes"),
                 7ULL * static_cast<std::uint64_t>(
                            sizeof(nfsu::TrackDrawListEntry)),
                 "frame.entry_bytes is the entry vector's length times its"
                 " element size");

    check_eq_u64(cost.parse_region_bytes, 900001, "frame leaves parse alone");
    check_eq_u64(cost.prepare_meshes, 900101, "frame leaves prepare alone");
    check_eq_u64(cost.render_pixels_drawn, 900301, "frame leaves render alone");
    check(!cost.measured_parse && !cost.measured_prepare &&
              !cost.measured_render,
          "frame marks no stage but its own");
}

void test_render_gatherer() {
    nfsu::RenderStats stats;
    stats.triangles_submitted = 5001;
    stats.triangles_rasterized = 5002;
    stats.pixel_tests = 5003;
    stats.fragments = 5004;
    stats.fragments_written = 5005;
    stats.pixels_drawn = 5006;
    // Must not be read.
    stats.triangles_front = 700301;
    stats.fragments_depth_failed = 700302;
    stats.pixels_fallback = 700303;
    stats.material_groups = 700304;

    nfsu::TrackRenderStats track_stats;
    track_stats.entries = 5007;
    track_stats.texture_binds = 5008;
    track_stats.entries_drawn = 700305;
    track_stats.groups = 700306;
    track_stats.instances_drawn = 700307;

    // A real frame, so that the byte total is measured over the buffers the
    // renderer actually allocates rather than over a constant this test and
    // the source would both have to keep in step with nfsu::Frame.
    nfsu::Frame frame;
    const unsigned char background[4] = {0, 0, 0, 255};
    const nfsu::RenderLimits limits;
    std::string error;
    check(frame.reset(16, 8, background, limits, error),
          "a 16x8 frame resets: " + error);

    nfsu::TrackBudgetCost cost = sentinel_cost();
    nfsu::track_budget_render_cost(stats, track_stats, frame, cost);

    check(cost.measured_render, "the render gatherer marks its stage measured");
    check_eq_u64(value_of(cost, "render.entries"), 5007, "render.entries");
    check_eq_u64(value_of(cost, "render.triangles_submitted"), 5001,
                 "render.triangles_submitted");
    check_eq_u64(value_of(cost, "render.triangles_rasterized"), 5002,
                 "render.triangles_rasterized");
    check_eq_u64(value_of(cost, "render.pixel_tests"), 5003,
                 "render.pixel_tests");
    check_eq_u64(value_of(cost, "render.fragments"), 5004, "render.fragments");
    check_eq_u64(value_of(cost, "render.fragments_written"), 5005,
                 "render.fragments_written");
    check_eq_u64(value_of(cost, "render.pixels_drawn"), 5006,
                 "render.pixels_drawn");
    // From TrackRenderStats and not from RenderStats: the two count binds
    // for different paths and only the track one is right here.
    check_eq_u64(value_of(cost, "render.texture_binds"), 5008,
                 "render.texture_binds comes from the track statistics");

    // Every one of the four buffers is counted, at its own element size.
    // 16 * 8 = 128 pixels: 4 bytes of colour, 8 of depth, 4 of id and 1 of
    // the blended mask.
    const std::uint64_t pixels = 16ULL * 8ULL;
    const std::uint64_t expected =
        pixels * (static_cast<std::uint64_t>(sizeof(unsigned char)) * 4ULL +
                  static_cast<std::uint64_t>(sizeof(double)) +
                  static_cast<std::uint64_t>(sizeof(std::uint32_t)) +
                  static_cast<std::uint64_t>(sizeof(unsigned char)));
    check_eq_u64(value_of(cost, "render.frame_bytes"), expected,
                 "render.frame_bytes counts all four frame buffers at their"
                 " own element sizes");
    // Not merely non-zero: a version that counted only the colour buffer
    // would still be non-zero and would still scale with the frame.
    check(value_of(cost, "render.frame_bytes") > pixels * 4ULL,
          "render.frame_bytes is more than the colour buffer alone");

    // A second, differently shaped frame, so the figure is shown to track
    // the frame rather than to be a constant.
    nfsu::Frame wider;
    check(wider.reset(32, 8, background, limits, error),
          "a 32x8 frame resets: " + error);
    nfsu::TrackBudgetCost wider_cost;
    nfsu::track_budget_render_cost(stats, track_stats, wider, wider_cost);
    check_eq_u64(value_of(wider_cost, "render.frame_bytes"), expected * 2ULL,
                 "twice the pixels is twice the frame bytes");

    check_eq_u64(cost.parse_region_bytes, 900001, "render leaves parse alone");
    check_eq_u64(cost.prepare_meshes, 900101, "render leaves prepare alone");
    check_eq_u64(cost.frame_draw_calls, 900201, "render leaves frame alone");
    check(!cost.measured_parse && !cost.measured_prepare && !cost.measured_frame,
          "render marks no stage but its own");
}

// ---------------------------------------------------------------------------
// The budget check.
//
// This is the part a gate runs, so its boundaries are pinned on both sides
// and its three refusals are each isolated. The distinction the header
// insists on - the return value says whether the check RAN, not whether it
// passed - is asserted in both directions, because a caller that read a
// false return as "over budget" would report a typo as a regression and a
// caller that read it as "within budget" would report a typo as a pass.
// ---------------------------------------------------------------------------

nfsu::TrackBudgetCost measured_cost() {
    nfsu::TrackBudgetCost cost;
    cost.measured_parse = true;
    cost.measured_prepare = true;
    cost.measured_frame = true;
    cost.measured_render = true;
    cost.frame_draw_calls = 46;
    cost.frame_triangles = 2827;
    cost.frame_texture_binds = 41;
    cost.prepare_resident_bytes = 3173846;
    cost.render_pixels_drawn = 33449;
    return cost;
}

void test_budget_check_boundaries() {
    const nfsu::TrackBudgetCost cost = measured_cost();
    std::vector<nfsu::TrackBudgetExceedance> over;
    std::string error;

    // An empty set runs and finds nothing.
    check(nfsu::check_track_budget(cost, {}, over, error),
          "an empty limit set runs: " + error);
    check(over.empty(), "an empty limit set exceeds nothing");

    // Strictly under.
    check(nfsu::check_track_budget(cost, {{"frame.draw_calls", 47}}, over,
                                   error),
          "a limit above the value runs: " + error);
    check(over.empty(), "46 is within a ceiling of 47");

    // EQUAL to the ceiling is within budget. This is the boundary a budget
    // written from a measurement sits exactly on, so it is the one that
    // decides whether such a budget passes on the day it is written.
    check(nfsu::check_track_budget(cost, {{"frame.draw_calls", 46}}, over,
                                   error),
          "a limit at the value runs: " + error);
    check(over.empty(), "46 is within a ceiling of 46");

    // One over.
    check(nfsu::check_track_budget(cost, {{"frame.draw_calls", 45}}, over,
                                   error),
          "a limit below the value runs: " + error);
    check(over.size() == 1, "46 exceeds a ceiling of 45");
    if (over.size() == 1) {
        check(over[0].name == "frame.draw_calls", "the exceedance is named");
        check(over[0].stage == nfsu::TrackBudgetStage::frame,
              "the exceedance carries its stage");
        check_eq_u64(over[0].value, 46, "the exceedance carries the value");
        check_eq_u64(over[0].ceiling, 45, "the exceedance carries the ceiling");
    }

    // A ceiling of zero on a zero counter is within budget, and on a
    // non-zero one is not. Both are needed: a check written as `value >=
    // ceiling` would fail the first, and one written as `value != 0` would
    // fail the second.
    check(nfsu::check_track_budget(cost, {{"frame.visible_unplaced", 0}}, over,
                                   error),
          "a zero ceiling on a zero counter runs: " + error);
    check(over.empty(), "zero is within a ceiling of zero");
    check(nfsu::check_track_budget(cost, {{"frame.draw_calls", 0}}, over,
                                   error),
          "a zero ceiling on a non-zero counter runs: " + error);
    check(over.size() == 1, "46 exceeds a ceiling of zero");

    // Several limits: the ones that are over are reported, in the order the
    // set gave them, and the ones that are not are silent.
    const std::vector<nfsu::TrackBudgetLimit> mixed = {
        {"frame.triangles", 2827},      // exactly at
        {"frame.draw_calls", 10},       // over
        {"prepare.resident_bytes", 4000000},  // under
        {"render.pixels_drawn", 1},     // over
    };
    check(nfsu::check_track_budget(cost, mixed, over, error),
          "a mixed set runs: " + error);
    check(over.size() == 2, "two of four limits are exceeded");
    if (over.size() == 2) {
        check(over[0].name == "frame.draw_calls",
              "exceedances keep the limit set's order, first");
        check(over[1].name == "render.pixels_drawn",
              "exceedances keep the limit set's order, second");
    }

    // The exceedance list is cleared, not appended to, across calls.
    check(nfsu::check_track_budget(cost, {{"frame.draw_calls", 45}}, over,
                                   error),
          "the check runs again: " + error);
    check(over.size() == 1, "a reused exceedance vector is cleared, not grown");
}

void test_budget_check_refusals() {
    const nfsu::TrackBudgetCost cost = measured_cost();
    std::vector<nfsu::TrackBudgetExceedance> over;
    std::string error;

    // An unknown counter is a typo in the limit set, not a regression.
    error.clear();
    check(!nfsu::check_track_budget(cost, {{"frame.draw_call", 1}}, over,
                                    error),
          "a limit on an unknown counter is refused");
    check(error.find("frame.draw_call") != std::string::npos,
          "the refusal names the counter it could not find: " + error);
    check(over.empty(), "a refused check reports no exceedance");

    // A duplicate: the later one would decide and the earlier would read as
    // enforced.
    error.clear();
    check(!nfsu::check_track_budget(
              cost, {{"frame.draw_calls", 10}, {"frame.draw_calls", 100}}, over,
              error),
          "two limits on one counter are refused");
    check(error.find("frame.draw_calls") != std::string::npos,
          "the duplicate refusal names the counter: " + error);
    check(over.empty(), "a refused duplicate reports no exceedance");

    // A limit on a stage nothing measured. Its value is zero because nothing
    // looked, so it would pass every ceiling and read as a budget held.
    nfsu::TrackBudgetCost partial;
    partial.measured_parse = true;
    partial.parse_meshes = 22;
    error.clear();
    check(!nfsu::check_track_budget(partial, {{"render.pixels_drawn", 5}}, over,
                                    error),
          "a limit on an unmeasured stage is refused");
    // "names the render stage", and NOT merely "render". Every counter is
    // prefixed with its own stage, so `error.find("render")` is satisfied by
    // the echoed counter name alone and would pass with the stage name
    // replaced by anything at all - which is what an independent mutation
    // pass demonstrated with a mutant that replaced it by "?".
    check(error.find("names the render stage") != std::string::npos,
          "the unmeasured refusal names the stage, in a phrase the counter"
          " name cannot supply: " + error);
    check(over.empty(), "a refused unmeasured check reports no exceedance");

    // All FOUR stage arms of the refusal, not only the render one. Only the
    // render arm is reachable from nfsu_probe track-budget, because
    // measure_track_budget_once gathers parse, prepare and frame before it
    // can return, so this test is the only place the other three can be
    // covered at all.
    {
        const struct {
            const char* counter;
            const char* phrase;
        } kArms[] = {
            {"parse.meshes", "names the parse stage"},
            {"prepare.meshes", "names the prepare stage"},
            {"frame.draw_calls", "names the frame stage"},
            {"render.pixels_drawn", "names the render stage"},
        };
        for (const auto& arm : kArms) {
            nfsu::TrackBudgetCost nothing;
            error.clear();
            check(!nfsu::check_track_budget(nothing, {{arm.counter, 0}}, over,
                                            error),
                  std::string("a limit on unmeasured ") + arm.counter +
                      " is refused");
            check(error.find(arm.phrase) != std::string::npos,
                  std::string("the refusal for ") + arm.counter +
                      " names its own stage: " + error);
        }
        // And with every stage measured, all four are checked rather than
        // refused - so the loop above is not passing because the refusal is
        // unconditional.
        nfsu::TrackBudgetCost everything;
        everything.measured_parse = true;
        everything.measured_prepare = true;
        everything.measured_frame = true;
        everything.measured_render = true;
        for (const auto& arm : kArms) {
            error.clear();
            check(nfsu::check_track_budget(everything, {{arm.counter, 0}}, over,
                                           error),
                  std::string("a limit on measured ") + arm.counter +
                      " is checked: " + error);
            check(over.empty(),
                  std::string("zero is within a zero ceiling for ") +
                      arm.counter);
        }

        // EACH STAGE MEASURED ON ITS OWN. The two loops above both set every
        // flag the same way, so a lookup that reads a NEIGHBOUR's flag -
        // parse answering with prepare's, say - gives the right answer in
        // both and survives. Here exactly one flag is set at a time: the
        // limit on that stage must be checked, and a limit on each of the
        // other three must still be refused. A misread flag fails one of the
        // two halves whichever pair it confuses.
        for (std::size_t measured = 0; measured < 4; ++measured) {
            nfsu::TrackBudgetCost only;
            only.measured_parse = (measured == 0);
            only.measured_prepare = (measured == 1);
            only.measured_frame = (measured == 2);
            only.measured_render = (measured == 3);
            for (std::size_t named = 0; named < 4; ++named) {
                const char* counter = kArms[named].counter;
                error.clear();
                const bool ran = nfsu::check_track_budget(
                    only, {{counter, 0}}, over, error);
                if (named == measured) {
                    check(ran, std::string("with only ") +
                                   kArms[measured].counter +
                                   "'s stage measured, its own limit is"
                                   " checked: " + error);
                } else {
                    check(!ran, std::string("with only ") +
                                    kArms[measured].counter +
                                    "'s stage measured, a limit on " +
                                    counter + " is refused");
                }
            }
        }
    }
    // The same counter, once its stage has been measured, is checked
    // normally. Without this the refusal above could be a blanket one.
    partial.measured_render = true;
    partial.render_pixels_drawn = 4;
    error.clear();
    check(nfsu::check_track_budget(partial, {{"render.pixels_drawn", 5}}, over,
                                   error),
          "the same limit runs once its stage is measured: " + error);
    check(over.empty(), "4 is within a ceiling of 5");
    // And a measured stage whose counter is genuinely zero is NOT refused:
    // the refusal is about the stage, not about the value.
    nfsu::TrackBudgetCost measured_zero;
    measured_zero.measured_frame = true;
    error.clear();
    check(nfsu::check_track_budget(measured_zero, {{"frame.draw_calls", 0}},
                                   over, error),
          "a measured stage with a zero counter is checked, not refused: " +
              error);
    check(over.empty(), "a measured zero is within a ceiling of zero");

    // EVERY refusal is checked over the whole set before any exceedance is
    // reported. A set holding both a typo and a real exceedance must report
    // the typo: otherwise a limit set with a typo in it reads as a budget
    // result, which is the failure this ordering exists to prevent. The typo
    // is placed AFTER the exceedance so that a check doing the two in one
    // pass would have already recorded the exceedance.
    error.clear();
    check(!nfsu::check_track_budget(
              cost, {{"frame.draw_calls", 1}, {"frame.no_such_counter", 1}},
              over, error),
          "a set holding an exceedance and a typo is refused for the typo");
    check(error.find("frame.no_such_counter") != std::string::npos,
          "the refusal names the typo rather than the exceedance: " + error);
    check(over.empty(),
          "a refused set reports no exceedance even though one was over");
}

// The header promises that a refusal leaves `exceedances` EMPTY, not merely
// "unchanged" or "not appended to". Every refusal case above starts from a
// default-constructed, already-empty vector, so an implementation that
// forgot to clear it beforehand would pass all of them too - the vector
// stays empty because nothing was ever there to leave behind. Here it starts
// pre-filled with a stale entry that does not correspond to anything in this
// call's own limit set, so only an actual clear (not merely never pushing to
// a fresh vector) can leave it empty afterward.
void test_budget_check_refusal_clears_prefilled_exceedances() {
    const nfsu::TrackBudgetCost cost = measured_cost();
    std::vector<nfsu::TrackBudgetExceedance> over;
    nfsu::TrackBudgetExceedance stale;
    stale.name = "stale.counter";
    stale.stage = nfsu::TrackBudgetStage::parse;
    stale.value = 999;
    stale.ceiling = 1;
    over.push_back(stale);
    std::string error;

    check(!nfsu::check_track_budget(cost, {{"frame.draw_call", 1}}, over,
                                    error),
          "a limit on an unknown counter is still refused with a pre-filled"
          " exceedances vector");
    check(over.empty(),
          "a refusal clears a pre-filled exceedances vector rather than"
          " leaving the caller's stale entry in it");
}

// ---------------------------------------------------------------------------
// Repetition comparison and sample statistics.
//
// Both of these were local to src/main.cpp in an earlier revision, and an
// independent mutation sweep found that EVERY fault in them survived every
// gate. Neither is reachable from a command-level test: a summary of
// wall-clock times cannot be asserted against a value, and a comparison that
// only runs over equal costs never takes its failing branch. Here the inputs
// are chosen so that both have exact expected answers.
// ---------------------------------------------------------------------------

void check_eq_double(double got, double want, const std::string& what) {
    // The values below are chosen to be exact in binary except where a
    // square root is involved, so this tolerance is about the one irrational
    // expectation and not a licence for the rest to drift.
    const double tolerance = 1e-12;
    const double delta = got > want ? got - want : want - got;
    check(delta <= tolerance,
          what + ": got " + std::to_string(got) + ", want " +
              std::to_string(want));
}

void test_costs_equal() {
    nfsu::TrackBudgetCost a = measured_cost();
    nfsu::TrackBudgetCost b = measured_cost();
    std::string difference;

    check(nfsu::track_budget_costs_equal(a, b, difference),
          "two identical costs compare equal: " + difference);

    // Two costs that measured nothing are equal too. Without this the
    // function could be "always false" and still pass the case below.
    nfsu::TrackBudgetCost empty_a;
    nfsu::TrackBudgetCost empty_b;
    check(nfsu::track_budget_costs_equal(empty_a, empty_b, difference),
          "two empty costs compare equal: " + difference);

    // One counter differs. The message names the counter and BOTH values,
    // because a report that named only one would leave a reader unable to
    // say which run was the odd one.
    b.frame_draw_calls = 47;
    difference.clear();
    check(!nfsu::track_budget_costs_equal(a, b, difference),
          "a differing counter is not equal");
    // The whole phrase, in order, and not the three pieces separately. Two
    // values searched for independently are both present when the message
    // prints them the wrong way round, and the comment above this block
    // says a reader has to be able to tell WHICH run was the odd one - so
    // asserting them apart would be asserting something other than what is
    // claimed. An independent mutation pass found exactly that shape in the
    // neighbouring assertion.
    check(difference.find("frame.draw_calls: 46 against 47") !=
              std::string::npos,
          "the difference names the counter and both values, in the order"
          " the arguments were given: " + difference);

    // The comparison is symmetric, and says so the other way round.
    difference.clear();
    check(!nfsu::track_budget_costs_equal(b, a, difference),
          "the comparison is symmetric");
    check(difference.find("frame.draw_calls: 47 against 46") !=
              std::string::npos,
          "swapping the arguments swaps the reported values: " + difference);

    // THE FIRST AND LAST ROWS OF THE TABLE. A walk that starts at 1 or
    // stops one short still finds a difference in the middle of the table,
    // which is where every other case here puts one, so neither end is
    // covered by them.
    {
        nfsu::TrackBudgetCost first_row = measured_cost();
        nfsu::TrackBudgetCost first_row_other = measured_cost();
        first_row_other.parse_region_bytes = first_row.parse_region_bytes + 1;
        difference.clear();
        check(!nfsu::track_budget_costs_equal(first_row, first_row_other,
                                              difference),
              "a difference in the table's FIRST counter is found");
        check(difference.find("parse.region_bytes") != std::string::npos,
              "and it is named: " + difference);

        nfsu::TrackBudgetCost last_row = measured_cost();
        nfsu::TrackBudgetCost last_row_other = measured_cost();
        last_row_other.render_frame_bytes = last_row.render_frame_bytes + 1;
        difference.clear();
        check(!nfsu::track_budget_costs_equal(last_row, last_row_other,
                                              difference),
              "a difference in the table's LAST counter is found");
        check(difference.find("render.frame_bytes") != std::string::npos,
              "and it is named: " + difference);
    }

    // The FIRST difference in table order is the one reported. Both costs
    // below differ in two counters, one early and one late, and the early
    // one is the answer.
    nfsu::TrackBudgetCost early = measured_cost();
    nfsu::TrackBudgetCost late = measured_cost();
    late.parse_meshes = 999;
    late.render_pixels_drawn = 999;
    early.parse_meshes = 1;
    early.render_pixels_drawn = 1;
    difference.clear();
    check(!nfsu::track_budget_costs_equal(early, late, difference),
          "two differences are not equal");
    check(difference.find("parse.meshes") != std::string::npos,
          "the earlier difference in table order is reported: " + difference);

    // A stage one cost measured and the other did not is a difference even
    // when both values are zero, because a zero that nothing measured does
    // not mean the same thing as a zero that was measured.
    nfsu::TrackBudgetCost measured;
    measured.measured_frame = true;
    nfsu::TrackBudgetCost unmeasured;
    difference.clear();
    check(!nfsu::track_budget_costs_equal(measured, unmeasured, difference),
          "a stage measured by one cost and not the other is a difference");
    // "the frame stage was measured", not "frame". Every counter is
    // prefixed with its own stage, so `difference.find("frame")` is
    // satisfied by the echoed counter name `frame.visible_instances` before
    // the stage name is ever appended - the same vacuous shape as the one
    // repaired in check_track_budget's refusal, which an independent pass
    // found repeated here in the very commit that repaired the first.
    check(difference.find("the frame stage was measured") != std::string::npos,
          "the difference names the stage, in a phrase the counter name"
          " cannot supply: " + difference);
}

void test_sample_summary() {
    // An empty set: zeros rather than a division by zero.
    const nfsu::TrackSampleSummary none = nfsu::summarize_track_samples({});
    check(none.samples == 0, "an empty sample set has no samples");
    check_eq_double(none.min, 0.0, "an empty set's min");
    check_eq_double(none.mean, 0.0, "an empty set's mean");
    check_eq_double(none.stddev, 0.0, "an empty set's standard deviation");

    // One sample: the spread is zero, and the standard deviation must not
    // divide by n-1 == 0.
    const nfsu::TrackSampleSummary one = nfsu::summarize_track_samples({5.0});
    check(one.samples == 1, "one sample");
    check_eq_double(one.min, 5.0, "one sample's min");
    check_eq_double(one.median, 5.0, "one sample's median");
    check_eq_double(one.mean, 5.0, "one sample's mean");
    check_eq_double(one.max, 5.0, "one sample's max");
    check_eq_double(one.stddev, 0.0, "one sample's standard deviation");

    // TWO samples, which is the smallest count that has a spread at all and
    // the boundary the n > 1 guard sits on. A guard written n > 2 leaves
    // this reporting no spread, and every other case in this function - 1,
    // 3 and 4 samples - passes with it.
    const nfsu::TrackSampleSummary two =
        nfsu::summarize_track_samples({4.0, 2.0});
    check(two.samples == 2, "two samples");
    check_eq_double(two.min, 2.0, "two samples' min");
    check_eq_double(two.max, 4.0, "two samples' max");
    check_eq_double(two.median, 3.0, "two samples' median");
    check_eq_double(two.mean, 3.0, "two samples' mean");
    // sqrt(((2-3)^2 + (4-3)^2) / (2-1)) = sqrt(2).
    check_eq_double(two.stddev, 1.4142135623730951,
                    "two samples have a spread");

    // Three samples, GIVEN OUT OF ORDER. Every figure below is wrong if the
    // sort is dropped: unsorted, min would be 3 and max 2.
    const nfsu::TrackSampleSummary odd =
        nfsu::summarize_track_samples({3.0, 1.0, 2.0});
    check(odd.samples == 3, "three samples");
    check_eq_double(odd.min, 1.0, "an unsorted set's min is the smallest");
    check_eq_double(odd.max, 3.0, "an unsorted set's max is the largest");
    check_eq_double(odd.median, 2.0, "an odd count's median is the middle");
    check_eq_double(odd.mean, 2.0, "the mean of 1, 2 and 3");
    // The SAMPLE standard deviation: sqrt(((1-2)^2 + 0 + (3-2)^2) / (3-1))
    // is sqrt(1) = 1. The population form would divide by 3 and give
    // sqrt(2/3) = 0.8165, so this value distinguishes the two.
    check_eq_double(odd.stddev, 1.0,
                    "the sample standard deviation divides by n-1");

    // Four samples, so the median is the mean of the two middle values.
    // samples[n/2] alone would be 3.
    const nfsu::TrackSampleSummary even =
        nfsu::summarize_track_samples({4.0, 1.0, 3.0, 2.0});
    check(even.samples == 4, "four samples");
    check_eq_double(even.min, 1.0, "an even count's min");
    check_eq_double(even.max, 4.0, "an even count's max");
    check_eq_double(even.median, 2.5,
                    "an even count's median is the mean of the two middle"
                    " values");
    check_eq_double(even.mean, 2.5, "the mean of 1, 2, 3 and 4");
    // sqrt((2.25 + 0.25 + 0.25 + 2.25) / 3) = sqrt(5/3).
    check_eq_double(even.stddev, 1.2909944487358056,
                    "an even count's sample standard deviation");

    // Repeated values: the spread is zero even though there are four of
    // them, which is what a stage whose time never moves should report.
    const nfsu::TrackSampleSummary flat =
        nfsu::summarize_track_samples({7.0, 7.0, 7.0, 7.0});
    check_eq_double(flat.stddev, 0.0, "identical samples have no spread");
    check_eq_double(flat.median, 7.0, "identical samples have that median");

    // The caller's vector is taken by value, so a caller that summarizes one
    // phase and then prints its own samples still has them in its own order.
    std::vector<double> mine = {3.0, 1.0, 2.0};
    (void)nfsu::summarize_track_samples(mine);
    check(mine.size() == 3 && mine[0] == 3.0,
          "summarizing does not sort the caller's own vector");
}

} // namespace

int main() {
    test_counter_table();
    test_parse_gatherer();
    test_prepare_gatherer();
    test_frame_gatherer();
    test_render_gatherer();
    test_budget_check_boundaries();
    test_budget_check_refusals();
    test_budget_check_refusal_clears_prefilled_exceedances();
    test_costs_equal();
    test_sample_summary();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "track budget tests passed\n";
    return 0;
}
