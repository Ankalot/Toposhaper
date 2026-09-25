#pragma once
#include <vector>

namespace audio_plugin {

class PluginProcessor;

// Oscilloscope-style view of the processed output: triggered (stable) or
// scrolling mode, vertical/horizontal zoom, min-max columns, 3-frame
// phosphor trail in scroll mode. View controls are UI-local on purpose
// (not APVTS): zooming a display must not be host-automatable.
class ScopeView : public juce::Component, private juce::Timer {
public:
  explicit ScopeView(PluginProcessor& p);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void refresh();           // read ring, pick view start, build trace path
  int findTrigger() const;  // last schmitt LOW->HIGH that fits the view, -1
  void buildPath(const juce::Rectangle<float>& r);
  juce::Rectangle<float> traceRect() const;
  int lenFromH(double t) const;  // slider 0..1000 -> 256..32768 samples
  double hFromLen(int len) const;

  PluginProcessor& proc_;

  // UI-local view state
  std::vector<float> span_;  // ring copy
  size_t spanN_ = 0;
  int viewLen_ = 2048;
  int viewStart_ = 0;
  float vZoomGain_ = 1.0f;
  bool locked_ = false;

  juce::Path trail_[3];  // [0] = newest; older drawn dimmer (phosphor)
  int trailN_ = 0;

  // control strip
  juce::ToggleButton trackBtn_{"TRACK"};
  juce::Slider vZoom_, hZoom_;
  juce::Rectangle<float> readoutRect_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

}  // namespace audio_plugin