#pragma once

// Central UI design tokens for Toposhaper.
namespace audio_plugin::ui {
inline juce::Typeface::Ptr oswaldTf() {
  return juce::Typeface::createSystemTypefaceFor(
      BinaryData::OswaldMedium_ttf,
      static_cast<size_t>(BinaryData::OswaldMedium_ttfSize));
}

inline juce::Typeface::Ptr caveatTf() {
  return juce::Typeface::createSystemTypefaceFor(
      BinaryData::CaveatRegular_ttf,
      static_cast<size_t>(BinaryData::CaveatRegular_ttfSize));
}

// --- Oscilloscope ----------------------------------------------------------
inline const juce::Colour scope{0xff212121};  // scope trace area
inline const juce::Colour grid{0x14ffffff};   // 8% white grid lines
inline const juce::Colour zero{0x33ffffff};   // 20% white centre / "now" lines
inline const juce::Colour text{0x99e8ecf5};   // 60% white: labels, rulers

// --- Other -----------------------------------------------------------------
inline const juce::Colour chalk{0xffffffff};   // board chalk white
inline const juce::Colour black{0xff000000};   // black outline, text, etc
inline const juce::Colour paper{0xfff6eec9};   // paper (tooltip)
inline const juce::Colour lampOff{0xff808080}; // OS lamp grey
inline const juce::Colour flaskDrive{0xffb70000};     // DRIVE liquid body
inline const juce::Colour flaskDriveTop{0xff890000};  // DRIVE 2 px surface rim
inline const juce::Colour flaskThresh{0xff2424B7};    // THRESH liquid body
inline const juce::Colour flaskThreshTop{0xff1B1B89}; // THRESH 2 px surface rim
inline const juce::Colour flaskSmooth{0xff17A15F};    // SMOOTH liquid body
inline const juce::Colour flaskSmoothTop{0xff117947}; // SMOOTH 2 px surface rim

// --- Accents (colorblind-safe blue / amber pair) ---------------------------
inline const juce::Colour accentL{0xff4f7dff};  // L trace, warp curve
inline const juce::Colour accentR{0xffffb454};  // R trace, indicators
inline const juce::Colour fillL{0x1f4f7dff};    // delta fill / under-curve fill
inline const juce::Colour fillR{0x1fffb454};    // R delta fill
inline const juce::Colour ghost{0x38e8ecf5};    // raw-input ghost trace

// --- Markers & statuss------------------------------------------------------
inline const juce::Colour markL{0xff5aa2ff};     // L kept-extremum dots
inline const juce::Colour markR{0xffffc078};     // R kept-extremum dots
inline const juce::Colour markFall{0xffe5484d};  // fallback seam dots
inline const juce::Colour chipIdle{0xff5a6472};  // S/M chip neutral tint

// --- Per-channel helpers ---------------------------------------------------
inline juce::Colour traceOf(int ch) noexcept {
  return ch == 0 ? accentL : accentR;
}
inline juce::Colour markOf(int ch) noexcept {
  return ch == 0 ? markL : markR;
}
inline juce::Colour fillOf(int ch) noexcept {
  return ch == 0 ? fillL : fillR;
}

// --- Metrics (px) ----------------------------------------------------------
inline constexpr float cornerSm = 4.0f;    // chip corner radius
inline constexpr float thinW = 1.6f;       // trace stroke / point rims
inline constexpr float ghostW = 1.2f;      // ghost stroke / hairlines
inline constexpr float hitR = 9.0f;        // point grab radius
inline constexpr float lineHitR = 6.0f;    // curve-line grab distance
inline constexpr float dotR = 3.5f;        // scope marker dot radius
inline constexpr float dotHoleR = 1.2f;    // inner "pupil" of marker dots
inline constexpr float gutterL = 20.0f;    // scope left label gutter
inline constexpr float chipSize = 12.0f;   // legend chip size
inline constexpr float lineBoxH = 12.0f;   // ruler / dB label box height
inline constexpr float laneGap = 3.0f;     // half-gap between split lanes
inline constexpr float fontTiny = 9.0f;    // legend letters
inline constexpr float fontSmall = 10.0f;  // rulers, dB labels

}  // namespace audio_plugin::ui