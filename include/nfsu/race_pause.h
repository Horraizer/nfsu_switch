#pragma once
#include <algorithm>
namespace nfsu {
struct RacePauseInput {bool up=false,down=false,left=false,right=false,accept=false,back=false;};
enum class RacePauseAction {none,resume,restart,leave};
enum class RacePausePage {menu,restart,leave,options};
//4CD7B0 callbacks: resume, confirmed restart, options, confirmed leave.
class RacePauseMenu {
public:
    explicit RacePauseMenu(bool practice=false,bool tournament=false):practice_(practice),tournament_(tournament){}
    bool practice()const noexcept{return practice_;}
    bool tournament()const noexcept{return tournament_;}
    void open()noexcept {page_=RacePausePage::menu;selected_=0;confirm_=false;++revision_;}
    RacePauseAction update(RacePauseInput input)noexcept {
        if(page_==RacePausePage::restart||page_==RacePausePage::leave) {
            if(input.back){page_=RacePausePage::menu;return RacePauseAction::none;}
            if(input.left!=input.right||input.up!=input.down)confirm_=!confirm_;
            if(input.accept) {
                const auto action=page_==RacePausePage::restart?RacePauseAction::restart:RacePauseAction::leave;
                page_=RacePausePage::menu;return confirm_?action:RacePauseAction::none;
            }
            return RacePauseAction::none;
        }
        if(input.back) {
            if(page_==RacePausePage::options){page_=RacePausePage::menu;return RacePauseAction::none;}
            return RacePauseAction::resume;
        }
        const unsigned count=page_==RacePausePage::menu?4:3;
        auto& selected=page_==RacePausePage::menu?selected_:option_;
        if(input.up!=input.down)selected=(selected+(input.up?count-1:1))%count;
        if(page_==RacePausePage::options) {
            if(input.left!=input.right) {
                if(option_==2)hud_=!hud_;
                else {auto& value=option_==0?music_:effects_;value=std::clamp(value+(input.right?10:-10),0,100);}
            }
            return RacePauseAction::none;
        }
        if(input.accept) {
            if(selected_==0)return RacePauseAction::resume;
            confirm_=false;
            page_=selected_==1?RacePausePage::restart:selected_==2?RacePausePage::options:RacePausePage::leave;
        }
        return RacePauseAction::none;
    }
    RacePausePage page()const noexcept{return page_;}
    unsigned selected()const noexcept{return selected_;}
    unsigned revision()const noexcept{return revision_;}
    unsigned option()const noexcept{return option_;}
    bool confirmed()const noexcept{return confirm_;}
    unsigned music()const noexcept{return unsigned(music_);}
    unsigned effects()const noexcept{return unsigned(effects_);}
    bool hud()const noexcept{return hud_;}
private:
    RacePausePage page_=RacePausePage::menu;
    unsigned selected_=0,option_=0,revision_=0;
    bool confirm_=false,hud_=true,practice_=false,tournament_=false;
    int music_=100,effects_=100;
};
}
