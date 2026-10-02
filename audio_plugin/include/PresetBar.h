#pragma once

namespace audio_plugin {

class PresetBar : public juce::Component, private juce::Timer {
public:
  explicit PresetBar(PluginProcessor& p);
  ~PresetBar() override;

  void markModified();  // called from PluginEditor when a parameter changes

private:
  void timerCallback() override;
  void sync();
  void showMenu();
  void step(int dir);
  void saveAs();
  void apply(const juce::String& xml,
             const juce::String& name,
             const juce::String& source);

  void resized() override;

  struct PresetRef {
    juce::String name, source, xml;
  };
  std::vector<PresetRef> collect() const;

  // Minimalist preset bar (Valhalla-style): label + pill + prev/next
  struct PillLnF : juce::LookAndFeel_V4 {
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& b,
                              const juce::Colour& c,
                              bool,
                              bool) override {
      auto r = b.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
      g.setColour(c);
      g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
      if (b.isMouseOver()) {
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
      }
    }
  };

  class ArrowBtn : public juce::Component {
  public:
    ArrowBtn(bool leftwards) : left(leftwards) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paint(juce::Graphics& g) override {
      const auto w = (float)getWidth(), h = (float)getHeight();
      juce::Path p;
      if (left)
        p.addTriangle(w * 0.80f, h * 0.15f, w * 0.80f, h * 0.85f, w * 0.20f,
                      h * 0.50f);
      else
        p.addTriangle(w * 0.20f, h * 0.15f, w * 0.20f, h * 0.85f, w * 0.80f,
                      h * 0.50f);
      g.setColour(isMouseOver() ? ui::textHi : ui::text);
      g.fillPath(p);
    }
    void mouseUp(const juce::MouseEvent&) override {
      if (onClick)
        onClick();
    }
    std::function<void()> onClick;

  private:
    bool left;
  };

  PluginProcessor& proc_;
  juce::Label label_{"", "PRESET:"};
  juce::TextButton nameBtn_{"Init"};
  ArrowBtn prev_{true}, next_{false};
  PillLnF pillLnF_;

  bool applying_ = false;
};

}  // namespace audio_plugin