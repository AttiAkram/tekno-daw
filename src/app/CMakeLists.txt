# SPDX-License-Identifier: AGPL-3.0-or-later

juce_add_gui_app(tekno-daw
    PRODUCT_NAME "tekno-daw"
    COMPANY_NAME "tekno-daw contributors"
    BUNDLE_ID    "org.teknodaw.app"
    VERSION      "${PROJECT_VERSION}")

target_sources(tekno-daw PRIVATE
    AppOptions.cpp
    MainWindow.cpp
    Main.cpp)

target_link_libraries(tekno-daw PRIVATE tekno_ui)
tekno_configure_target(tekno-daw)

# cmake --build --preset dev --target run
add_custom_target(run
    COMMAND tekno-daw
    DEPENDS tekno-daw
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Starting tekno-daw")
