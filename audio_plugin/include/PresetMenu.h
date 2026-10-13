#pragma once

namespace audio_plugin {

// Wall-radio preset control: the art prints the body, the teal name
// window and both arrow pills; this component owns the hit zones, the
// name readout and the hover/press dimming overlays.
class PresetMenu : public juce::Component, private juce::Timer {
public:
  explicit PresetMenu(PluginProcessor& p);
  ~PresetMenu() override = default;

  void markModified();  // called from PluginEditor when a parameter changes
  void setNameFont(const juce::Font& f);

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  void timerCallback() override;
  void sync();
  void showMenu();
  void step(int dir);
  void saveAs();
  void deleteCurrent();  // confirm + remove the applied user preset file
  void finishDelete(const juce::String& name);
  void revealFolder();  // open the user preset dir in the OS file browser
  void apply(const juce::String& xml,
             const juce::String& name,
             const juce::String& source);

  struct PresetRef {
    juce::String name, source, xml;
  };
  std::vector<PresetRef> collect() const;

  // Invisible hit zone over a printed button; reports hover/press to the
  // parent so the parent can dim the silk underneath.
  class ZoneBtn : public juce::Component {
  public:
    ZoneBtn() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void mouseEnter(const juce::MouseEvent&) override { poke(); }
    void mouseExit(const juce::MouseEvent&) override { poke(); }
    void mouseDown(const juce::MouseEvent&) override { poke(); }
    void mouseUp(const juce::MouseEvent& e) override {
      poke();
      // release outside the zone after a drag must not step
      if (onClick && getLocalBounds().contains(e.getPosition()))
        onClick();
    }
    std::function<void()> onClick;

  private:
    void poke() {
      if (auto* p = getParentComponent())
        p->repaint();
    }
  };

  PluginProcessor& proc_;
  juce::TextButton nameBtn_{"Init"};  // invisible: click = menu, tooltip
  ZoneBtn prev_, next_;               // invisible: click = step
  juce::Font nameFont_{juce::FontOptions{}};
  juce::String dispName_;
  bool applying_ = false;
  const int maxNameLength_ = 48;

  float marqueeOffset() const;
  void updateScrolling();
  float textW_ = 0.0f;  // cached width of dispName_ in nameFont_
  int tick_ = 0;        // marquee phase, reset whenever the name changes
  bool scrolling_ = false;
};

}  // namespace audio_plugin