#pragma once
#include "nfsu/career.h"

namespace nfsu {
struct CareerRideSetup;
enum class CareerMagazineStage { notice, list, photo };
inline bool career_magazine_page(CareerPage page)noexcept {
    return page==CareerPage::magazines||page==CareerPage::magazine_album;
}
std::uint32_t career_magazine_title_key(unsigned index)noexcept;
std::uint32_t career_magazine_description_key(unsigned index)noexcept;
// Transient presentation of durable magazine snapshots. The application uses
// the same input/selection state as the headless career regression.
class CareerMagazineMenu {
public:
    void reset(const CareerProfile&);
    bool active()const noexcept{return active_;}
    bool notice()const noexcept{return active_&&stage_==CareerMagazineStage::notice;}
    bool viewing()const noexcept{return active_&&stage_==CareerMagazineStage::photo;}
    bool album()const noexcept{return album_;}
    unsigned selected()const noexcept{return selected_;}
    unsigned first_visible()const noexcept{return first_visible_;}
    unsigned count()const noexcept{return unsigned(entries_.size());}
    unsigned cover_index()const{return entries_.at(selected_).cover;}
    unsigned cover_index(unsigned row)const{return entries_.at(row).cover;}
    unsigned magazine_index()const{return entries_.at(selected_).index;}
    unsigned magazine_index(unsigned row)const{return entries_.at(row).index;}
    bool earned(unsigned row)const{return entries_.at(row).cover!=~0U;}
    unsigned revision()const noexcept{return revision_;}
    bool move(int);
    bool move_vertical(int);
    bool accept(const CareerCatalog&,CareerProfile&,std::string& error);
    bool back(const CareerCatalog&,CareerProfile&,std::string& error);
    bool prepare_photo(const std::vector<unsigned char>& globals,const CareerCatalog&,const CareerProfile&,
                       CareerMagazineRecipe&,CareerRideSetup&,std::string& error)const;
private:
    struct Entry {unsigned index,cover;};
    std::vector<Entry> entries_;
    unsigned selected_=0,first_visible_=0,revision_=0;
    CareerMagazineStage stage_=CareerMagazineStage::notice;
    bool active_=false,album_=false;
};
}
