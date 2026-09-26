#include "nfsu/track_residency.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>

namespace nfsu {

namespace {

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t top = std::numeric_limits<std::uint64_t>::max();
    return a > top - b ? top : a + b;
}

// The 0x00034153 record 0x004446A1..0x004446CB stores at +0x598: the first
// one keyed on the track. TrackZoneReport::section_lists holds the
// 0x00034153 records first, in record order.
const TrackZoneSectionList* route_list(const TrackZoneReport& zones,
                                       std::uint32_t track) noexcept {
    for (const TrackZoneSectionList& list : zones.section_lists) {
        if (list.chunk_id == kTrackZoneTrackListId && list.key == track) {
            return &list;
        }
    }
    return nullptr;
}

// The 0x00034154 record 0x00444BA4 takes: the first keyed on the track.
const TrackZoneSectionList* extra_list(const TrackZoneReport& zones,
                                       std::uint32_t track) noexcept {
    for (const TrackZoneSectionList& list : zones.section_lists) {
        if (list.chunk_id == kTrackZoneExtraListId && list.key == track) {
            return &list;
        }
    }
    return nullptr;
}

// The entries a list's count names, never its capacity (TRACK_WHOLE.md,
// the first deliberate break).
void append_list(const TrackZoneSectionList& list,
                 std::vector<std::int32_t>& numbers) {
    for (std::size_t k = 0;
         list.count > 0 && k < static_cast<std::size_t>(list.count) &&
         k < list.sections.size();
         ++k) {
        numbers.push_back(list.sections[k]);
    }
}

// The zone 0x00444C3C..0x00444C55 walks list 3 for: the one whose key is
// `key`. Keys are unique in a valid report, so list order does not matter.
std::size_t zone_by_key(const TrackZoneReport& zones, std::int32_t key) noexcept {
    for (std::size_t z = 0; z < zones.zones.size(); ++z) {
        if (zones.zones[z].key == key) {
            return z;
        }
    }
    return kTrackZoneNone;
}

bool route_names(const TrackZoneSectionList& route, std::int32_t key) noexcept {
    // 0x00444A32..0x00444A4D: the count's entries, compared as words.
    for (std::size_t k = 0;
         route.count > 0 && k < static_cast<std::size_t>(route.count) &&
         k < route.sections.size();
         ++k) {
        if (route.sections[k] == static_cast<std::int16_t>(key)) {
            return true;
        }
    }
    return false;
}

enum RecordState : std::uint8_t {
    kIdle = 0,    // +0xC 0
    kPlaced = 1,  // +0xC 1, 0x00444DFB: memory taken, read not started
    kLoaded = 3,  // +0xC 3, 0x00444747
};

struct Slot {
    bool live = false;
    TrackResidencySample sample;
    // +0x12, +0x14 and +0x18 of the slot record.
    std::int32_t committed = kTrackResidencyNoZone;
    std::int32_t last = 0;
    std::int32_t count = -1;
};

class Streamer {
public:
    Streamer(const TrackStreamTable& table, const TrackZoneReport& zones,
             std::uint32_t track, const std::vector<TrackResidencyCost>& costs,
             const TrackResidencyOptions& options)
        : table_(table),
          zones_(zones),
          costs_(costs),
          options_(options),
          route_(route_list(zones, track)),
          extra_(extra_list(zones, track)),
          state_(table.records.size(), kIdle),
          requested_(table.records.size(), 0),
          stamp_(table.records.size(), 0) {}

    // One frame of 0x00445050 -> 0x00445180. `sample` is slot 0's input, or
    // null while no car is live.
    void step(std::size_t index, const TrackResidencySample* sample,
              TrackResidencyStep& out, TrackResidencyRun& run) {
        out = TrackResidencyStep{};
        out.index = index;
        tick_ = index;
        slots_[0].live = sample != nullptr;
        if (sample != nullptr) {
            slots_[0].sample = *sample;
        }

        std::int32_t keys[kTrackResidencySlots] = {kTrackResidencyNoZone,
                                                   kTrackResidencyNoZone};
        const bool changed = sample_slots(keys, out, run);

        // 0x00445198..0x004451A4: nothing to do once the set is loaded,
        // unless a slot's key changed.
        if (!(set_loaded_ && !changed)) {
            if (changed) {
                rebuild(keys, out, run);
            }
            bool proceed = true;
            if (!pass_done_) {
                // 0x00445280: a false return gives the frame up before any
                // read starts (0x00445287 `je 0x44531a`).
                proceed = place_with_release(out);
                out.placed = true;
                if (proceed) {
                    pass_done_ = true;
                }
            }
            if (proceed) {
                const bool started = start_reads(out);
                // 0x00445302..0x00445311. Reads complete between steps in
                // this model, so a step that started any leaves the rest for
                // the next one, as retail waits for reads in flight.
                if (!started) {
                    if (deferred_) {
                        pass_done_ = false;
                    } else {
                        // 0x00445323 and 0x0044533B.
                        set_loaded_ = true;
                        track_bytes_done_ = true;
                    }
                }
            }
        }

        out.committed_key = slots_[0].committed;
        out.set_loaded = set_loaded_;
        out.requested = list_.size();
        std::size_t resident = 0;
        for (const std::uint8_t state : state_) {
            resident += state == kLoaded ? 1U : 0U;
        }
        out.resident = resident;
        out.held_bytes = held_;
    }

    // What 0x00446F9E does once the zones have loaded: force a rebuild and
    // reset both slots' counts.
    void force_rebuild() noexcept {
        force_ = true;
        for (Slot& slot : slots_) {
            slot.count = -1;
        }
    }

    // 0x00444840: a car placed in slot 0.
    void place_car() noexcept {
        slots_[0].count = -1;
        slots_[0].last = 0;
    }

    bool set_loaded() const noexcept { return set_loaded_; }
    const std::vector<std::uint8_t>& state() const noexcept { return state_; }

private:
    // 0x00444FB0.
    bool sample_slots(std::int32_t (&keys)[kTrackResidencySlots],
                      TrackResidencyStep& out, TrackResidencyRun& run) {
        bool changed = false;
        for (std::size_t k = 0; k < kTrackResidencySlots; ++k) {
            Slot& slot = slots_[k];
            std::int32_t key = kTrackResidencyNoZone;
            if (slot.live) {
                TrackResidencyLookAhead detail;
                key = track_residency_slot_key(zones_, route_, slot.sample,
                                               &detail);
                if (k == 0) {
                    out.sampled_key = key;
                    run.route_query_samples += detail.route_query_skipped ? 1U : 0U;
                }
            }
            // Keys are words (0x00444FE4 `cmp ax, word ptr [esi - 4]`).
            key = static_cast<std::int16_t>(key);
            if (key == slot.last) {
                // 0x00444FEC. Saturated here; retail's dword would take
                // more than a year of frames to wrap.
                if (slot.count < std::numeric_limits<std::int32_t>::max()) {
                    ++slot.count;
                }
            } else {
                slot.last = key;
                slot.count = slot.count == -1 ? kTrackResidencyFirstSampleCount : 1;
            }
            std::int32_t result = key;
            if (result != slot.committed && slot.count < kTrackResidencyCommitCount) {
                result = slot.committed;
            }
            if (result != slot.committed) {
                changed = true;
            }
            keys[k] = result;
        }
        // 0x00445032..0x0044503F: +0x179 forces one.
        changed = changed || force_;
        return changed;
    }

    // 0x004451CD..0x0044526F.
    void rebuild(const std::int32_t (&keys)[kTrackResidencySlots],
                 TrackResidencyStep& out, TrackResidencyRun& run) {
        force_ = false;
        unplace();                             // 0x00444E90
        for (const std::size_t r : list_) {    // 0x004443E0
            requested_[r] = 0;
        }
        list_.clear();
        set_loaded_ = false;
        pass_done_ = false;
        deferred_ = false;
        for (std::size_t k = 0; k < kTrackResidencySlots; ++k) {
            slots_[k].committed = keys[k];     // 0x0044522F
        }
        build_request();                       // 0x00444B50
        out.rebuilt = true;
        ++run.rebuilds;

        if (options_.policy == TrackResidencyPolicy::zone_only) {
            // NOT retail: release every loaded record the new request does
            // not name, in table order, now rather than under pressure.
            for (std::size_t r = 0; r < state_.size(); ++r) {
                if (state_[r] == kLoaded && requested_[r] == 0) {
                    release(r, out);
                }
            }
        }
    }

    // 0x00444B50, single player, outside the front end.
    void build_request() {
        std::vector<std::int32_t> numbers(std::begin(kTrackStreamFixedSections),
                                          std::end(kTrackStreamFixedSections));
        if (extra_ != nullptr) {
            append_list(*extra_, numbers);
        }
        for (const Slot& slot : slots_) {
            // 0x00444C37 `test cx, cx` / `jle`.
            if (slot.committed <= 0) {
                continue;
            }
            // A committed key came out of this report's own zones, so the
            // lookup cannot miss; 0x00444C57 would read through a null
            // pointer if it did, and this skips instead.
            const std::size_t zone = zone_by_key(zones_, slot.committed);
            if (zone == kTrackZoneNone) {
                continue;
            }
            const TrackZone& found = zones_.zones[zone];
            for (std::size_t k = 0;
                 found.section_count > 0 &&
                 k < static_cast<std::size_t>(found.section_count) &&
                 k < kTrackZoneMaxSections;
                 ++k) {
                numbers.push_back(found.sections[k]);
            }
        }
        // 0x00444C94: the track byte, only until the first set is loaded.
        if (options_.policy == TrackResidencyPolicy::retail && !track_bytes_done_) {
            for (const TrackStreamRecord& record : table_.records) {
                if (record.track_flag != 0) {
                    numbers.push_back(record.number);
                }
            }
        }
        // 0x00444D0E..0x00444D73.
        for (const std::int32_t number : numbers) {
            const std::size_t r = find_track_stream_record(table_, number);
            if (r == kTrackStreamNoRecord) {
                continue;
            }
            stamp_[r] = tick_;
            if (requested_[r] != 0) {
                continue;
            }
            requested_[r] = 1;
            if (list_.size() == kTrackStreamRequestLimit) {
                break;
            }
            list_.push_back(r);
        }
    }

    std::uint64_t free_room() const noexcept {
        std::uint64_t used = saturating_add(held_, in_flight_);
        std::uint64_t room = used >= options_.budget_bytes
                                 ? 0
                                 : options_.budget_bytes - used;
        // 0x00444DC9..0x00444DCE: less the block header, and never below 0.
        return room > options_.block_header ? room - options_.block_header : 0;
    }

    // 0x00444D80. Returns the place bytes of the records it skipped.
    std::uint64_t place_pass() {
        std::uint64_t unplaced = 0;
        std::size_t placed = 0;
        for (const std::size_t r : list_) {
            if (state_[r] != kIdle) {
                continue;
            }
            const std::uint64_t need =
                saturating_add(costs_[r].place_bytes, options_.placement_slack);
            if (need > free_room()) {
                unplaced = saturating_add(unplaced, costs_[r].place_bytes);
                continue;
            }
            state_[r] = kPlaced;
            in_flight_ = saturating_add(in_flight_, costs_[r].place_bytes);
            ++placed;
            if (placed >= options_.placements_per_pass) {
                deferred_ = true;
                return unplaced;
            }
        }
        deferred_ = false;
        return unplaced;
    }

    // 0x00444E90: every record of the table that is placed and not read
    // goes back (0x00444EAB `cmp eax, 1`).
    void unplace() noexcept {
        for (std::size_t r = 0; r < state_.size(); ++r) {
            if (state_[r] == kPlaced) {
                state_[r] = kIdle;
                in_flight_ -= costs_[r].place_bytes;
            }
        }
    }

    void release(std::size_t r, TrackResidencyStep& out) {
        state_[r] = kIdle;
        held_ -= costs_[r].held_bytes;
        out.releases.push_back(r);
    }

    // 0x00444780: the loaded, unrequested record with the smallest stamp,
    // the first in table order among equals (0x004447B5 `jge`). Returns
    // false when there is none; otherwise releases it and reports its place
    // bytes, which is what retail returns (0x00444822 `mov eax, [edi+0x18]`).
    bool release_one(TrackResidencyStep& out, std::uint64_t& freed) {
        std::size_t best = kTrackStreamNoRecord;
        for (std::size_t r = 0; r < state_.size(); ++r) {
            if (state_[r] != kLoaded || requested_[r] != 0) {
                continue;
            }
            if (best == kTrackStreamNoRecord || stamp_[r] < stamp_[best]) {
                best = r;
            }
        }
        if (best == kTrackStreamNoRecord) {
            return false;
        }
        release(best, out);
        freed = costs_[best].place_bytes;
        return true;
    }

    // 0x00444EE0.
    bool place_with_release(TrackResidencyStep& out) {
        std::uint64_t released = 0;
        unplace();
        std::uint64_t unplaced = place_pass();
        while (unplaced != 0) {
            // 0x00444F00 / 0x00444F06 `jg`: the limit is tested before each
            // release, so the release after the test may carry the total
            // past it by one whole record.
            if (released > options_.release_pass_limit) {
                // 0x00444F42: the frame is given up. The tentative
                // placements stay until the next pass frees them first.
                out.release_capped = true;
                out.unplaced_bytes = unplaced;
                return false;
            }
            unplace();
            std::uint64_t freed = 0;
            const bool any = release_one(out, freed);
            released = saturating_add(released, freed);
            // 0x00444F20 `jg`: a release that freed nothing counts as none,
            // and 0x00444830 frees nothing either (`xor eax, eax`).
            if (!any || freed == 0) {
                // 0x00444F48..0x00444F50: give up, place what fits.
                out.over_budget = true;
                unplace();
                unplaced = place_pass();
                break;
            }
            unplace();
            unplaced = place_pass();
        }
        out.unplaced_bytes = unplaced;
        return true;
    }

    // 0x004452A8..0x004452F6 starts reads while fewer than three are in
    // flight (0x004452FB `cmp dword ptr [edi + 0x10], 3`). Retail picks the
    // placed record of more than 1000000 bytes (0x004452C9 `cmp dword ptr
    // [eax + 0x18], 0xf4240`) or else the highest number first; this model
    // starts them in list order, which is the order they were placed. A pass
    // places at most placements_per_pass records, so with the default of two
    // every one starts in the same step and the order changes nothing.
    // Reads complete before the next step in this model; a placed record
    // left over starts then.
    bool start_reads(TrackResidencyStep& out) {
        std::size_t started = 0;
        for (const std::size_t r : list_) {
            if (started == kTrackResidencyReadsInFlight) {
                break;
            }
            if (state_[r] != kPlaced) {
                continue;
            }
            state_[r] = kLoaded;
            in_flight_ -= costs_[r].place_bytes;
            held_ = saturating_add(held_, costs_[r].held_bytes);
            out.loads.push_back(r);
            ++started;
        }
        return started != 0;
    }

    const TrackStreamTable& table_;
    const TrackZoneReport& zones_;
    const std::vector<TrackResidencyCost>& costs_;
    const TrackResidencyOptions& options_;
    const TrackZoneSectionList* route_;
    const TrackZoneSectionList* extra_;
    std::vector<std::uint8_t> state_;
    std::vector<std::uint8_t> requested_;
    std::vector<std::uint64_t> stamp_;
    std::vector<std::size_t> list_;
    Slot slots_[kTrackResidencySlots];
    std::uint64_t held_ = 0;
    std::uint64_t in_flight_ = 0;
    std::uint64_t tick_ = 0;
    bool force_ = false;
    bool set_loaded_ = false;       // +0x15D
    bool pass_done_ = false;        // +0x15E
    bool deferred_ = false;         // +0x15F
    bool track_bytes_done_ = false; // +0x178
};

void record_step(const TrackResidencyStep& step, TrackResidencyRun& run) {
    run.loads += step.loads.size();
    run.releases += step.releases.size();
    run.over_budget_steps += step.over_budget ? 1U : 0U;
    run.unplaced_steps += step.unplaced_bytes != 0 ? 1U : 0U;
    run.release_capped_steps += step.release_capped ? 1U : 0U;
    if (run.steps.empty() || step.held_bytes > run.peak_held_bytes) {
        run.peak_held_bytes = step.held_bytes;
        run.peak_step = step.index;
    }
    run.steps.push_back(step);
}

std::string numbers_of(const TrackStreamTable& table,
                       const std::vector<std::size_t>& records) {
    if (records.empty()) {
        return "-";
    }
    std::string text;
    for (std::size_t k = 0; k < records.size(); ++k) {
        if (k != 0) {
            text += ',';
        }
        text += records[k] < table.records.size()
                    ? std::to_string(table.records[records[k]].number)
                    : std::string("?");
    }
    return text;
}

} // namespace

std::int32_t track_residency_slot_key(
    const TrackZoneReport& zones,
    const TrackZoneSectionList* route,
    const TrackResidencySample& sample,
    TrackResidencyLookAhead* detail) noexcept {
    TrackResidencyLookAhead local;
    TrackResidencyLookAhead& out = detail != nullptr ? *detail : local;
    out = TrackResidencyLookAhead{};

    const float px = sample.position[0];
    const float pz = sample.position[1];
    const float vx = sample.velocity[0];
    const float vz = sample.velocity[1];
    // 0x00444877..0x00444887: fsqrt in retail, a correctly rounded root
    // here, so that no build takes it from the C library.
    const float speed = track_residency_sqrtf(vx * vx + vz * vz);
    out.route_query_skipped = speed > kTrackResidencyRouteQuerySpeed;
    float dx = 0.0F;
    float dz = 0.0F;
    // 0x00444891 `fcomp` / `test ah, 0x41` / `jne`: a look-ahead only when
    // the speed is not above the cap. NaN is unordered and takes the jump,
    // as retail's would.
    if (!(speed > kTrackResidencyLookAheadMaxSpeed)) {
        float end_x = 0.0F;
        float end_z = 0.0F;
        if (kTrackResidencyLookAheadSeconds * speed > kTrackResidencyLookAheadDistance) {
            // 0x004448BC..0x004448E9.
            const float scale = kTrackResidencyLookAheadDistance / speed;
            end_x = scale * vx + px;
            end_z = scale * vz + pz;
        } else {
            end_x = kTrackResidencyLookAheadSeconds * vx + px;
            end_z = kTrackResidencyLookAheadSeconds * vz + pz;
        }
        dx = end_x - px;
        dz = end_z - pz;
    }
    for (std::size_t i = 0; i < kTrackResidencyLookAheadPoints; ++i) {
        const float fraction =
            static_cast<float>(i) * kTrackResidencyLookAheadFraction;
        const float point[2] = {dx * fraction + px, dz * fraction + pz};
        out.points[i][0] = point[0];
        out.points[i][1] = point[1];
        const std::size_t zone = find_track_zone(zones, point);
        out.zones[i] = zone;
        if (zone == kTrackZoneNone) {
            continue;
        }
        const std::int32_t key = zones.zones[zone].key;
        // 0x00444A20: the slot's own point accepts any zone; 0x00444A2E: so
        // does a look-ahead point when the track has no route record.
        if (i == 0 || route == nullptr || route_names(*route, key)) {
            out.keys[i] = key;
        }
    }
    // 0x00444A76..0x00444AA4.
    std::int32_t result = out.keys[kTrackResidencyLookAheadPoints - 1];
    for (std::size_t i = 1; i < kTrackResidencyLookAheadPoints; ++i) {
        if (out.keys[i] == kTrackResidencyNoZone) {
            result = out.keys[i - 1];
            break;
        }
    }
    out.key = result;
    return result;
}

double track_residency_sqrt(double x) noexcept {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    const std::uint64_t sign = bits >> 63;
    const std::uint64_t biased = (bits >> 52) & 0x7FFU;
    std::uint64_t fraction = bits & ((std::uint64_t{1} << 52) - 1U);
    if (biased == 0 && fraction == 0) {
        return x;  // +0 and -0
    }
    if (biased == 0x7FFU && fraction == 0 && sign == 0) {
        return x;  // +infinity
    }
    if (sign != 0 || biased == 0x7FFU) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // x = m * 2^e with m in [2^52, 2^53), subnormals normalised.
    std::uint64_t m = 0;
    std::int64_t e = 0;
    if (biased == 0) {
        m = fraction;
        e = -1074;
        while (m < (std::uint64_t{1} << 52)) {
            m <<= 1U;
            --e;
        }
    } else {
        m = fraction | (std::uint64_t{1} << 52);
        e = static_cast<std::int64_t>(biased) - 1075;
    }
    // An even exponent, so that sqrt(2^e) is a power of two: m in [2^52, 2^54).
    if ((e & 1) != 0) {
        m <<= 1U;
        --e;
    }
    // The integer root of N = m * 2^54, a number of at most 108 bits, one
    // bit a turn from the top pair down (the digit-by-digit method). The
    // root q lies in [2^53, 2^54): 53 bits, one more for rounding, and the
    // remainder says whether anything lies below that. N's low 54 bits are
    // zero, so its pairs come from m and then are zero; the remainder stays
    // below 2q + 1 < 2^55, so nothing here needs more than 64 bits.
    std::uint64_t q = 0;
    std::uint64_t r = 0;
    for (int pair = 53; pair >= 0; --pair) {
        const int shift = 2 * pair - 54;
        const std::uint64_t digits = shift >= 0 ? (m >> shift) & 3U : 0U;
        r = (r << 2U) | digits;
        const std::uint64_t trial = (q << 2U) | 1U;
        if (r >= trial) {
            r -= trial;
            q = (q << 1U) | 1U;
        } else {
            q <<= 1U;
        }
    }
    // sqrt(x) = sqrt(N) * 2^(e/2 - 27) = q.rest * 2^(e/2 - 27). Round the
    // 54-bit q to 53 bits, to nearest and ties to even; an exact tie cannot
    // happen for a root, but the rule is the standard's.
    std::uint64_t mantissa = q >> 1U;
    const bool half = (q & 1U) != 0;
    const bool below = r != 0;
    std::int64_t exponent = e / 2 - 26;  // of mantissa's lowest bit
    if (half && (below || (mantissa & 1U) != 0)) {
        ++mantissa;
        if (mantissa == (std::uint64_t{1} << 53)) {
            mantissa >>= 1U;
            ++exponent;
        }
    }
    // mantissa * 2^exponent with mantissa in [2^52, 2^53): the biased
    // exponent of the result is exponent + 52 + 1023. The root of any
    // positive double is a normal double, so it is always in range.
    const std::uint64_t out_biased = static_cast<std::uint64_t>(exponent + 1075);
    const std::uint64_t out =
        (out_biased << 52) | (mantissa & ((std::uint64_t{1} << 52) - 1U));
    double result = 0.0;
    std::memcpy(&result, &out, sizeof(result));
    return result;
}

float track_residency_sqrtf(float x) noexcept {
    return static_cast<float>(track_residency_sqrt(static_cast<double>(x)));
}

const char* track_residency_policy_name(TrackResidencyPolicy policy) noexcept {
    switch (policy) {
        case TrackResidencyPolicy::retail:
            return "retail";
        case TrackResidencyPolicy::zone_only:
            return "zone-only";
    }
    return "unknown";
}

std::vector<TrackResidencyCost> track_residency_table_costs(
    const TrackStreamTable& table) {
    std::vector<TrackResidencyCost> costs(table.records.size());
    for (std::size_t r = 0; r < table.records.size(); ++r) {
        costs[r].place_bytes = table.records[r].size;
        costs[r].held_bytes = table.records[r].resident_size;
    }
    return costs;
}

bool simulate_track_residency(
    const TrackStreamTable& table,
    const TrackZoneReport& zones,
    std::uint32_t track,
    const std::vector<TrackResidencyCost>& costs,
    const std::vector<TrackResidencySample>& samples,
    const TrackResidencyOptions& options,
    TrackResidencyRun& run,
    std::string& error) {
    run = TrackResidencyRun{};
    if (!table.valid) {
        error = "the streaming table is not valid";
        return false;
    }
    if (!zones.valid) {
        error = "the zone report is not valid";
        return false;
    }
    if (costs.size() != table.records.size()) {
        error = "costs hold " + std::to_string(costs.size()) +
                " entries for a table of " + std::to_string(table.records.size()) +
                " records";
        return false;
    }
    if (options.placements_per_pass == 0) {
        error = "placements per pass must be at least 1";
        return false;
    }
    for (std::size_t r = 0; r < costs.size(); ++r) {
        // A record held with more than it took to place would make the
        // release arithmetic below go negative on the next placement.
        if (costs[r].held_bytes > costs[r].place_bytes) {
            error = "record " + std::to_string(r) + " holds " +
                    std::to_string(costs[r].held_bytes) +
                    " bytes once loaded, more than the " +
                    std::to_string(costs[r].place_bytes) + " it takes to place";
            return false;
        }
    }

    Streamer streamer(table, zones, track, costs, options);
    streamer.force_rebuild();
    std::size_t index = 0;
    TrackResidencyStep step;
    while (!streamer.set_loaded()) {
        if (run.start_steps >= options.max_start_steps) {
            error = "the start set did not finish loading in " +
                    std::to_string(options.max_start_steps) + " steps";
            return false;
        }
        streamer.step(index, nullptr, step, run);
        record_step(step, run);
        ++index;
        ++run.start_steps;
    }
    run.start_loaded = true;

    streamer.place_car();
    for (std::size_t s = 0; s < samples.size(); ++s) {
        streamer.step(index, &samples[s], step, run);
        step.on_path = true;
        step.sample = s;
        record_step(step, run);
        ++index;
    }
    run.resident.assign(streamer.state().size(), 0);
    for (std::size_t r = 0; r < streamer.state().size(); ++r) {
        run.resident[r] = streamer.state()[r] == kLoaded ? 1U : 0U;
    }
    return true;
}

std::string track_residency_step_line(
    const TrackStreamTable& table, const TrackResidencyStep& step) {
    std::ostringstream line;
    line << "step " << step.index << (step.on_path ? " path" : " start")
         << " sample " << step.sample << " key " << step.sampled_key << " zone "
         << step.committed_key << " rebuild " << (step.rebuilt ? 1 : 0)
         << " placed " << (step.placed ? 1 : 0) << " loads "
         << numbers_of(table, step.loads) << " releases "
         << numbers_of(table, step.releases) << " unplaced "
         << step.unplaced_bytes << " over " << (step.over_budget ? 1 : 0)
         << " capped " << (step.release_capped ? 1 : 0) << " loaded "
         << (step.set_loaded ? 1 : 0) << " requested " << step.requested
         << " resident " << step.resident << " held " << step.held_bytes;
    return line.str();
}

Sha256Digest track_residency_sequence_sha256(
    const TrackStreamTable& table, const TrackResidencyRun& run) {
    Sha256 hash;
    for (const TrackResidencyStep& step : run.steps) {
        const std::string line = track_residency_step_line(table, step) + "\n";
        hash.update(line.data(), line.size());
    }
    return hash.finish();
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

namespace {

bool parse_field(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    // Decimal only: an optional sign, digits, an optional fraction and an
    // optional exponent. strtod alone would take "inf", "nan" and hex.
    std::size_t k = 0;
    if (text[k] == '+' || text[k] == '-') {
        ++k;
    }
    bool digits = false;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9') {
        ++k;
        digits = true;
    }
    if (k < text.size() && text[k] == '.') {
        ++k;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') {
            ++k;
            digits = true;
        }
    }
    if (!digits) {
        return false;
    }
    if (k < text.size() && (text[k] == 'e' || text[k] == 'E')) {
        ++k;
        if (k < text.size() && (text[k] == '+' || text[k] == '-')) {
            ++k;
        }
        bool exponent = false;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') {
            ++k;
            exponent = true;
        }
        if (!exponent) {
            return false;
        }
    }
    if (k != text.size()) {
        return false;
    }
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> value;
    // Finite: infinity minus itself is a NaN, and a NaN fails every
    // comparison.
    return !stream.fail() && value - value == 0.0;
}

} // namespace

bool parse_track_residency_path(
    const std::string& text,
    std::vector<TrackResidencyVertex>& vertices,
    std::string& error,
    const TrackResidencyPathLimits& limits) {
    vertices.clear();
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        ++line_number;
        start = end + 1;
        std::vector<std::string> fields;
        std::string current;
        for (const char c : line) {
            // A carriage return separates like a space, so CR LF endings
            // and a stray CR anywhere in a line read the same.
            if (c == ' ' || c == '\t' || c == '\r') {
                if (!current.empty()) {
                    fields.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            fields.push_back(current);
        }
        if (fields.empty() || fields[0][0] == '#') {
            if (end == text.size()) {
                break;
            }
            continue;
        }
        if (fields.size() != 3) {
            error = "path line " + std::to_string(line_number) + " has " +
                    std::to_string(fields.size()) +
                    " fields; a vertex is three numbers, slot 0, slot 1 and slot 2";
            return false;
        }
        TrackResidencyVertex vertex;
        for (std::size_t k = 0; k < 3; ++k) {
            if (!parse_field(fields[k], vertex.slot[k]) ||
                vertex.slot[k] > limits.max_coordinate ||
                vertex.slot[k] < -limits.max_coordinate) {
                error = "path line " + std::to_string(line_number) + " field " +
                        std::to_string(k + 1) + " is not a decimal number from " +
                        std::to_string(-limits.max_coordinate) + " to " +
                        std::to_string(limits.max_coordinate);
                return false;
            }
        }
        if (vertices.size() >= limits.max_vertices) {
            error = "the path has more than " + std::to_string(limits.max_vertices) +
                    " vertices";
            return false;
        }
        vertices.push_back(vertex);
        if (end == text.size()) {
            break;
        }
    }
    if (vertices.size() < 2) {
        error = "a path needs at least two vertices, and this one has " +
                std::to_string(vertices.size());
        return false;
    }
    if (!(track_residency_path_length(vertices) > 0.0)) {
        error = "the path has no horizontal length";
        return false;
    }
    return true;
}

double track_residency_path_length(
    const std::vector<TrackResidencyVertex>& vertices) noexcept {
    double length = 0.0;
    for (std::size_t k = 1; k < vertices.size(); ++k) {
        const double dx = vertices[k].slot[0] - vertices[k - 1].slot[0];
        const double dz = vertices[k].slot[1] - vertices[k - 1].slot[1];
        length += track_residency_sqrt(dx * dx + dz * dz);
    }
    return length;
}

bool sample_track_residency_path(
    const std::vector<TrackResidencyVertex>& vertices,
    double speed,
    std::uint32_t ticks_per_second,
    std::vector<TrackResidencySample>& samples,
    std::vector<TrackResidencyPathPoint>& points,
    std::string& error,
    const TrackResidencyPathLimits& limits) {
    samples.clear();
    points.clear();
    // Finite and positive: infinity minus itself is a NaN, and a NaN fails
    // every comparison.
    if (!(speed > 0.0) || !(speed - speed == 0.0)) {
        error = "the speed must be a positive number";
        return false;
    }
    if (ticks_per_second == 0) {
        error = "ticks per second must be at least 1";
        return false;
    }
    if (vertices.size() < 2) {
        error = "a path needs at least two vertices";
        return false;
    }
    // Segments of positive horizontal length, with their start distances.
    struct Segment {
        std::size_t from = 0;
        double start = 0.0;
        double length = 0.0;
        double direction[2] = {1.0, 0.0};
    };
    std::vector<Segment> segments;
    double total = 0.0;
    for (std::size_t k = 1; k < vertices.size(); ++k) {
        const double dx = vertices[k].slot[0] - vertices[k - 1].slot[0];
        const double dz = vertices[k].slot[1] - vertices[k - 1].slot[1];
        const double length = track_residency_sqrt(dx * dx + dz * dz);
        if (!(length > 0.0)) {
            continue;
        }
        Segment segment;
        segment.from = k - 1;
        segment.start = total;
        segment.length = length;
        segment.direction[0] = dx / length;
        segment.direction[1] = dz / length;
        segments.push_back(segment);
        total += length;
    }
    if (segments.empty()) {
        error = "the path has no horizontal length";
        return false;
    }
    const double step = speed / static_cast<double>(ticks_per_second);
    const double steps = total / step;
    // The ceiling of `steps` without a library call: it is below the sample
    // limit here, so the truncation fits and the comparison is exact.
    if (!(steps + 1.0 <= static_cast<double>(limits.max_samples))) {
        error = "the path needs more than " + std::to_string(limits.max_samples) +
                " samples at this speed";
        return false;
    }
    std::size_t whole = static_cast<std::size_t>(steps);
    if (static_cast<double>(whole) < steps) {
        ++whole;
    }
    if (!(static_cast<double>(whole) + 1.0 <=
          static_cast<double>(limits.max_samples))) {
        error = "the path needs more than " + std::to_string(limits.max_samples) +
                " samples at this speed";
        return false;
    }
    const std::size_t count = whole + 1;
    samples.reserve(count);
    points.reserve(count);
    std::size_t segment = 0;
    for (std::size_t k = 0; k < count; ++k) {
        const double distance =
            std::min(static_cast<double>(k) * step, total);
        while (segment + 1 < segments.size() &&
               distance >= segments[segment + 1].start) {
            ++segment;
        }
        const Segment& at = segments[segment];
        const double t = std::min(1.0, std::max(0.0, (distance - at.start) / at.length));
        const TrackResidencyVertex& a = vertices[at.from];
        const TrackResidencyVertex& b = vertices[at.from + 1];
        TrackResidencyPathPoint point;
        for (std::size_t j = 0; j < 3; ++j) {
            point.slot[j] = a.slot[j] + (b.slot[j] - a.slot[j]) * t;
        }
        point.direction[0] = at.direction[0];
        point.direction[1] = at.direction[1];
        point.distance = distance;
        TrackResidencySample sample;
        sample.position[0] = static_cast<float>(point.slot[0]);
        sample.position[1] = static_cast<float>(point.slot[1]);
        sample.velocity[0] = static_cast<float>(at.direction[0] * speed);
        sample.velocity[1] = static_cast<float>(at.direction[1] * speed);
        samples.push_back(sample);
        points.push_back(point);
    }
    return true;
}

// Drawn by hand over a top-down CPU render of the permanent set of L1RA/1003
// (docs/TRACK_STREAMING.md, "The hand-given path"). NOT the game's route.
const char* const kTrackResidencyHandPath1003 =
    "# HAND-GIVEN path around the circuit of track 1003, not the game's route.\n"
    "# Raw slot order: slot 0, slot 1, slot 2. It follows the streets through\n"
    "# the 0x00034153 route zones of 1003 in that record's order, starting in\n"
    "# zone 1003 and closing on its first vertex. Every height is the static\n"
    "# collision of TRACKB1003.lzc under the vertex, or, where that column\n"
    "# holds no polygon or meets a deck over the road, the collision heights\n"
    "# either side interpolated; vertices were added where the road dips.\n"
    "930 -212 59.18\n"
    "960 -240 59.18\n"
    "995 -268 59.18\n"
    "1025 -295 59.18\n"
    "1060 -318 59.16\n"
    "1080 -325.5 59.43\n"
    "1100 -333 58.66\n"
    "1150 -334 54.77\n"
    "1180 -334 51.79\n"
    "1190 -334 51.23\n"
    "1210 -334 51.61\n"
    "1230 -334 52.57\n"
    "1265 -334 56.17\n"
    "1300 -335 58.63\n"
    "1340 -315 58.57\n"
    "1370 -287 58.07\n"
    "1395 -262 57.60\n"
    "1422 -235 57.27\n"
    "1422 -198.2 57.42\n"
    "1422 -100 59.80\n"
    "1422 15 59.28\n"
    "1450 48 59.29\n"
    "1610 200 58.96\n"
    "1642 245 58.84\n"
    "1644.8 316.5 54.95\n"
    "1648 400 54.28\n"
    "1672.7 466.7 54.29\n"
    "1685 500 53.05\n"
    "1688 572 50.93\n"
    "1690 620 50.76\n"
    "1680 690 50.74\n"
    "1650 730 51.62\n"
    "1600 740 51.96\n"
    "1550 712 51.49\n"
    "1505 666 50.89\n"
    "1472.1 632.1 50.93\n"
    "1439.1 598.2 49.87\n"
    "1365 522 50.02\n"
    "1339 495.2 49.85\n"
    "1321.7 477.3 47.83\n"
    "1261 414.8 48.11\n"
    "1243.7 396.9 46.03\n"
    "1235 388 46.01\n"
    "1205 355 46.43\n"
    "1100 350 47.17\n"
    "1062.5 350 46.78\n"
    "1037.5 350 45.58\n"
    "987.5 350 40.56\n"
    "950 350 40.46\n"
    "937.5 350 41.09\n"
    "862.5 350 48.77\n"
    "800 350 49.14\n"
    "765 378 49.15\n"
    "745 390 49.15\n"
    "730 382 49.24\n"
    "727 360 49.16\n"
    "726.6 264 49.18\n"
    "726 120 54.86\n"
    "740 85 54.89\n"
    "800 30 53.09\n"
    "845 -20 53.27\n"
    "851 -60 53.27\n"
    "851 -100 53.27\n"
    "853 -130 54.90\n"
    "861 -152 56.71\n"
    "876 -170 58.05\n"
    "900 -187 58.94\n"
    "930 -212 59.18\n";

} // namespace nfsu
