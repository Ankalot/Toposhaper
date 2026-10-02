#pragma once
// Central UI design tokens for Toposhaper.
// Header-only: inline variables give one instance per binary (unity-build
// safe). Semantic names keep widgets from re-inventing colours or sizes.
// Values are frozen from the original per-widget palettes; change a token
// here and every widget follows. Ring sizing formulas live in
// PluginProcessor.h next to kScopeBits / kMarkerBits.

namespace audio_plugin::ui {

// --- Surfaces ---------------------------------------------------------------
inline const juce::Colour panel{0xff14161c};   // inset widget panel (warp editor)
inline const juce::Colour scope{0xff101218};   // scope trace area
inline const juce::Colour frame{0x808a93a6};   // widget borders, 50% grey-blue

// --- Guides -----------------------------------------------------------------
inline const juce::Colour grid{0x14ffffff};    // 8% white grid lines
inline const juce::Colour zero{0x33ffffff};    // 20% white centre / "now" lines
inline const juce::Colour diag{0x26ffffff};    // 15% white identity diagonal

// --- Text -------------------------------------------------------------------
inline const juce::Colour text{0x99e8ecf5};    // 60% white: labels, rulers
inline const juce::Colour textHi{0xffe8ecf5};  // full white: point rims

// --- Accents (colorblind-safe blue / amber pair) ----------------------------
inline const juce::Colour accentL{0xff4f7dff}; // L trace, warp curve
inline const juce::Colour accentR{0xffffb454}; // R trace, S/M "wide" tint
inline const juce::Colour glowL{0x264f7dff};   // warp curve glow
inline const juce::Colour fillL{0x1f4f7dff};   // delta fill / under-curve fill
inline const juce::Colour fillR{0x1fffb454};   // R delta fill
inline const juce::Colour ghost{0x38e8ecf5};   // raw-input ghost trace

// --- Markers & status ---------------------------------------------------------
inline const juce::Colour markL{0xff5aa2ff};   // L kept-extremum dots
inline const juce::Colour markR{0xffffc078};   // R kept-extremum dots
inline const juce::Colour markFall{0xffe5484d};// fallback seam dots
inline const juce::Colour handle{0xff35d38a};  // warp on-curve handles
inline const juce::Colour endpoint{0xff9aa3b5};// fixed warp endpoints
inline const juce::Colour chipIdle{0xff5a6472};// S/M chip neutral tint

// --- Per-channel helpers -------------------------------------------------------
inline juce::Colour traceOf(int ch) noexcept { return ch == 0 ? accentL : accentR; }
inline juce::Colour markOf(int ch) noexcept { return ch == 0 ? markL : markR; }
inline juce::Colour fillOf(int ch) noexcept { return ch == 0 ? fillL : fillR; }

// --- Metrics (px) ----------------------------------------------------------------
inline constexpr int stripH = 24;         // scope control strip height
inline constexpr float corner = 6.0f;     // panel corner radius
inline constexpr float cornerSm = 4.0f;   // chip corner radius
inline constexpr float frameW = 1.5f;     // border stroke
inline constexpr float thinW = 1.6f;      // trace stroke / point rims
inline constexpr float ghostW = 1.2f;     // ghost stroke / hairlines
inline constexpr float curveW = 2.4f;     // warp curve stroke
inline constexpr float glowW = 6.0f;      // warp glow stroke
inline constexpr float hitR = 9.0f;       // point grab radius
inline constexpr float lineHitR = 6.0f;   // curve-line grab distance
inline constexpr float dotR = 3.5f;       // scope marker dot radius
inline constexpr float dotHoleR = 1.2f;   // inner "pupil" of marker dots
inline constexpr float gutterL = 58.0f;   // scope left label gutter
inline constexpr float chipSize = 12.0f;  // legend chip size
inline constexpr float chipStep = 26.0f;  // legend chip stride
inline constexpr float lineBoxH = 12.0f;  // ruler / dB label box height
inline constexpr float laneGap = 3.0f;    // half-gap between split lanes
inline constexpr float panelInset = 8.0f; // warp editor inner inset
inline constexpr float fontTiny = 9.0f;   // legend letters
inline constexpr float fontSmall = 10.0f; // rulers, dB labels
inline constexpr float fontChip = 11.0f;  // S/M chip text
inline constexpr float fontLabel = 15.0f; // knob titles

// --- Editor layout ----------------------------------------------------------------
inline constexpr int margin = 14;         // editor outer margin
inline constexpr int controlGap = 16;     // gap between knob columns
inline constexpr int labelH = 26;         // title label height
inline constexpr int stripMin = 150;      // top strip min height
inline constexpr int stripMax = 220;      // top strip max height
inline constexpr int knobTextW = 90;      // knob value box width
inline constexpr int knobTextH = 16;      // knob value box height
inline constexpr int stripGap = 6;        // gap between strip widgets

}  // namespace audio_plugin::ui