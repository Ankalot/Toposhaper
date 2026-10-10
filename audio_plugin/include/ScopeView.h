#pragma once
#include <vector>
#include <functional>

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
  int findTriggerAligned(const float* s,
                         size_t n,
                         int center,
                         int radius) const;
  void storeTriggerRef(const float* s, int vs, size_t n);
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
  void pushUiState();   // widgets -> processor mirror (each refresh)
  void applyUiState();  // processor -> widgets (restore while open)
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
  // Corrscope-style frame-to-frame alignment: the previous frame's displayed
  // window is the reference; each new frame locks onto the gate crossing
  // whose window correlates best with it.
  std::vector<float> trigBuf_;  // previous displayed window (reference)
  float trigBufNorm_ = 0.0f;    // ||trigBuf_||^2 for normalized correlation
  int trigWidth_ = 0;           // reference length in samples
  int prevVs_ = -1;             // previous viewStart (in its span coords)
  uint64_t prevSpanStart_ = 0;  // spanStart_[prim_] of that previous frame
  int prevPrim_ = -1;           // channel the reference was taken from
  juce::String modeLabel_;

  // stereo view state (UI-local)
  int chanMode_ = 0;     // 0=L, 1=R, 2=L+R overlay, 3=L|R split
  int prim_ = 0;         // effective primary channel
  bool stereo_ = false;  // overlay/split actively drawing two channels
  bool hasR_ = false;    // R history available and aligned
  juce::String smText_;
  uint32_t uiSeq_ = 0;  // last seen UiState::seq

  // Passive stereo-width indicator
  class SmChip : public juce::Component, public juce::TooltipClient {
  public:
    void setLit(bool lit);
    void setTooltip(const juce::String& newTooltip);
    juce::String getTooltip() override;
    void paint(juce::Graphics&) override;

    // required side/mid amplitude ratio (scale-free "is it wide"),
    static constexpr float kWidthDb = -20.0f;
    // absolute side-peak floor (keeps near-silence grey).
    static constexpr float kFloorDbfs = -80.0f;
    static float dbToRatio(float db);

  private:
    bool lit_ = false;
    juce::String tooltipText;
  };
  SmChip smChip_;

  // A toggle that also flips on a vertical drag, like a real switch lever.
  class ToggleKnob : public juce::TextButton {
  public:
    using juce::TextButton::TextButton;
    void mouseDrag(const juce::MouseEvent& e) override {
      const int dy = e.getDistanceFromDragStartY();
      if (std::abs(dy) > 8) {
        dragged_ = true;
        setToggleState(dy > 0, juce::sendNotification);  // up = ON
      }
    }
    void mouseUp(const juce::MouseEvent& e) override {
      if (dragged_) {
        dragged_ = false;
        return;  // release after a drag must not double-flip
      }
      juce::TextButton::mouseUp(e);
    }

  private:
    bool dragged_ = false;
  };
  ToggleKnob trackBtn_{"TRACK: LOCK"};

  // Four-detent groove switch
  class ModeSwitch : public juce::TextButton {
  public:
    using juce::TextButton::TextButton;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    std::function<void(int)> onModeChange_;  // detent index 0..3
  private:
    void pick(const juce::MouseEvent& e);
    bool dragged_ = false;
  };
  ModeSwitch chanBtn_;

  juce::Slider vZoom_, hZoom_;

  juce::Image caseImg;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

}  // namespace audio_plugin