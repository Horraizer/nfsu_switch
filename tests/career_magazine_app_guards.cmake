# The portable controller regression must be wired to the actual Switch menu.
file(READ "${ROOT}/switch/race_main.cpp" magazine_app)
foreach(marker IN ITEMS
    "CareerMagazineMenu magazine_menu"
    "magazine_menu.reset(profile)"
    "magazine_menu.prepare_photo("
    "magazine_menu.viewing()"
    "if(!magazine_menu.viewing()){frontend_scene.focus_part(0);frontend_scene.road_target(20.F);}"
    "frontend_scene.adopt_garage(*retired_garage,frontend_error)"
    "retired_garage->load_owned_ride("
    "magazine_menu.accept("
    "magazine_menu.back("
    "magazine_menu.move("
    "magazine_menu.move_vertical("
    "career.enter_magazine_album("
    "art.record_magazine_menu(")
    string(FIND "${magazine_app}" "${marker}" found)
    if(found LESS 0)
        message(FATAL_ERROR "Magazine application wiring absent: ${marker}")
    endif()
endforeach()
foreach(legacy IN ITEMS "next.magazine_covers[selection].seen=true" "next.page=CareerPage::magazines")
    string(FIND "${magazine_app}" "${legacy}" found)
    if(NOT found LESS 0)
        message(FATAL_ERROR "Legacy immediate magazine viewer/dismissal bypass remains: ${legacy}")
    endif()
endforeach()
string(FIND "${magazine_app}" "career.pending_movie(profile)" movie)
string(FIND "${magazine_app}" "magazine_menu.reset(profile)" magazine)
if(movie LESS 0 OR NOT movie LESS magazine)
    message(FATAL_ERROR "Pending career movie must dispatch before magazine presentation")
endif()
message(STATUS "PASS real magazine application entry/input/photo/menu wiring")
