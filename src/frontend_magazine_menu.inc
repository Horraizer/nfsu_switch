// Original Giveth and album FNGs; their label keys come from4C90C0.
void FrontendArt::Impl::draw_magazine_menu(Canvas canvas,const CareerProfile& profile,const CareerMagazineMenu& menu)const {
    if(!menu.active()||menu.viewing())throw std::runtime_error("magazine list presentation unavailable");
    const unsigned index=menu.magazine_index();unsigned owned=0;
    for(const auto& cover:profile.magazine_covers)owned|=1U<<cover.index;
    const bool earned=menu.earned(menu.selected());
    PostRaceValues values{
        {"MagazineCount",std::to_string(menu.count())},{"MagazineOwned",std::to_string(owned)},
        {"MagazineFirst",std::to_string(menu.first_visible())},
        {"NoticeVisible",menu.notice()?"1":"0"},{"Unranked","0"},
        {"LeftEnabled",menu.selected()&&(!menu.album()||menu.selected()%2)?"1":"0"},
        {"RightEnabled",menu.selected()+1<menu.count()&&(!menu.album()||menu.selected()%2==0)?"1":"0"},
        {"MagazineCover",magazine_hot},{"MagazineName",magazine_names.at(index)},
        {"MagazineBlurb",magazine_blurbs.at(index)},{"SelectUniqueBlurb",""},
        {"Mag_Title_DATA",magazine_names.at(index)},{"Mag_blurb",earned?magazine_blurbs.at(index):magazine_locked},
        {"MagazineScreen_Title","MAGAZINES"},{"HelpIcon",""},{"button_icon",""},{"button","OK"},
        {"VIEWMAGAZINEINSTRUCTION","View Magazine"},
        {"Controls",menu.notice()?"A: Continue   B: Skip rewards":menu.album()?
            "D-pad: Select   A: View   B: Underground menu":"Left / Right: Select   A: View   B: Skip rewards"}
    };
    for(unsigned row=0;row<8;++row) {
        const auto number=menu.first_visible()+row+1;
        values["Mag_Number_0"+std::to_string(row+1)]=number>27?"":std::string(number<10?"0":"")+std::to_string(number);
    }
    draw_dialog(canvas,menu.album()?7:6,values,menu.selected(),~0U);
}
void FrontendArt::draw_magazine_menu(ScreenImage image,const CareerProfile& profile,const CareerMagazineMenu& menu)const {
    impl_->draw_magazine_menu(Canvas{image},profile,menu);
}
void FrontendArt::record_magazine_menu(unsigned width,unsigned height,const CareerProfile& profile,
                                      const CareerMagazineMenu& menu,FrontendUiFrame& frame)const {
    frame.vertices.clear();frame.additive.clear();frame.background_quads=0;
    impl_->draw_magazine_menu(Canvas{width,height,impl_->atlas,frame},profile,menu);
}
