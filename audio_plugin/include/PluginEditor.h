#pragma once

namespace audio_plugin {
class PluginEditor : public juce::AudioProcessorEditor {
public:
  explicit PluginEditor(PluginProcessor&);

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  // Framed placeholder until the real warp-curve editor and topology view
  // land; keeps the concept layout alive meanwhile.
  class StubPanel : public juce::Component {
  public:
    explicit StubPanel(juce::String text) : text_(std::move(text)) {}
    void paint(juce::Graphics& g) override {
      g.fillAll(juce::Colours::black.withAlpha(0.35f));
      g.setColour(juce::Colours::white.withAlpha(0.55f));
      g.drawRect(getLocalBounds().toFloat(), 2.0f);
      g.setFont(14.0f);
      g.drawFittedText(text_, getLocalBounds().reduced(10),
                       juce::Justification::centred, 2);
    }

  private:
    juce::String text_;
  };

  void placeKnob(juce::Label& lab, juce::Slider& sl, juce::Rectangle<int> col);

  PluginProcessor& processorRef;

  juce::TooltipWindow tooltipWindow{this, 400};

  juce::Slider driveSlider, thresholdSlider, smoothSlider;
  juce::Label driveLabel, warpLabel, thresholdLabel, smoothLabel;
  WarpCurveEditor warpEditor;
  ScopeView scopeView;

  // Declared after the widgets so they outlive the attachments they bind
  using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  std::unique_ptr<SliderAttachment> driveAtt, thresholdAtt, smoothAtt;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}  // namespace audio_plugin
