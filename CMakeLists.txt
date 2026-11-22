# GrooveDeck FM-1 - PC-Werkzeuge (Windows/Linux/macOS): Tests, WAV-Renderer, Bedien-Simulation.
# Die eigentliche Firmware wird mit target/build.py und der JieLi-Toolchain gebaut (siehe target/README.md).
#
#   cmake -B build
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
cmake_minimum_required(VERSION 3.16)
project(GrooveDeckFM1 C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(GD_CORE
    core/gd_dsp.c core/gd_arp.c core/gd_seq.c core/gd_preset.c core/gd_presets_data.c core/gd_presets_mini.c
    core/gd_app.c core/gd_ui.c core/gd_ui_params.c core/gd_font.c core/gd_input.c core/gd_leds.c core/gd_fmt.c)

add_library(gdcore STATIC ${GD_CORE})
target_include_directories(gdcore PUBLIC core)
if(MSVC)
    target_compile_definitions(gdcore PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gdcore PRIVATE /W3)
else()
    target_compile_options(gdcore PRIVATE -Wall -Wextra -pedantic)
    target_link_libraries(gdcore PUBLIC m)
endif()

add_executable(gd_render host/gd_render.c)
target_link_libraries(gd_render PRIVATE gdcore)
add_executable(gd_sim host/gd_sim.c)
target_link_libraries(gd_sim PRIVATE gdcore)
# Bedienfeld am PC (braucht SDL2: sudo apt install libsdl2-dev)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(gd_panel host/gd_panel.c host/gd_panel_draw.c)
    if(TARGET SDL2::SDL2)
        target_link_libraries(gd_panel PRIVATE gdcore SDL2::SDL2)
    else()
        target_include_directories(gd_panel PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(gd_panel PRIVATE gdcore ${SDL2_LIBRARIES})
    endif()
else()
    message(STATUS "SDL2 nicht gefunden: gd_panel (Bedienfeld am PC) wird nicht gebaut - sudo apt install libsdl2-dev")
endif()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE gdcore)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME alle_presets COMMAND gd_render --all 2)

# Klangvergleich mit der Original-Engine des Plugins (optional):
#   cmake -B build -DGROOVEDECK_SOURCE=C:/pfad/zu/GrooveDeck/Source
if(GROOVEDECK_SOURCE)
    add_executable(compare_ref tests/compare_ref.cpp)
    target_include_directories(compare_ref PRIVATE ${GROOVEDECK_SOURCE})
    set_target_properties(compare_ref PROPERTIES CXX_STANDARD 17)
    target_link_libraries(compare_ref PRIVATE gdcore)
    add_test(NAME vergleich_plugin COMMAND compare_ref 0 439 0)
endif()
