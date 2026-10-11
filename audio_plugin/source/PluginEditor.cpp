namespace audio_plugin {
void SpeechBubble::setText(const juce::String& t) {
  if (t == text_)
    return;
  text_ = t;
  layoutDirty_ = true;
  repaint();
}

void SpeechBubble::setFont(const juce::Font& f) {  // font comes from the LF
  font_ = f;
  layoutDirty_ = true;
  repaint();
}

const juce::String SpeechBubble::getText() {
  return text_;
}

void SpeechBubble::paint(juce::Graphics& g) {
  constexpr float kTailWidth = 18.0f;
  constexpr float kTailHeight = 18.0f;
  constexpr float kTailBottomY = 56.0f;
  constexpr float kCr = 12.0f;
  constexpr float pi = juce::MathConstants<float>::pi;
  constexpr float kTextPaddingX = 12.0f;
  constexpr float kTextPaddingTop = 6.0f;
  constexpr float kTextPaddingBottom = 12.0f;
  // Min height leaves exactly one corner radius of strip below the
  // tail base: the bottom-left arc then starts at the tail edge with
  // a zero-length connector and keeps the full radius.
  const int kMinH = juce::roundToInt(kTailBottomY + kCr) + 1;  // 69
  if (layoutDirty_ && getWidth() > 0) {
    juce::AttributedString as;
    as.setText(text_);
    as.setFont(font_);
    as.setColour(ui::black);
    as.setWordWrap(juce::AttributedString::WordWrap::byWord);
    const float maxTextWidth =
        static_cast<float>(getWidth()) - kTailWidth - (kTextPaddingX * 2.0f);
    layout_.createLayout(as, maxTextWidth);
    layoutDirty_ = false;
    const int h = juce::jlimit(
        kMinH, 400,
        juce::roundToInt(layout_.getHeight()) +
            juce::roundToInt(kTextPaddingTop + kTextPaddingBottom));
    if (h != getHeight()) {
      setSize(getWidth(), h);
      return;
    }
  }
  const auto r = getLocalBounds().toFloat().reduced(1.0f);
  const float left = r.getX() + kTailWidth;
  const float top = r.getY();
  const float right = r.getRight();
  const float bottom = r.getBottom();
  const float tailTopY = kTailBottomY - kTailHeight;
  // Bottom-left corner radius = the strip between tail base and bottom,
  // capped at kCr: its arc's start (left middle of the box) lands
  // exactly on the tail edge, so nothing is drawn "further down".
  const float crBL = juce::jmin(kCr, bottom - kTailBottomY);
  juce::Path bubble;
  bubble.startNewSubPath(left, tailTopY);
  bubble.lineTo(r.getX(), kTailBottomY);
  bubble.lineTo(left, kTailBottomY);
  bubble.addArc(left, bottom - 2 * crBL, 2 * crBL, 2 * crBL, 1.5f * pi,
                1.0f * pi, false);
  bubble.addArc(right - 2 * kCr, bottom - 2 * kCr, 2 * kCr, 2 * kCr, 1.0f * pi,
                0.5f * pi, false);
  bubble.addArc(right - 2 * kCr, top, 2 * kCr, 2 * kCr, 0.5f * pi, 0.0f * pi,
                false);
  bubble.addArc(left, top, 2 * kCr, 2 * kCr, 0.0f * pi, -0.5f * pi, false);
  bubble.closeSubPath();
  g.setColour(ui::paper);
  g.fillPath(bubble);
  g.setColour(ui::black);
  g.strokePath(bubble, juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered,
                                            juce::PathStrokeType::square));
  if (!text_.isEmpty()) {
    const float textLeft = left + kTextPaddingX;
    const float textTop = top + kTextPaddingTop;
    const float textWidth = right - textLeft - kTextPaddingX;
    const float textHeight = bottom - textTop - kTextPaddingBottom;
    layout_.draw(g, {textLeft, textTop, textWidth, textHeight});
  }
}

OsPanel::OsPanel(PluginProcessor& p) : proc_(p) {
  handleImg_ =
      juce::ImageCache::getFromMemory(BinaryData::oversampling_handle_png,
                                      BinaryData::oversampling_handle_pngSize);
  static const char* kTips[3] = {
      "Oversampling OFF: native rate, cheapest CPU; aliasing stays "
      "possible on steep or folded curves.",
      "Oversampling x2: warp runs at double rate with zero-phase "
      "decimation.",
      "Oversampling x4: cleanest highs on folds, four times the warp "
      "CPU.",
  };
  for (int i = 0; i < 3; ++i) {
    btns_[i].setTooltip(kTips[i]);
    btns_[i].setAlpha(0.0f);  // invisible hit zone over the column
    btns_[i].setMouseCursor(juce::MouseCursor::PointingHandCursor);
    btns_[i].onClick = [this, i] { engage(i); };
    addAndMakeVisible(btns_[i]);
  }
  startTimerHz(10);  // catch preset/host-driven changes
  sync();
}

void OsPanel::engage(int idx) {
  if (idx == active_)
    return;  // radio bank: the engaged switch cannot be turned off
  auto* p = proc_.parameters().getParameter("os_factor");
  p->setValueNotifyingHost(static_cast<float>(idx) / 2.0f);  // 0 / 0.5 / 1
  sync();
}

void OsPanel::sync() {
  const int f = proc_.osFactor();  // 1, 2 or 4
  const int idx = (f == 4) ? 2 : (f == 2 ? 1 : 0);
  if (idx != active_) {
    active_ = idx;
    repaint();
  }
}

void OsPanel::timerCallback() {
  sync();
}

void OsPanel::resized() {
  for (int i = 0; i < 3; ++i)
    btns_[i].setBounds(juce::Rectangle<int>(15 + 32 * i, 61, 28, 57));
}

void OsPanel::paint(juce::Graphics& g) {
  for (int i = 0; i < 3; ++i) {
    const bool on = (i == active_);
    // Indicator lamp: amber when engaged, grey otherwise
    g.setColour(on ? ui::accentR : ui::lampOff);
    g.fillEllipse(25.0f + 32.0f * i, 64.0f, 8.0f, 8.0f);
    // Handle sprite: up = engaged, down = released
    const float shift = on ? 0.0f : 12.0f;
    g.drawImage(
        handleImg_,
        juce::Rectangle<float>(24.0f + 32.0f * i, 76.0f + shift, 10.0f, 11.0f));
  }
}

Flask::Flask(const Config& cfg)
    : glass_(cfg.glass),
      liquid_(cfg.liquid),
      rim_(cfg.rim),
      baseTooltip_(cfg.tooltip) {
  for (size_t i = 0; i < cfg.points.size(); ++i) {
    const juce::Point<float> p{cfg.points[i][0] - cfg.originX,
                               cfg.points[i][1] - cfg.originY};
    if (i == 0)
      outline_.startNewSubPath(p);
    else
      outline_.lineTo(p);
  }
  outline_.closeSubPath();
  localBottom_ = cfg.levelY0 - cfg.originY;
  travel_ = cfg.levelY0 - cfg.levelY1;
  setSliderStyle(juce::Slider::RotaryVerticalDrag);  // vertical axis only
  setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  setRange(0.0, 1.0);
  setMouseCursor(juce::MouseCursor::PointingHandCursor);
  valueChanged();
}

void Flask::valueChanged() {
  juce::Slider::valueChanged();  // notify SliderAttachment / listeners
  const int pct = juce::roundToInt(getValue() * 100.0);
  setTooltip(juce::String(pct) + "%\n" + baseTooltip_);
}

bool Flask::hitTest(int x, int y) {
  return outline_.contains(
      juce::Point<float>(static_cast<float>(x), static_cast<float>(y)));
}

void Flask::mouseDown(const juce::MouseEvent& e) {
  pour(e.y);
}
void Flask::mouseDrag(const juce::MouseEvent& e) {
  pour(e.y);
}

void Flask::pour(int yLocal) {
  // Position-based, like a vertical slider: the surface jumps to the
  // cursor on press and follows it 1:1 across the liquid travel.
  setValue(juce::jlimit(0.0f, 1.0f, (localBottom_ - yLocal) / travel_),
           juce::sendNotificationSync);
}

void Flask::paint(juce::Graphics& g) {
  const float v = juce::jlimit(0.0f, 1.0f, static_cast<float>(getValue()));
  const float levelY = localBottom_ - v * travel_;
  // No clip needed: the sprite's opaque backdrop masks every spill.
  if (v > 0.001f) {
    g.setColour(liquid_);
    g.fillRect(1.0f, levelY, static_cast<float>(getWidth()) - 2.0f,
               localBottom_ - levelY);  // body down to the bottom
    g.setColour(rim_);
    g.fillRect(1.0f, levelY, static_cast<float>(getWidth()) - 2.0f, 2.0f);
  }
  g.drawImage(glass_, getLocalBounds().toFloat());
}

namespace {
Flask::Config makeFlaskDrive() {
  Flask::Config cfg;
  cfg.liquid = ui::flaskDrive;
  cfg.rim = ui::flaskDriveTop;
  cfg.glass = juce::ImageCache::getFromMemory(BinaryData::drive_flask_png,
                                              BinaryData::drive_flask_pngSize);
  cfg.points = {
      {45, 491},  {38, 519},  {26, 555},  {18, 580},  {13, 599},  {11, 612},
      {11, 628},  {15, 638},  {21, 644},  {27, 647},  {101, 647}, {108, 644},
      {114, 638}, {118, 628}, {118, 611}, {114, 590}, {109, 573}, {102, 551},
      {93, 525},  {85, 494},  {84, 486},  {84, 419},  {89, 413},  {84, 408},
      {46, 408},  {40, 413},  {45, 420},
  };
  cfg.originX = 10.0f;
  cfg.originY = 407.0f;
  cfg.levelY0 = 646.5f;
  cfg.levelY1 = 551.5f;
  cfg.tooltip =
      "Blend between raw and warped using DEFORMATION EQUATION.\nOnly "
      "section interiors bend - anchor peaks/valleys stay pinned.";
  return cfg;
}

Flask::Config makeFlaskThresh() {
  Flask::Config cfg;
  cfg.liquid = ui::flaskThresh;
  cfg.rim = ui::flaskThreshTop;
  cfg.glass = juce::ImageCache::getFromMemory(BinaryData::thresh_flask_png,
                                              BinaryData::thresh_flask_pngSize);
  cfg.points = {
      {169, 455}, {162, 483}, {150, 519}, {142, 544}, {137, 563}, {135, 576},
      {135, 592}, {139, 602}, {145, 608}, {151, 611}, {225, 611}, {232, 608},
      {238, 602}, {242, 592}, {242, 575}, {238, 554}, {233, 537}, {226, 515},
      {217, 489}, {209, 458}, {208, 450}, {208, 383}, {213, 377}, {208, 372},
      {170, 372}, {164, 377}, {169, 384},
  };
  cfg.originX = 134.0f;
  cfg.originY = 371.0f;
  cfg.levelY0 = 610.5f;
  cfg.levelY1 = 515.5f;
  cfg.tooltip =
      "How strong an extremum must be to anchor a section.\n"
      "Low = keep fine detail; high = demote noise to texture.";
  return cfg;
}

Flask::Config makeFlaskSmooth() {
  Flask::Config cfg;
  cfg.liquid = ui::flaskSmooth;
  cfg.rim = ui::flaskSmoothTop;
  cfg.glass = juce::ImageCache::getFromMemory(BinaryData::smooth_flask_png,
                                              BinaryData::smooth_flask_pngSize);
  cfg.points = {
      {291, 475}, {284, 503}, {272, 539}, {264, 564}, {259, 583}, {257, 596},
      {257, 612}, {261, 622}, {267, 628}, {273, 631}, {347, 631}, {354, 628},
      {360, 622}, {364, 612}, {364, 595}, {360, 574}, {355, 557}, {348, 535},
      {339, 509}, {331, 478}, {330, 470}, {330, 403}, {335, 397}, {330, 392},
      {292, 392}, {286, 397}, {291, 404},
  };
  cfg.originX = 256.0f;
  cfg.originY = 391.0f;
  cfg.levelY0 = 630.5f;
  cfg.levelY1 = 535.5f;
  cfg.tooltip =
      "Strength of the selected smoothing.\n"
      "Anchors stay raw; very short sections stay raw.";
  return cfg;
}
}  // namespace

namespace {
// hit zones and the outline rings around each printed option.
const auto kSgZone = juce::Rectangle<int>(7, 52, 26, 30);
const auto kPavaZone = juce::Rectangle<int>(19, 7, 26, 45);
const auto kSgRing = juce::Rectangle<float>(9.0f, 53.5f, 22.0f, 28.0f);
const auto kPavaRing = juce::Rectangle<float>(20.5f, 8.5f, 24.0f, 44.0f);
const float kTagAngle = 0.3f;  // rad clockwise: the tag top leans right
}  // namespace

SmoothTag::SmoothTag(PluginProcessor& p) : proc_(p) {
  tagImg_ = juce::ImageCache::getFromMemory(BinaryData::smooth_mode_png,
                                            BinaryData::smooth_mode_pngSize);
  sgBtn_.setTooltip(
      "Savitzky-Golay polynomial denoise: smooths section interiors "
      "while keeping their wiggle shape.");
  pavaBtn_.setTooltip(
      "PAVA (pool adjacent violators): isotonic monotonization - "
      "interiors become monotone between anchors, wiggles pool into "
      "plateaus.");
  for (auto* b : {&sgBtn_, &pavaBtn_}) {
    b->setAlpha(0.0f);  // invisible hit zone over the printed label
    b->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(b);
  }
  sgBtn_.onClick = [this] { engage(0); };
  pavaBtn_.onClick = [this] { engage(1); };
  setInterceptsMouseClicks(false, true);
  startTimerHz(10);  // catch preset/host changes

  sync();
}

void SmoothTag::engage(int mode) {
  if (mode == mode_)
    return;  // radio pair: the engaged option stays engaged
  auto* p = proc_.parameters().getParameter("smooth_mode");
  p->setValueNotifyingHost(static_cast<float>(mode));  // 0 = SG, 1 = PAVA
  sync();
}

void SmoothTag::sync() {
  const int m = proc_.smoothMode();
  if (m != mode_) {
    mode_ = m;
    repaint();
  }
}

void SmoothTag::timerCallback() {
  sync();
}

void SmoothTag::resized() {
  sgBtn_.setBounds(kSgZone);
  pavaBtn_.setBounds(kPavaZone);
}

void SmoothTag::paint(juce::Graphics& g) {
  g.drawImage(tagImg_, getLocalBounds().toFloat());
  // Squashed-ellipse outline around the engaged option, rotated with
  // the tag so the ring hugs the printed label instead of the screen.
  const auto ring = (mode_ == 0) ? kSgRing : kPavaRing;
  juce::Path ringPath;
  ringPath.addEllipse(ring);
  ringPath.applyTransform(juce::AffineTransform::rotation(
      kTagAngle, ring.getCentreX(), ring.getCentreY()));
  g.setColour(ui::black);
  g.strokePath(ringPath, juce::PathStrokeType(2.0f));
}

namespace {
// Local geometry measured from the board art (component-local px):
// hit zones and the tilted chalk rings around each printed letter.
const auto kYesZone = juce::Rectangle<int>(0, 0, 33, 33);
const auto kNoZone = juce::Rectangle<int>(33, 0, 33, 33);
const auto kYesRing = juce::Rectangle<float>(3.0f, 1.0f, 27.0f, 31.0f);
const auto kNoRing = juce::Rectangle<float>(35.0f, 2.0f, 27.0f, 31.0f);
constexpr float kYesAngle = 0.52f;  // hand-drawn tilt, rad (CCW on screen)
constexpr float kNoAngle = 0.52f;   // slightly different = alive hand
}  // namespace

FoldSwitch::FoldSwitch(PluginProcessor& p, std::function<void()> onDirty)
    : proc_(p), onDirty_(std::move(onDirty)) {
  yesBtn_.setTooltip(
      "FOLD = YES: free folding - the warp curve may become "
      "non-monotone, steep drives fold the waveform onto itself.");
  noBtn_.setTooltip(
      "FOLD = NO: monotonic lock - topology preserved, the curve "
      "stays monotone; steep drives saturate instead of folding.");
  for (auto* b : {&yesBtn_, &noBtn_}) {
    b->setAlpha(0.0f);  // invisible hit zone over the chalk letter
    b->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(b);
  }
  yesBtn_.onClick = [this] { engage(true); };
  noBtn_.onClick = [this] { engage(false); };
  setInterceptsMouseClicks(false, true);  // only the zones are clickable
  startTimerHz(10);                       // catch preset/host changes
  sync();
}

void FoldSwitch::engage(bool folding) {
  if (folding == folding_)
    return;  // radio pair: the circled answer stays circled
  proc_.warp().setMonotonicLock(!folding);  // Y = fold => lock off
  proc_.warpEdited();                       // rebake LUT on the audio thread
  if (onDirty_)
    onDirty_();  // lock is part of the warp tree = preset state
  sync();
}

void FoldSwitch::sync() {
  const bool folding = !proc_.warp().monotonicLock();
  if (folding != folding_) {
    folding_ = folding;
    repaint();
  }
}

void FoldSwitch::timerCallback() {
  sync();
}

void FoldSwitch::resized() {
  yesBtn_.setBounds(kYesZone);
  noBtn_.setBounds(kNoZone);
}

void FoldSwitch::paint(juce::Graphics& g) {
  // Tilted chalk ellipse around the engaged answer, same logic as the
  // SG/PAVA tag ring but white on green and hand-tilted per letter.
  const auto ring = folding_ ? kYesRing : kNoRing;
  const float angle = folding_ ? kYesAngle : kNoAngle;
  juce::Path ringPath;
  ringPath.addEllipse(ring);
  ringPath.applyTransform(juce::AffineTransform::rotation(
      angle, ring.getCentreX(), ring.getCentreY()));
  g.setColour(ui::chalk);
  g.strokePath(ringPath, juce::PathStrokeType(2.0f));
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      warpEditor(processorRef.warp(),
                 [this] {
                   processorRef.warpEdited();
                   presetMenu.markModified();
                 }),
      foldSwitch(processorRef, [this] { presetMenu.markModified(); }),
      scopeView(processorRef),
      osPanel(p),
      driveFlask(makeFlaskDrive()),
      threshFlask(makeFlaskThresh()),
      smoothFlask(makeFlaskSmooth()),
      smoothTag(processorRef),
      presetMenu(p),
      customLF() {
  addAndMakeVisible(scopeView);

  addAndMakeVisible(driveFlask);
  addAndMakeVisible(threshFlask);
  addAndMakeVisible(smoothFlask);

  warpEditor.setReadoutFont(customLF.caveatPt(20.0f));  // handwritten chalk
  addAndMakeVisible(warpEditor);

  addAndMakeVisible(foldSwitch);
  addAndMakeVisible(osPanel);
  addAndMakeVisible(smoothTag);

  startTimerHz(10);  // catch external state/preset loads

  for (auto* id :
       {"drive", "threshold", "smooth_mix", "os_factor", "smooth_mode"})
    processorRef.parameters().addParameterListener(id, this);
  addAndMakeVisible(presetMenu);
  presetMenu.setNameFont(customLF.oswaldPt(16.0f));

  driveAtt = std::make_unique<SliderAttachment>(processorRef.parameters(),
                                                "drive", driveFlask);
  threshAtt = std::make_unique<SliderAttachment>(processorRef.parameters(),
                                                 "threshold", threshFlask);
  smoothAtt = std::make_unique<SliderAttachment>(processorRef.parameters(),
                                                 "smooth_mix", smoothFlask);

  bgImage = juce::ImageCache::getFromMemory(BinaryData::background_png,
                                            BinaryData::background_pngSize);

  addAndMakeVisible(bubble_);
  bubble_.setFont(customLF.oswaldPt(16.0f));
  bubble_.setBounds(235, 112, 190, 64);
  bubble_.setInterceptsMouseClicks(false, false);
  bubble_.setVisible(false);

  noseBtn.onClick = [this] { say("MEOW!", 1500); };
  addAndMakeVisible(noseBtn);

  setLookAndFeel(&customLF);
  setResizable(false, false);
  setSize(1000, 681);
  setConstrainer(nullptr);
}

PluginEditor::~PluginEditor() {
  setLookAndFeel(nullptr);

  juce::Typeface::clearTypefaceCache();

  for (auto* id :
       {"drive", "threshold", "smooth_mix", "os_factor", "smooth_mode"})
    processorRef.parameters().removeParameterListener(id, this);
}

void PluginEditor::parameterChanged(const juce::String&, float) {
  presetMenu.markModified();
}

void PluginEditor::say(const juce::String& text, juce::uint32 ms) {
  sayText_ = text;
  sayUntil_ = juce::Time::getMillisecondCounter() + ms;
  bubble_.setText(text);
  bubble_.setVisible(true);
  bubble_.toFront(false);
  tipComp_ = nullptr;  // hover delay restarts cleanly after the meow fades
}

void PluginEditor::updateTooltip() {
  // The cat's own voice outranks the tooltip pipeline for its duration.
  if (juce::Time::getMillisecondCounter() < sayUntil_) {
    if (!bubble_.isVisible() || bubble_.getText() != sayText_) {
      bubble_.setText(sayText_);
      bubble_.setVisible(true);
      bubble_.toFront(false);
    }
    return;
  }
  auto* under = juce::Desktop::getInstance()
                    .getMainMouseSource()
                    .getComponentUnderMouse();
  juce::TooltipClient* src = nullptr;
  for (auto* c = under; c != nullptr && c != this;
       c = c->getParentComponent()) {
    if (auto* tc = dynamic_cast<juce::TooltipClient*>(c)) {
      if (!tc->getTooltip().isEmpty()) {
        src = tc;
        break;
      }
    }
  }
  if (src == nullptr) {
    tipComp_ = nullptr;
    bubble_.setVisible(false);
    return;
  }
  if (src != tipComp_) {
    tipComp_ = src;
    tipSince_ = juce::Time::getMillisecondCounter();
    bubble_.setVisible(false);
    return;
  }
  if (juce::Time::getMillisecondCounter() - tipSince_ < 300)
    return;
  const juce::String txt = src->getTooltip();
  if (!bubble_.isVisible()) {
    bubble_.setText(txt);
    bubble_.setVisible(true);
    bubble_.toFront(false);
  } else if (bubble_.isVisible() && txt != bubble_.getText()) {
    bubble_.setText(txt);
  }
}

void PluginEditor::timerCallback() {
  updateTooltip();
}

void PluginEditor::paint(juce::Graphics& g) {
  g.drawImage(bgImage, getLocalBounds().toFloat());
}

void PluginEditor::resized() {
  warpEditor.setBounds(462, 40, 326, 222);
  foldSwitch.setBounds(690, 184, 76, 68);
  driveFlask.setBounds(10, 407, 110, 241);
  threshFlask.setBounds(134, 371, 110, 241);
  smoothFlask.setBounds(256, 391, 110, 241);
  smoothTag.setBounds(240, 462, 106, 90);
  presetMenu.setBounds(92, 269, 95, 48);
  osPanel.setBounds(852, 89, 122, 154);
  scopeView.setBounds(376, 303, 624, 354);
  noseBtn.setBounds(178, 180, 10, 10);
}

}  // namespace audio_plugin
