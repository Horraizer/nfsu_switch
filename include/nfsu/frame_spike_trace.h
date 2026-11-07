#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
namespace nfsu {
struct FrameSpikeRow {
    std::uint64_t frame=0,tick=0,debt=0;
    int node=-1,lap=-1;
    double x=0,y=0,z=0,fraction=0,gpu_ms=-1;
    unsigned steps=0,draws=0,awake=0,vertices=0;
    // Interval, work, sim, residency, record, update, present, acquire, fence,
    // uniforms, command recording, finish, submit, heap, pre-begin gap,
    // previous post-present tail, unaccounted work.
    std::array<double,17> times{};
    std::array<double,9> simulation{};
    std::array<double,7> render{};
};
// Bounded first-event capture: previous/current/next, no allocation or I/O.
// GPU results arrive later and join by display frame, never by simulation tick.
template<std::size_t Capacity=64> struct FrameSpikeTrace {
    struct Event {std::array<FrameSpikeRow,3> rows;};
    std::array<Event,Capacity> events{};
    FrameSpikeRow previous{};
    std::size_t count=0,dropped=0;
    void add(const FrameSpikeRow& row) {
        if(count && !events[count-1].rows[2].frame)events[count-1].rows[2]=row;
        if(row.times[0]>20.0 || row.times[1]>20.0) {
            if(count<Capacity)events[count++].rows={previous,row,{}};
            else ++dropped;
        }
        previous=row;
    }
    void gpu(std::uint64_t frame,double value) {
        if(!frame || !std::isfinite(value) || value<0)return;
        if(previous.frame==frame)previous.gpu_ms=value;
        for(std::size_t i=0;i<count;++i)for(auto& row:events[i].rows)
            if(row.frame==frame)row.gpu_ms=value;
    }
};
} // namespace nfsu
