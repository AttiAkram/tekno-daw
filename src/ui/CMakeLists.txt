# SPDX-License-Identifier: AGPL-3.0-or-later

add_library(tekno_ui STATIC
    theme/Contrast.cpp
    theme/Fonts.cpp
    theme/TeknoLookAndFeel.cpp
    components/IconButton.cpp
    components/ValueChip.cpp
    views/PatternView.cpp
    ProjectActions.cpp)
add_library(tekno::ui ALIAS tekno_ui)

target_link_libraries(tekno_ui
    PUBLIC  tekno_core
    PRIVATE tekno_fonts)
tekno_configure_target(tekno_ui)
