#pragma once
namespace audio_plugin {
struct FactoryPreset {
  const char* category;
  const char* name;
  const char* xml;
};
// Index 0 is the fresh-instance default: keep Init FIRST and mirror
// createParameterLayout() defaults byte-for-byte, so a new instance ticks
// as "Init" and applying it from the menu is a true no-op.
inline const FactoryPreset kFactoryPresets[] = {
    {"Basic", "Init",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.1"/>
  <PARAM id="os_factor" value="1"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="1">
    <P x="0" y="0"/>
    <H x="0.5" y="0"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>
)TS"},
    {"Basic", "Gentle S-Curve",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.1"/>
  <PARAM id="os_factor" value="1"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="1">
    <P x="0" y="0"/>
    <H x="0.25" y="0"/>
    <P x="0.5" y="0.5"/>
    <H x="0.75" y="1"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>
)TS"},
    {"Destruction", "Explosive Squaring",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.23"/>
  <PARAM id="os_factor" value="3"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="0">
    <P x="0" y="0"/>
    <H x="0" y="0"/>
    <P x="0" y="1"/>
    <H x="0.5" y="1"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>
)TS"},
    {"Destruction", "Razor Gate",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.35"/>
  <PARAM id="os_factor" value="2"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="1">
    <P x="0" y="0"/>
    <H x="0.215" y="0"/>
    <P x="0.45" y="0"/>
    <H x="0.5" y="0.5"/>
    <P x="0.55" y="1"/>
    <H x="0.765" y="1"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>)TS"},
    {"Texture", "Folding Sand",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="1"/>
  <PARAM id="threshold" value="0"/>
  <PARAM id="os_factor" value="3"/>
  <PARAM id="smooth_mode" value="1"/>
  <WARP lock="0">
    <P x="0" y="0"/>
    <H x="0.125" y="1"/>
    <P x="0.25" y="1"/>
    <H x="0.375" y="1"/>
    <P x="0.5" y="0.5"/>
    <H x="0.625" y="0"/>
    <P x="0.75" y="0"/>
    <H x="0.875" y="0"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>)TS"},
    {"Contour", "Soft Knee",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.1"/>
  <PARAM id="os_factor" value="2"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="1">
    <P x="0" y="0"/>
    <H x="0.25" y="0"/>
    <P x="0.5" y="0.05"/>
    <H x="0.75" y="0.475"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>)TS"},
    {"Contour", "Transient Punch",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1"/>
  <PARAM id="smooth_mix" value="0"/>
  <PARAM id="threshold" value="0.3"/>
  <PARAM id="os_factor" value="2"/>
  <PARAM id="smooth_mode" value="0"/>
  <WARP lock="1">
    <P x="0" y="0"/>
    <H x="0.4" y="0.05"/>
    <P x="0.5" y="0.5"/>
    <H x="0.6" y="0.95"/>
    <P x="1" y="1"/>
  </WARP>
</Toposhaper>)TS"}};
inline constexpr int kNumFactoryPresets =
    static_cast<int>(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));
}  // namespace audio_plugin