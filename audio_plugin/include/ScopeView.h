#pragma once
#include <vector>

namespace audio_plugin {

class PluginProcessor;
struct ScopeMarker;

// Oscilloscope of the processed output: triggered (LOCK) or true-time scroll
// with phosphor trail, vertical/horizontal zoom, dB grid, sample/ms rulers,
// dim raw-input ghost and extrema/fallback markers. View controls are
// UI-local on purpose (not APVTS).
class ScopeView : public juce::Component, private juce::Timer {
public:
  explicit ScopeView(PluginProcessor& p);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void refresh();
  int findTrigger() const;  // newest schmitt LOW->HIGH whose view fits span
  void buildPaths(const juce::Rectangle<float>& r);
  juce::Rectangle<float> traceRect() const;
  int lenFromH(double t) const;
  double hFromLen(int len) const;
  void updateModeLabel();
  void mouseWheelMove(const juce::MouseEvent&,
                      const juce::MouseWheelDetails&) override;

  PluginProcessor& proc_;

  std::vector<float> span_;    // processed ch0 at ring [spanStart_, +spanN_)
  std::vector<float> inSpan_;  // raw ch0 at ring [spanStart_ - W, +inN_)
  size_t spanN_ = 0, inN_ = 0;
  uint64_t spanStart_ = 0;
  std::vector<ScopeMarker> marks_;
  size_t markN_ = 0;
  int viewLen_ = 2048;
  int viewStart_ = 0;  // offset into span_: view = [viewStart_, +viewLen_)
  float vZoomGain_ = 1.0f;
  bool locked_ = false;
  juce::String modeLabel_;

  juce::Path inPath_, outPath_;
  juce::Path trail_[3];  // [0] newest; older drawn dimmer (phosphor)
  int trailN_ = 0;

  juce::ToggleButton trackBtn_{"TRACK: LOCK"};
  juce::Slider vZoom_, hZoom_;
  juce::Rectangle<float> readoutRect_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

}  // namespace audio_plugin