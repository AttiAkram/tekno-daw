# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Layers, from the bottom up. Each one may only depend on the ones above it here.
#   core  engine and model, no GUI          (later: audio/, instruments/, plugins/, io/)
#   ui    JUCE components, theme, views
#   app   the executable: wiring and the main window

add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)
