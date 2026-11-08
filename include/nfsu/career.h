#pragma once
#include "nfsu/race_events.h"
#include "nfsu/career_tuning.h"
#include "nfsu/career_magazine.h"
#include "nfsu/career_info.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {
constexpr unsigned kCareerLastEvent=113;
// Port-owned save schema; never reads or overwrites original PC profiles.
enum class CareerPage : unsigned {
    intro, intro_short, choose_car, intro_long, map, menu, customize_movie, garage, briefing, unique_reward, magazines
};
struct CareerRide {
    std::array<std::uint32_t,120> installed{};
    // Original ownership grid: category, then a bounded list of36 keys.
    std::array<std::array<std::uint32_t,36>,25> owned{};
    //5A0350 fields0..8; aero fields follow the installed visual parts.
    std::array<unsigned,9> performance{};
    std::array<bool,9> unique{}; // Original separate item flags, never package level4.
};
struct CareerTournament {
    unsigned event=0,round=0;
    //Configuration order: human, then the three authored opponents.
    std::array<std::array<unsigned,4>,kRaceEventRaceSlots> places{};
    std::array<unsigned,4> totals()const noexcept;
    std::array<unsigned,4> order()const noexcept;
};
struct CareerMagazine {
    unsigned index=0;std::uint32_t seed=0;bool seen=false;
    std::string car;
    std::array<std::uint32_t,120> installed{};
    std::array<unsigned,9> performance{};
    std::array<bool,9> unique{};
};
struct CareerProfile {
    // Display identity is supplied by the selected profile directory.
    std::string name;
    std::uint32_t races_won=0,races_lost=0,restarts=0;
    CareerStatistics statistics;
    CareerPage page=CareerPage::intro;
    unsigned difficulty=1, selected_event=1;
    std::uint32_t bank=0,style_points=0,style_high_water=0;
    CareerRide ride;
    std::vector<CareerMagazine> magazine_covers;
    std::uint32_t magazine_random=0,magazine_pending=0;
    std::string car;
    // Transient Quick Race identity, never serialized into an Underground save.
    std::string bonus_preset;
    bool customization_seen=false;
    CareerTournament tournament;
    unsigned pending_unique=0;
    // Durable original event movie request; zero phase means no request.
    unsigned movie_event=0,movie_phase=0; //1 before briefing,2 after victory.
    std::array<std::uint8_t,256> status{};
    // Most recent won event's frontend reward context, separate from ownership.
    unsigned new_parts_event=0;
};
struct CareerCar { std::string name,brand;std::uint32_t cost=0; std::array<float,3> performance{}; };
struct CareerBonusCar {
    std::string preset,car,label;
    std::uint32_t points=0;
    InstalledPartSelection parts;
    std::array<unsigned,9> performance{};
};
struct CareerEventMedia {
    std::string before,after,sender;
    std::uint32_t portrait=0; // original event130 texture hash; zero hides sender
    int reward_speech=-1; //4C6C00/4C76D0 separate reward-screen requests.
    int speech=-1; // first original6FA5D0 event/English speech-stream request
    std::array<std::uint32_t,3> reward{};
    std::vector<std::string> unlock_messages;
    std::array<unsigned char,25> new_part_levels{};
    std::vector<unsigned> unique_categories;
    std::vector<std::uint32_t> magazine_rewards;
    std::array<float,3> deadlines{};
};
// Free practice reuses authored routes. It never creates career results.
struct FreeDriveCourse {
    unsigned event=1,round=0;
    int track=1003,direction=0;
    RaceType type=RaceType::circuit;
    std::string name;
    std::uint32_t mode_mask=0;
};
enum class QuickRaceRow { track, car, customize, performance, unique, traffic,
    difficulty, opponents, laps, catch_up, start };
std::vector<QuickRaceRow> quick_race_rows(QuickRaceMode,RaceType backing,bool bonus=false);
const char* quick_race_mode_name(QuickRaceMode) noexcept;
class CareerCatalog {
public:
    bool load(const std::filesystem::path& retail,std::string& error);
    bool load_free_drive(const std::filesystem::path&,std::string& error);
    bool free_drive()const{return free_drive_;}
    const std::vector<FreeDriveCourse>& free_courses()const{return free_courses_;}
    std::vector<unsigned> quick_courses(QuickRaceMode)const;
    bool free_profile(const std::string& car,CareerProfile&,std::string& error)const;
    const std::vector<CareerBonusCar>& bonus_cars()const{return bonus_cars_;}
    const CareerBonusCar* bonus_car(const std::string& preset)const;
    std::vector<std::string> quick_cars(std::uint32_t earned_high_water)const;
    bool quick_profile(const std::string& identity,std::uint32_t earned_high_water,
                       CareerProfile&,std::string& error)const;
    std::string quick_car_label(const CareerProfile&)const;
    const RaceEventTable& events()const{return events_;}
    CareerInfoValues info_values(const CareerProfile&,const CareerInfoMenu&,bool metric=false)const;
    std::uint32_t statistics_track_mask(unsigned event,unsigned round)const;
    const CareerTuningCatalog& tuning()const{return tuning_;}
    const CareerMagazineCatalog& magazines()const{return magazines_;}
    const std::vector<CareerCar>& starters()const{return starters_;}
    const std::vector<CareerCar>& cars()const{return cars_;}
    bool car_available(const CareerProfile&,const std::string&)const;
    bool prepare_trade(const CareerProfile&,const std::string&,CareerProfile&,int& cost,std::string& error)const;
    bool trade(CareerProfile&,const std::string&,std::string& error)const;
    bool performance_ratings(const CareerProfile&,std::array<float,3>&)const;
    const CareerEventMedia* media(unsigned event)const;
    std::vector<unsigned> available(const CareerProfile&)const;
    // Keep a completed or unavailable race out of the active map selection.
    bool select_available_event(CareerProfile&)const;
    std::vector<std::string> style_messages(std::uint32_t before,std::uint32_t after)const;
    bool valid(const CareerProfile&,std::string& error)const;
    bool intro_won(CareerProfile&,std::string& error)const;
    bool begin_event(CareerProfile&,std::string& error)const;
    bool abandon_event(CareerProfile&,std::string& error)const;
    bool tournament_round_finished(CareerProfile&,const std::array<unsigned,4>& places,std::string& error)const;
    bool event_finished(CareerProfile&,unsigned event,unsigned place,std::string& error)const;
    bool award_unique(CareerProfile&,unsigned category,std::uint32_t part_hash,std::string& error)const;
    bool buy(CareerProfile&,std::size_t car,std::string& error)const;
    bool refresh_magazines(CareerProfile&,bool capture,std::string& error)const;
    bool dismiss_magazines(CareerProfile&,std::string& error)const;
    bool owns(const CareerProfile&,unsigned category,std::uint32_t key)const noexcept;
    //A purchased paint job permits later unlocked color changes without
    //buying every palette swatch. Access gates remain separate from cost.
    std::uint32_t part_price(const CareerProfile&,const CarPartRecord&)const noexcept;
    bool buy_part(CareerProfile&,unsigned target_slot,std::uint32_t part_hash,std::string& error)const;
    bool buy_performance(CareerProfile&,unsigned field,unsigned level,std::string& error)const;
    // Race-confirm movies are withheld until difficulty is accepted.
    std::string pending_movie(const CareerProfile&,bool race_confirmed=false)const;
    bool movie_finished(CareerProfile&,std::string& error)const;
    bool enter_customization(CareerProfile&,std::string& error)const;
private:
    bool free_drive_=false;
    std::vector<FreeDriveCourse> free_courses_;
    RaceEventTable events_;
    std::array<std::string,64> ranking_names_{};
    std::array<std::array<unsigned char,40>,113> ranking_rosters_{};
    std::array<std::array<int,2>,256> ranking_event_{};
    std::map<unsigned,std::string> info_labels_;
    std::map<int,std::uint32_t> statistics_track_masks_;
    CareerTuningCatalog tuning_;
    CareerMagazineCatalog magazines_;
    std::array<CareerEventMedia,256> media_{};
    std::vector<CareerCar> starters_,cars_;
    std::vector<CareerBonusCar> bonus_cars_;
    std::vector<std::pair<std::string,std::vector<unsigned>>> car_unlocks_;
    std::vector<std::pair<std::string,std::vector<unsigned char>>> rating_records_;
    std::array<float,5> rating_low_{},rating_high_{};
    std::vector<std::string> style_messages_;
};
// Alternating checksummed slots preserve the previous complete save if power
// is lost while writing the other. Missing slots mean a new profile; damaged
// existing slots never silently erase a career. Base must be outside retail.
bool load_career(const std::filesystem::path& base,const CareerCatalog&,
                 CareerProfile&,std::string& error);
bool save_career(const std::filesystem::path& base,const CareerCatalog&,
                 const CareerProfile&,std::string& error);
bool apply_career_race_result(const CareerCatalog&,CareerProfile&,unsigned place,
    const std::array<unsigned,4>& places,int style,std::string& error);
}
