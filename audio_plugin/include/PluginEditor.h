#pragma once

namespace audio_plugin {
class CustomLookAndFeel : public juce::LookAndFeel_V4 {
public:
  CustomLookAndFeel() {
    caveatFace = ui::caveatTf();
    oswaldFace = ui::oswaldTf();
  }

  ~CustomLookAndFeel() override {
    caveatFace = nullptr;
    oswaldFace = nullptr;
  }

  juce::Font oswald(float h) const {
    return juce::Font(juce::FontOptions(oswaldFace).withHeight(h));
  }

  juce::Font oswaldPt(float pts) const {
    return juce::Font(juce::FontOptions(oswaldFace).withPointHeight(pts));
  }

  juce::Font caveat(float h) const {
    return juce::Font(juce::FontOptions(caveatFace).withHeight(h));
  }

  juce::Font caveatPt(float pts) const {
    return juce::Font(juce::FontOptions(caveatFace).withPointHeight(pts));
  }

private:
  juce::Typeface::Ptr caveatFace;
  juce::Typeface::Ptr oswaldFace;
};

// Fixed speech bubble next to the cat: the plugin's only tooltip surface.
// Never follows the mouse, never intercepts clicks, grows downward to fit.
class SpeechBubble : public juce::Component {
public:
  void setText(const juce::String& t);
  void setFont(const juce::Font& f);
  void paint(juce::Graphics& g) override;
  const juce::String getText();

private:
  juce::String text_;
  juce::TextLayout layout_;
  bool layoutDirty_ = true;
  juce::Font font_{juce::FontOptions{}};
};

// Three-way radio switch bank over the printed OVER SAMPLING panel: the
// art prints slots, labels and stripes; lamps and handle sprites live
// here. Exactly one switch is engaged and the engaged one cannot be
// turned off (something must stay engaged).
class OsPanel : public juce::Component, private juce::Timer {
public:
  explicit OsPanel(PluginProcessor& p);
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void engage(int idx);
  void sync();
  void timerCallback() override;
  PluginProcessor& proc_;
  juce::TextButton btns_[3];
  juce::Image handleImg_;
  int active_ = 0;
};

// Vertical level knob: liquid clipped to the printed glass
// outline, darker 2 px surface rim, glass sprite on top. Drag up/down or
// scroll to pour.
class Flask : public juce::Slider {
public:
  struct Config {
    juce::Colour liquid;  // body of the liquid
    juce::Colour rim;     // 2 px surface line
    juce::Image glass;    // opaque backdrop + transparent hole
    std::vector<std::array<float, 2>> points;  // silhouette in editor coords
    float originX = 0.0f;                      // local origin of the sprite
    float originY = 0.0f;
    float levelY0 = 0.0f;  // editor y of value 0.0 (bottom)
    float levelY1 = 0.0f;  // editor y of value 1.0 (top)
    juce::String tooltip;
  };

  explicit Flask(const Config& cfg);
  void paint(juce::Graphics& g) override;
  bool hitTest(int x, int y) override;

private:
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void pour(int yLocal);

  juce::Path outline_;  // glass silhouette in local coords (hit area)
  juce::Image glass_;
  juce::Colour liquid_;
  juce::Colour rim_;
  float localBottom_ = 0.0f;     // local y of value 0.0
  float travel_ = 0.0f;          // px of liquid travel
  juce::String baseTooltip_;     // static text from Config
  void valueChanged() override;  // rebuild tooltip with current %
};

// Paper tag tied to the SMOOTH flask neck: two-option mode switch
// (SG / PAVA). The sprite prints tag, strings and labels; this component
// owns two invisible hit zones with pointing-hand cursors and draws the
// squashed-ellipse outline around the engaged option.
class SmoothTag : public juce::Component, private juce::Timer {
public:
  explicit SmoothTag(PluginProcessor& p);
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void engage(int mode);  // 0 = SG, 1 = PAVA
  void sync();
  void timerCallback() override;
  PluginProcessor& proc_;
  juce::Image tagImg_;
  juce::TextButton sgBtn_, pavaBtn_;  // invisible hit zones
  int mode_ = 0;
};

// Small round invisible button: circular hit area,
// pointing-hand cursor, click callback. Deliberately tooltip-less: the
// cat speaks through PluginEditor::say(), not through the hover pipeline.
class RoundBtn : public juce::Component {
public:
  RoundBtn() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
  bool hitTest(int x, int y) override {
    const auto r = getLocalBounds().toFloat();
    return r.getCentre().getDistanceFrom({(float)x, (float)y}) <=
           r.getWidth() * 0.5f;
  }
  void mouseUp(const juce::MouseEvent& e) override {
    if (onClick && getLocalBounds().contains(e.getPosition()))
      onClick();
  }
  std::function<void()> onClick;
};

// Chalk toggle on the green board: FOLD? Y / N. The art prints the
// letters; this component owns two invisible hit zones and draws the
// tilted chalk-white ellipse around the engaged answer.
class FoldSwitch : public juce::Component, private juce::Timer {
public:
  FoldSwitch(PluginProcessor& p, std::function<void()> onDirty);
  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void engage(bool folding);  // true = Y (free folding), false = N
  void sync();
  void timerCallback() override;
  PluginProcessor& proc_;
  std::function<void()> onDirty_;  // lock lives in the warp tree
  juce::TextButton yesBtn_, noBtn_;
  bool folding_ = false;  // mirrors !warp().monotonicLock()
};

class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer,
                     private juce::AudioProcessorValueTreeState::Listener {
public:
  explicit PluginEditor(PluginProcessor&);
  ~PluginEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void parameterChanged(const juce::String&, float) override;

  PluginProcessor& processorRef;

  Flask driveFlask, threshFlask, smoothFlask;
  SmoothTag smoothTag;
  WarpCurveEditor warpEditor;
  FoldSwitch foldSwitch;
  ScopeView scopeView;
  OsPanel osPanel;
  PresetMenu presetMenu;

  // Declared after the widgets so they outlive the attachments they bind
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  std::unique_ptr<SliderAttachment> driveAtt, threshAtt, smoothAtt;

  void updateTooltip();
  SpeechBubble bubble_;
  juce::TooltipClient* tipComp_ =
      nullptr;                 // component whose tooltip is shown/pending
  juce::uint32 tipSince_ = 0;  // hover start, for the show delay

  void say(const juce::String& text, juce::uint32 ms);  // cat speaks on demand
  RoundBtn noseBtn;
  juce::String sayText_;
  juce::uint32 sayUntil_ = 0;

  CustomLookAndFeel customLF;

  juce::Image bgImage;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}  // namespace audio_plugin
