namespace audio_plugin {

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      warpEditor(processorRef.warp(), [this] { processorRef.warpEdited(); }),
      scopeView(processorRef) {
  // Rotary knobs bound to the APVTS parameters
  for (auto* s : {&driveSlider, &thresholdSlider, &smoothSlider}) {
    s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, ui::knobTextW,
                       ui::knobTextH);
    addAndMakeVisible(*s);
  }
  driveLabel.setText("DRIVE", juce::dontSendNotification);
  warpLabel.setText("DEFORMATION", juce::dontSendNotification);
  thresholdLabel.setText("EXTREMA THRESHOLD", juce::dontSendNotification);
  smoothLabel.setText("SMOOTHING", juce::dontSendNotification);
  for (auto* l : {&driveLabel, &warpLabel, &thresholdLabel, &smoothLabel}) {
    l->setJustificationType(juce::Justification::centred);
    l->setFont(juce::Font(
        juce::FontOptions().withHeight(ui::fontLabel).withStyle("Bold")));
    addAndMakeVisible(*l);
  }
  addAndMakeVisible(warpEditor);
  addAndMakeVisible(scopeView);

  // Fold-mode toggle next to the DEFORMATION title: flips the curve's
  // monotonic lock (WarpCurve::setMonotonicLock) and republishes to audio
  foldToggle_.setTooltip(
      "Monotonic lock. ON = curve stays monotone (topology preserved). "
      "OFF = free folding: non-monotone warp, 2x oversampled. "
      "Endpoints stay pinned in both modes.");
  foldToggle_.onClick = [this] {
    auto& w = processorRef.warp();
    w.setMonotonicLock(!w.monotonicLock());
    processorRef.warpEdited();  // rebake LUT on the audio thread
    syncFoldToggle();
  };
  addAndMakeVisible(foldToggle_);
  startTimerHz(10);  // catch external state/preset loads
  syncFoldToggle();

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
  lab.setBounds(r.removeFromTop(ui::labelH));
  sl.setBounds(r);
}

void PluginEditor::timerCallback() {
  syncFoldToggle();
}

void PluginEditor::syncFoldToggle() {
  const bool locked = processorRef.warp().monotonicLock();
  const juce::String t = locked ? "FOLD: OFF" : "FOLD: ON";
  if (foldToggle_.getButtonText() != t)
    foldToggle_.setButtonText(t);
  foldToggle_.setColour(
      juce::TextButton::buttonColourId,
      locked ? juce::Colour(0x00000000) : ui::accentR.withAlpha(0.30f));
  foldToggle_.setColour(juce::TextButton::textColourOnId, ui::text);
}

void PluginEditor::paint(juce::Graphics& g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::resized() {
  auto area = getLocalBounds().reduced(ui::margin);
  // Top control strip: ~1/3 of the height, clamped to sane bounds
  auto top = area.removeFromTop(
      juce::jlimit(ui::stripMin, ui::stripMax, area.getHeight() / 3));
  auto scope = area.withTop(area.getTopLeft().getY() + ui::margin);
  // Four equal columns: DRIVE | DEFORMATION | EXTREMA THRESHOLD | SMOOTHING
  const int gap = ui::controlGap;
  const int colW = (top.getWidth() - 3 * gap) / 4;
  auto c0 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c1 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c2 = top.removeFromLeft(colW);
  top.removeFromLeft(gap);
  auto c3 = top;
  placeKnob(driveLabel, driveSlider, c0);
  {  // warp column: title + fold toggle + always-square editor
    auto r = c1;
    auto title = r.removeFromTop(ui::labelH);
    auto foldRect = title.removeFromRight(72);
    foldRect = foldRect.withSizeKeepingCentre(foldRect.getWidth(), 16);
    title.removeFromRight(4);
    warpLabel.setBounds(title);
    foldToggle_.setBounds(foldRect);
    const int side = std::min(r.getWidth(), r.getHeight());
    warpEditor.setBounds(r.withSizeKeepingCentre(side, side));
  }
  placeKnob(thresholdLabel, thresholdSlider, c2);
  placeKnob(smoothLabel, smoothSlider, c3);
  scopeView.setBounds(scope);
}

}  // namespace audio_plugin
