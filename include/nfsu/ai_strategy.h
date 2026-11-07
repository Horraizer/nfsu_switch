#pragma once
#include "nfsu/ai_neighbors.h"

namespace nfsu {
// 44B56D..44B70F ordinary role2 controller gate. Slot is the actor's
// position in the world's rival range, not its actor ID or difficulty.
// The caller stores tick at4C0 only when full is selected. Driver/physics
// continue on retained publications between full controller updates.
bool ai_rival_full_update(std::uint32_t frame,std::uint32_t tick,
                          std::uint32_t previous_tick,unsigned slot,
                          unsigned count,bool have_action,bool& full) noexcept;
enum class AiStrategyKind {none=0,ordinary=1,drag=2,scripted_human=3,traffic=4,special=5};
// Strategy virtual1C: policies1/2/4/5 return true; policy3 returns false.
// Controller4BC can still force rebuilding. Policy3 is a scripted human,
// not the ordinary traffic policy4 selected by583230.
bool ai_strategy_rebuild_path(AiStrategyKind,bool controller_dirty) noexcept;
//452B40: collision alone does not admit action2. Ordinary traffic policy4's
// virtual0C is false; policy5's is true. Special world flags bypass this gate.
bool ai_strategy_traffic_admitted(bool target,bool actor_reference,int role,
                                  AiStrategyKind,bool force) noexcept;

// World metadata indexed in the SAME sorted order as AiNeighborhood. A race
// actor has original reference kind6 and a race row; role1 is the human.
struct AiStrategyActor {bool race_actor=false,human=false;float speed=0;};
// Ordered live contact list from the collision owner. Endpoints are stable
// world IDs resolved from original body kinds0 (actor) and4 (reference).
// Other kinds are preserved so unsupported pair types can be skipped exactly.
struct AiStrategyContact {
    float value=0; // contact+60, compared to the caller's minimum
    int first_kind=0,second_kind=0;
    std::uint32_t first=0,second=0;
};
// 44ED70, including pair resolution and human preference. Metadata uses the
// same sorted indices as neighbors; no nearest-distance re-sort is performed.
// Missing world entries are skipped as in57B4E0/57B550. ID0 means no target.
// Invalid consumed data refuses without replacing the caller's output.
bool ai_strategy_contact(const AiNeighborhood&,const std::array<AiStrategyActor,16>&,
                         const AiStrategyContact*,std::size_t,float minimum,
                         bool racers_only,std::uint32_t& target) noexcept;
struct AiBlockingMode {int priority=200;bool subtract_lengths=true;};
// Original77A340 rows addressed by configuration736.
bool ai_strategy_blocking_mode(unsigned,AiBlockingMode&) noexcept;
struct AiBlockingInput {
    bool race_time_hold=false,subtract_lengths=false;
    int mode_priority=0; // initialized77A340 row selected by configuration736
    std::array<AiStrategyActor,16> actors{};
};
struct AiActionCandidate {int kind=7,priority=0;std::uint32_t target=0;};
// Original450EC0 selects action5's approaching actor. It considers the first
// eligible entry behind self and does not search past a failed distance test.
bool ai_strategy_blocking(const AiRoadChain&,const AiNeighborhood&,
                          const AiBlockingInput&,AiActionCandidate&) noexcept;
// Original452630: action14 when fewer than two wheels contact and body-up
// projection is at most original6B76B0. Not the subsequent reset lifecycle.
bool ai_strategy_rollover(int contacts,const AiDriverPoint& up,bool& request) noexcept;
struct AiManeuverGate {std::uint32_t rejected_target=0,rejected_tick=0;};
// Original462740: expire the rejected-target cache, then draw once from the
// shared world RNG for a new target. Selected difficulty owns probability.
bool ai_strategy_allow_target(std::uint32_t target,std::uint32_t tick,
                              float cooldown,float probability,
                              AiManeuverGate&,std::uint32_t& rng,bool& allow) noexcept;
// Actor reaction44C2E0 and timer429100. This changes horn/headlamp state,
// never the physical steering input. The shared RNG is consumed even while a
// previous reaction prevents replacing the actor's timer.
struct AiReactionState {
    std::uint32_t tick=0;
    float first=0,gap=0,last=0;
    bool flash=false,horn=false,direction=false;
};
struct AiReactionInput {
    std::uint32_t tick=0;
    float relative=0;
    unsigned neighbors=0;
    int self_selector=3,self_role=2,target_role=2;
    bool target_disabled=false,racing=true,eligible=true;
};
bool ai_strategy_react(const AiReactionInput&,AiReactionState&,std::uint32_t& rng) noexcept;
struct AiReactionOutput {bool flash=false,horn=false;};
AiReactionOutput ai_reaction_output(const AiReactionState&,std::uint32_t tick,
                                   bool manual_flash=false,bool human=false) noexcept;

enum class AiStrategyQuery {recovery,obstacle,contact,traffic,passing,rollover,blocking,following,wall_contact};
struct AiStrategySelection {
    int current_kind=20,current_priority=-1,mode_priority=0;
    bool recovery_enabled=false;
};
// Query execution stays lazy and follows actual strategy1 virtual44D8B0,
// not the different44DD30 implementation. Query functions own geometric/path
// producers and their side effects. A missing producer must return false,
// never "no candidate". No fabricated avoidance decisions or heap closures.
using AiStrategyEvaluator=bool(*)(void*,AiStrategyQuery,bool& found,AiActionCandidate&);
bool ai_strategy_choose(const AiStrategySelection&,AiStrategyEvaluator,void*,AiActionCandidate&) noexcept;
//44DD30, policy2. Its wall/contact probes are not guarded by the ordinary
//strategy's kind/priority gates; the subsequent owner decides preemption.
bool ai_drag_strategy_choose(const AiStrategySelection&,AiStrategyEvaluator,void*,AiActionCandidate&) noexcept;
// Original44BC30 automatic replacement is strictly higher priority. Explicit
// action requests follow their own path and are not filtered by this helper.
bool ai_strategy_preempts(bool have_current,int priority,const AiActionCandidate&) noexcept;
} // namespace nfsu
