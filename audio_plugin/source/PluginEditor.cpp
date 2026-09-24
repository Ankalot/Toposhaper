namespace audio_plugin {
PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p) {
  // Rotary knobs bound to the APVTS parameters
  for (auto* s : {&driveSlider, &thresholdSlider, &smoothSlider}) {
    s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 16);
    addAndMakeVisible(*s);
  }

  driveLabel.setText("DRIVE", juce::dontSendNotification);
  warpLabel.setText("DEFORMATION", juce::dontSendNotification);
  thresholdLabel.setText("EXTREMA THRESHOLD", juce::dontSendNotification);
  smoothLabel.setText("SMOOTHING", juce::dontSendNotification);
  for (auto* l : {&driveLabel, &warpLabel, &thresholdLabel, &smoothLabel}) {
    l->setJustificationType(juce::Justification::centred);
    l->setFont(
        juce::Font(juce::FontOptions().withHeight(15.0f).withStyle("Bold")));
    addAndMakeVisible(*l);
  }

  addAndMakeVisible(warpStub);
  addAndMakeVisible(scopeStub);

  driveAtt = std::make_unique<SliderAttachment>(processorRef.parameters(),
                                                "drive", driveSlider);
  thresholdAtt = std::make_unique<SliderAttachment>(
      processorRef.parameters(), "threshold", thresholdSlider);
  smoothAtt = std::make_unique<SliderAttachment>(processorRef.parameters(),
                                                 "smooth_mix", smoothSlider);

  driveSlider.setTooltip(
      "Blend between raw and warped: 0 = untouched, 1 = fully deformed using "
      "DEFORMATION curve.\n"
      "Only section interiors bend - anchor peaks/valleys stay pinned.");

  smoothSlider.setTooltip(
      "Denoises section interiors before the warp; anchors stay raw.\n"
      "Very short sections stay raw - raise Threshold to lengthen them.");

  thresholdSlider.setTooltip(
      "How strong an extremum must be to anchor a section.\n"
      "Low = keep fine detail; high = demote noise to texture.");

  setResizable(true, false);
  setResizeLimits(560, 420, 1400, 1000);
  setSize(820, 600);
}

void PluginEditor::placeKnob(juce::Label& lab,
                             juce::Slider& sl,
                             juce::Rectangle<int> col) {
  auto r = col;
  lab.setBounds(r.removeFromTop(26));
  sl.setBounds(r);
}

void PluginEditor::paint(juce::Graphics& g) {
  // (Our component is opaque, so we must completely fill the background with a
  // solid colour)
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  g.setFont(15.0f);
  g.drawFittedText("Hello World!", getLocalBounds(),
                   juce::Justification::centred, 1);
}

void PluginEditor::resized() {
  auto area = getLocalBounds().reduced(14);
  // Top control strip: ~1/3 of the height, clamped to sane bounds
  auto top = area.removeFromTop(juce::jlimit(150, 220, area.getHeight() / 3));
  auto scope = area.withTop(area.getTopLeft().getY() + 14);

  // Four equal columns: DRIVE | DEFORMATION | EXTREMA THRESHOLD | SMOOTHING
  const int gap = 16;
  const int colW = (top.getWidth() - 3 * gap) / 4;
  auto c0 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c1 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c2 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c3 = top;

  placeKnob(driveLabel, driveSlider, c0);
  {  // warp column: title + stub panel
    auto r = c1;
    warpLabel.setBounds(r.removeFromTop(26));
    warpStub.setBounds(r);
  }
  placeKnob(thresholdLabel, thresholdSlider, c2);
  placeKnob(smoothLabel, smoothSlider, c3);

  scopeStub.setBounds(scope);
}
}  // namespace audio_plugin
