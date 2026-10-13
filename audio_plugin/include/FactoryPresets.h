#pragma once
namespace audio_plugin {
struct FactoryPreset {
  const char* category;
  const char* name;
  const char* xml;
};
inline const FactoryPreset kFactoryPresets[1] = {
    {"Destruction", "DOLBIT NORMALNO",
     R"TS(<?xml version="1.0" encoding="UTF-8"?>
<Toposhaper>
  <PARAM id="drive" value="1.0"/>
  <PARAM id="smooth_mix" value="0.0"/>
  <PARAM id="threshold" value="0.23"/>
  <PARAM id="os_factor" value="3.0"/>
  <PARAM id="smooth_mode" value="0.0"/>
  <WARP lock="0">
    <P x="0.0" y="0.0"/>
    <H x="0.0" y="0.0"/>
    <P x="0.0" y="1.0"/>
    <H x="0.5" y="1.0"/>
    <P x="1.0" y="1.0"/>
  </WARP>
</Toposhaper>
)TS"},
};
inline constexpr int kNumFactoryPresets =
    static_cast<int>(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));
}  // namespace audio_plugin