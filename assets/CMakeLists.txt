# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Fonts are embedded in the binary so the UI looks the same on every machine (ADR 0007).
# All of them are SIL OFL 1.1: the licence files sit next to the fonts and must stay there.

juce_add_binary_data(tekno_fonts
    HEADER_NAME "TeknoFonts.h"
    NAMESPACE tekno_fonts
    SOURCES
        fonts/inter/Inter-Regular.ttf
        fonts/inter/Inter-SemiBold.ttf
        fonts/inter/Inter-Bold.ttf
        fonts/barlow-condensed/BarlowCondensed-ExtraBold.ttf)
