#pragma once
#include <vector>

namespace audio_plugin {

class PluginProcessor;
struct ScopeMarker;

// Oscilloscope of the processed output with stereo support: single-channel
// inspection (L / R, with ghost + delta fill), L+R overlay and L|R split on
// shared time/vertical axes and a shared trigger. Passive S/M width chip
// indicates stereo divergence without ever changing the layout by itself;
// clicking it jumps to the overlay. View controls are UI-local (not APVTS).
class ScopeView : public juce::Component, private juce::Timer {
public:
  explicit ScopeView(PluginProcessor& p);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void refresh();
  int findTrigger(const float* s, size_t n) const;
  void buildPaths(int ch,
                  const juce::Rectangle<float>& r,
                  float scale,
                  bool withGhost);
  juce::Rectangle<float> traceRect() const;
  juce::Rectangle<float> laneRect(int ch) const;
  void drawDbGrid(juce::Graphics& g,
                  const juce::Rectangle<float>& r,
                  float scale) const;
  int primaryChannel() const;
  bool channelActive(int ch) const;
  int lenFromH(double t) const;
  double hFromLen(int len) const;
  void updateModeLabel();
  void updateChanLabel();
  void updateSmMetric();
  void updateSmChip();
  void mouseWheelMove(const juce::MouseEvent&,
                      const juce::MouseWheelDetails&) override;

  PluginProcessor& proc_;

  // per-channel display state
  std::vector<float> span_[2];    // processed at ring [spanStart_[c], +spanN_)
  std::vector<float> inSpan_[2];  // raw, single-channel modes only
  size_t spanN_[2] = {0, 0}, inN_[2] = {0, 0};
  uint64_t spanStart_[2] = {0, 0};
  juce::Path outPath_[2], inPath_[2], deltaPath_[2];
  juce::Path trail_[2][3];  // [ch][age], age 0 = newest (phosphor)
  int trailN_[2] = {0, 0};

  std::vector<ScopeMarker> marks_;
  size_t markN_ = 0;
  int viewLen_ = 2048;
  int viewStart_ = 0;  // shared view offset: common time axis for both ch
  float vZoomGain_ = 1.0f;
  bool locked_ = false;
  juce::String modeLabel_;

  // stereo view state (UI-local)
  int chanMode_ = 0;      // 0=L, 1=R, 2=L+R overlay, 3=L|R split
  int prim_ = 0;          // effective primary channel
  bool stereo_ = false;   // overlay/split actively drawing two channels
  bool hasR_ = false;     // R history available and aligned
  float smDb_ = -120.0f;  // side/mid energy ratio of the visible window
  juce::String smText_;
  uint32_t frame_ = 0;

  // Passive stereo-width indicator
  class SmChip : public juce::Component, public juce::TooltipClient {
  public:
    void setText(const juce::String& t);
    void setTint(juce::Colour c);
    void paint(juce::Graphics&) override;
    void setTooltip(const juce::String& newTooltip);
    juce::String getTooltip() override;

  private:
    juce::String text_{"S/M: MONO"};
    juce::Colour tint_{0xff5a6472};
    juce::String tooltipText;
  };
  SmChip smChip_;

  juce::ToggleButton trackBtn_{"TRACK: LOCK"};
  juce::TextButton chanBtn_{"CH: L"};
  juce::Slider vZoom_, hZoom_;
  juce::Rectangle<float> readoutRect_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

}  // namespace audio_plugin