#pragma once
#include "nfsu/career.h"
#include "nfsu/career_ride.h"
namespace nfsu {
// Original512294 first matching sponsor alias, or zero when absent.
std::uint32_t career_decal_brand_icon(std::uint32_t brand) noexcept;
std::uint32_t career_vinyl_icon(unsigned type) noexcept;
std::vector<std::uint32_t> career_decal_menu_icons();
enum class GaragePage {root,visual,body,accessories,performance,packages,parts,rims,rim_styles,rim_sizes,
    paint,colors,vinyl_layers,vinyl_groups,vinyl_types,decals_zones,decals_sizes,decals_slots,decals_brands,unique_performance,unique_parts,trade_in};
enum class GarageDialog {none,vinyl_layer,remove_vinyls,remove_unique,stock_hood,already_installed};
struct GarageEntry {
    std::string name;
    std::uint32_t icon=0,part=0,price=0;
    unsigned value=0,upgrade_level=~0U,rim_diameter=0;
    int trade_cost=0;
    bool available=true,owned=false,installed=false,newly_unlocked=false;
};
// Ephemeral menu/preview state. Only a confirmed transaction mutates the
// durable profile. Returning through categories restores the installed ride.
class CareerGarage {
public:
    void reset(const CareerCatalog&,const CareerProfile&,bool appearance_only=false);
    void reset_unique(const CareerCatalog&,const CareerProfile&);
    void move(int direction);
    void move_color(int x,int y);
    bool edit_vinyl_colors(const CareerCatalog&,const CareerProfile&,std::string& error);
    bool reorder_vinyl(int,const CareerCatalog&,CareerProfile&);
    void toggle_decal_color(const CareerCatalog&,const CareerProfile&);
    bool white_decal()const{return state_.white_decal;}
    unsigned color_channel()const{return state_.channel;}
    unsigned color_channels()const{return state_.channels;}
    void change_finish(int direction,const CareerCatalog&,const CareerProfile&);
    unsigned color_family()const{return state_.group;}
    bool back(const CareerCatalog&,const CareerProfile&); // true at root exit
    bool accept(const CareerCatalog&,CareerProfile&,bool& purchased,std::string& error);
    void refresh(const CareerCatalog&,const CareerProfile&);
    GaragePage page()const{return state_.page;}
    std::size_t fng()const;
    const std::string& title()const{return title_;}
    const std::vector<GarageEntry>& entries()const{return entries_;}
    unsigned slot()const{return state_.slot;}
    unsigned performance_field()const{return state_.field;}
    unsigned focus_slot()const;
    float road_target()const;
    std::string preview_signature(const CareerProfile&)const;
    static std::string installed_signature(const CareerProfile&);
    bool prepare_previews(const std::vector<unsigned char>& globals,const CareerCatalog&,const CareerProfile&,
                          std::vector<CareerRideSetup>& variants,std::vector<unsigned>& indices,std::string& error)const;
    unsigned selected()const{return state_.selection;}
    unsigned preview_index()const{return state_.page==GaragePage::colors?state_.preview:state_.selection;}
    bool confirming()const{return confirm_;}
    GarageDialog dialog()const{return dialog_;}
    unsigned dialog_selected()const{return dialog_selection_;}
    bool unique_intro()const{return unique_intro_;}
    bool previewing()const;
    CareerProfile preview(const CareerProfile&)const;
    CareerProfile preview(const CareerProfile&,unsigned index)const;
private:
    struct State {GaragePage page=GaragePage::root;unsigned selection=0,slot=0,group=0,field=0,layout=0,channel=0,channels=1,preview=0;bool white_decal=false;};
    State state_;
    std::vector<CareerProfile> trade_previews_;
    std::array<std::uint32_t,4> draft_colors_{};
    // Original4DAFA0 keeps a cursor for every finish/channel pair.
    // These are palette cells, independent of locked/unpopulated entries.
    std::array<unsigned,12> color_cells_{};
    std::vector<State> parents_;
    std::vector<GarageEntry> entries_;
    std::string title_;
    bool confirm_=false,unique_intro_=false,appearance_only_=false;
    GarageDialog dialog_=GarageDialog::none;
    unsigned dialog_selection_=0;
    void open_page(State,const CareerCatalog&,const CareerProfile&);
    void build(const CareerCatalog&,const CareerProfile&);
    void remember_color_cursor();
    void restore_color_cursor();
    unsigned color_cursor_index()const;
};
}
