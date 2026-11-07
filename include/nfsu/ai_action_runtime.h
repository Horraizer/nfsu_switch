#pragma once
#include "nfsu/ai_action.h"
#include "nfsu/ai_recovery.h"

namespace nfsu {
// Constructor operands from the selected original factory/strategy branch.
// Selection remains a live producer; no absent query is treated as no rival.
struct AiActionCreation {
    AiActionCandidate candidate;
    float duration=-1;
    bool present=true;
};
using AiActionSelection=bool(*)(void*,int,const AiOwnedAction&,AiActionCreation&);
struct AiActionRuntimeInput {
    std::uint32_t tick=0;
    const AiNeighborhood* neighbors=nullptr;
    const std::array<AiStrategyActor,16>* actors=nullptr;
    const AiStrategyContact* contacts=nullptr;
    std::size_t contact_count=0;
    // Each context belongs to this tick. Fallback's context includes the
    // original ride-category reset, when required by450480.
    AiActionPathInput path,fallback_path;
    AiContactActionInput contact;
    const RoutePoint* selected_road=nullptr;
    AiRecoveryMotion recovery_motion;
    bool recovery_target_override=false;
    const AiActionNeighborState* motion=nullptr;
    std::size_t motion_count=0;
    float fallback_gap=0,fallback_desired_gap=0;
    AiActionSelection select=nullptr;
    void* selection_context=nullptr;
};
struct AiActionRuntimeOutput {
    AiOwnedAction action;
    std::uint32_t created_tick=0;
    float duration=-1;
    int category=0;
    AiPlannedPath path;
    AiActionSpeedState speed;
    bool clear_actor_recovery=false; //450160 cleanup publication
    unsigned constructions=0,path_updates=0;
};
// Fixed two-slot storage: the original owner holds current+candidate before
// preemption. Implements real4/12 constructors, predicates, cleanup and path/
// speed updates using the recovered owner scheduler. Other action kinds are
// refused explicitly until their update is integrated. Not a whole strategy.
class AiActionRuntime {
public:
    bool initialize(const AiActionCreation&,std::uint32_t tick) noexcept;
    bool step(const AiActionRuntimeInput&,AiActionRuntimeOutput&) noexcept;
    bool request(int kind) noexcept;
    const AiOwnedAction& current() const noexcept {return owner_.current;}
    bool failed() const noexcept {return failed_;}
private:
    struct Record {
        AiOwnedAction action;
        AiContactActionState state;
        std::uint32_t epoch=0;
        float duration=-1;
    };
    std::array<Record,2> records_{};
    AiActionOwner owner_;
    AiContactManeuver maneuver_;
    std::uint32_t next_handle_=1;
    bool failed_=false,clear_recovery_=false;
    unsigned constructions_=0,path_updates_=0;
    int category_=0;
    const AiActionRuntimeInput* input_=nullptr;
    Record* find(std::uint32_t) noexcept;
    bool construct(const AiActionCreation&,std::uint32_t,AiOwnedAction&) noexcept;
    static bool predicate(void*,const AiOwnedAction&,AiActionPredicate,bool&);
    static bool create(void*,int,const AiOwnedAction&,AiOwnedAction&);
    static bool cleanup(void*,const AiOwnedAction&);
    static void release(void*,const AiOwnedAction&);
    static bool update(void*,const AiOwnedAction&,bool&);
    static bool contact_path(void*,const AiContactManeuver&,AiPlannedPath&);
    static bool road_class(void*,int&);
};
} // namespace nfsu
