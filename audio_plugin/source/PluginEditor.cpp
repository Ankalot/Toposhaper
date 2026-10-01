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
  smoothLabel.setText("SMOOTH", juce::dontSendNotification);
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
      "OFF = free folding: non-monotone warp. Use the OS chip for "
      "oversampling. Endpoints stay pinned in both modes.");
  foldToggle_.onClick = [this] {
    auto& w = processorRef.warp();
    w.setMonotonicLock(!w.monotonicLock());
    processorRef.warpEdited();  // rebake LUT on the audio thread
    syncFoldToggle();
  };
  addAndMakeVisible(foldToggle_);
  osBtn_.setTooltip(
      "Oversampling: OFF / X2 / X4. "
      "Cuts aliasing from steep or folded warp curves. Drive = 0 "
      "always stays bit-exact passthrough.");
  osBtn_.onClick = [this] {
    auto* p = processorRef.parameters().getParameter("os_factor");
    const int idx = static_cast<int>(p->getValue() * 2.0f + 0.5f);
    p->setValueNotifyingHost(static_cast<float>((idx + 1) % 3) / 2.0f);
    syncOsButton();
  };
  addAndMakeVisible(osBtn_);
  smoothModeBtn_.setTooltip(
      "Smoothing math for section interiors: SG = Savitzky-Golay polynomial "
      "denoise; PAVA = pool adjacent violators (isotonic monotonization): "
      "interiors become monotone between anchors, wiggles pool into plateaus. "
      "The SMOOTH knob sets strength in both modes (1 = full). Anchors stay "
      "raw always.");
  smoothModeBtn_.onClick = [this] {
    auto* p = processorRef.parameters().getParameter("smooth_mode");
    const int idx =
        static_cast<int>(p->getValue() + 0.5f);  // 2 choices: norm == index
    p->setValueNotifyingHost(idx == 0 ? 1.0f : 0.0f);
    syncSmoothModeButton();
  };
  addAndMakeVisible(smoothModeBtn_);

  startTimerHz(10);  // catch external state/preset loads
  syncFoldToggle();
  syncOsButton();
  syncSmoothModeButton();

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
      "Strength of the selected smoothing math (see the SMOOTH chip):\n"
      "SG = Savitzky-Golay denoise, PAVA = pool-adjacent-violators "
      "monotonization.\n"
      "Anchors stay raw; very short sections stay raw - raise Threshold.");
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
  syncOsButton();
  syncSmoothModeButton();
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

void PluginEditor::syncOsButton() {
  const int f = processorRef.osFactor();
  const juce::String t = f == 2 ? "OS: X2" : (f == 4 ? "OS: X4" : "OS: OFF");
  if (osBtn_.getButtonText() != t)
    osBtn_.setButtonText(t);
  osBtn_.setColour(
      juce::TextButton::buttonColourId,
      f > 1 ? ui::accentL.withAlpha(0.30f) : juce::Colour(0x00000000));
  osBtn_.setColour(juce::TextButton::textColourOnId, ui::text);
}

void PluginEditor::syncSmoothModeButton() {
  const int m = processorRef.smoothMode();
  const juce::String t = m == 1 ? "PAVA" : "SG";
  if (smoothModeBtn_.getButtonText() != t)
    smoothModeBtn_.setButtonText(t);
  smoothModeBtn_.setColour(
      juce::TextButton::buttonColourId,
      m == 1 ? ui::accentR.withAlpha(0.30f) : juce::Colour(0x00000000));
  smoothModeBtn_.setColour(juce::TextButton::textColourOnId, ui::text);
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
  {  // warp column: title + centered option chips + square editor
    auto r = c1;
    warpLabel.setBounds(r.removeFromTop(ui::labelH));
    auto chipRow = r.removeFromTop(16);
    r.removeFromTop(4);
    constexpr int kFoldW = 72, kOsW = 64, kChipGap = 4;
    const int chipsW = kFoldW + kChipGap + kOsW;
    auto chips = chipRow.withWidth(chipsW).withX(
        chipRow.getX() + (chipRow.getWidth() - chipsW) / 2);
    foldToggle_.setBounds(chips.removeFromLeft(kFoldW));
    chips.removeFromLeft(kChipGap);
    osBtn_.setBounds(chips.removeFromLeft(kOsW));
    const int side = std::min(r.getWidth(), r.getHeight());
    warpEditor.setBounds(r.withSizeKeepingCentre(side, side));
  }
  placeKnob(thresholdLabel, thresholdSlider, c2);
  {  // smooth column: title with the mode chip hugging its right side;
    auto r = c3;
    auto labRow = r.removeFromTop(ui::labelH);
    constexpr int kModeW = 52, kModeH = 16, kGap = 6;
    const int labW =
        smoothLabel.getFont().getStringWidth(smoothLabel.getText());
    const int pairW = labW + kGap + kModeW;
    const int x = labRow.getX() + (labRow.getWidth() - pairW) / 2;
    smoothLabel.setBounds(labRow.withWidth(labW).withX(x));
    smoothModeBtn_.setBounds(x + labW + kGap,
                             labRow.getY() + (labRow.getHeight() - kModeH) / 2,
                             kModeW, kModeH);
    smoothSlider.setBounds(r);
  }
  scopeView.setBounds(scope);
}

}  // namespace audio_plugin
