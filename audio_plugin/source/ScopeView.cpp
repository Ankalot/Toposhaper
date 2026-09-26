#include <cmath>

namespace audio_plugin {
namespace {
// Scope-prefixed: unity build merges anonymous namespaces across .cpp files
const juce::Colour kScopeBg{0xff101218};
const juce::Colour kScopeFrame{0x808a93a6};
const juce::Colour kScopeGrid{0x14ffffff};
const juce::Colour kScopeZero{0x33ffffff};
const juce::Colour kScopeTraceL{0xff4f7dff};
const juce::Colour kScopeTraceR{0xffffb454};  // colorblind-safe vs blue
const juce::Colour kScopeGhost{0x38e8ecf5};
const juce::Colour kScopeText{0x99e8ecf5};
const juce::Colour kScopeMarkExtL{0xff5aa2ff};
const juce::Colour kScopeMarkExtR{0xffffc078};
const juce::Colour kScopeMarkFall{0xffe5484d};
const juce::Colour kScopeDelta{0x1f4f7dff};
const juce::Colour kScopeDeltaR{0x1fffb454};  // R channel delta fill
const juce::Colour kScopeMono{0xff5a6472};
constexpr int kScopeStripH = 24;

juce::Colour traceColour(int ch) {
  return ch == 0 ? kScopeTraceL : kScopeTraceR;
}
juce::Colour extColour(int ch) {
  return ch == 0 ? kScopeMarkExtL : kScopeMarkExtR;
}

int niceDiv(int viewLen) {
  const float target = static_cast<float>(viewLen) / 6.0f;
  float best = 1.0f;
  for (float base = 1.0f; base < 1.0e7f; base *= 10.0f)
    for (const float m : {1.0f, 2.0f, 5.0f}) {
      const float cand = base * m;
      if (std::abs(cand - target) < std::abs(best - target))
        best = cand;
    }
  return std::max(1, juce::roundToInt(best));
}
}  // namespace

ScopeView::ScopeView(PluginProcessor& p) : proc_(p) {
  trackBtn_.setClickingTogglesState(true);
  trackBtn_.setToggleState(true, juce::dontSendNotification);
  trackBtn_.setTooltip(
      "Waveform tracking: ON = triggered (stable picture), "
      "OFF = scroll (true-time, phosphor trail). Label shows "
      "LOCK / FREE / OFF.");
  trackBtn_.onClick = [this] {
    trailN_[0] = trailN_[1] = 0;
    updateModeLabel();
    repaint();
  };
  addAndMakeVisible(trackBtn_);

  chanBtn_.setTooltip(
      "Display channel cycle: L -> L+R (overlay) -> R -> L|R (split). "
      "Shift-click walks the cycle backwards. This is the only control "
      "that changes what is drawn.");
  chanBtn_.onClick = [this] {
    // Cycle ring: L -> L+R -> R -> L|R -> L (overlay one click from L)
    constexpr int kNext[4] = {2, 3, 1, 0};
    constexpr int kPrev[4] = {3, 2, 0, 1};  // inverse ring for shift-click
    const bool back = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
    chanMode_ = back ? kPrev[chanMode_] : kNext[chanMode_];
    trailN_[0] = trailN_[1] = 0;
    updateChanLabel();
    repaint();
  };
  addAndMakeVisible(chanBtn_);

  // Passive indicator chip: reports stereo width, never changes the layout,
  // and no longer looks or behaves like a button.
  smChip_.setTooltip(
      "Side/mid energy of the visible window (indicator only): "
      "grey = mono-ish, amber = wide. Use the CH button to change mode.");
  addAndMakeVisible(smChip_);

  vZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
  vZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  vZoom_.setRange(-2.0, 5.0, 0.01);  // gain = 2^v: 0.25x .. 32x
  vZoom_.setValue(0.0, juce::dontSendNotification);
  vZoom_.setTooltip("Vertical zoom (amplitude), shared by both channels");
  vZoom_.setScrollWheelEnabled(false);
  vZoom_.onValueChange = [this] { repaint(); };
  addAndMakeVisible(vZoom_);

  hZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
  hZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  hZoom_.setRange(0.0, 1000.0, 1.0);  // 256..32768 samples, log
  hZoom_.setValue(hFromLen(2048), juce::dontSendNotification);
  hZoom_.setTooltip("Horizontal zoom: visible window 256 .. 32768 samples");
  hZoom_.setScrollWheelEnabled(false);
  hZoom_.onValueChange = [this] {
    trailN_[0] = trailN_[1] = 0;
    repaint();
  };
  addAndMakeVisible(hZoom_);

  setOpaque(true);
  startTimerHz(60);
  updateChanLabel();
}

void ScopeView::SmChip::setText(const juce::String& t) {
  if (t != text_) {
    text_ = t;
    repaint();
  }
}

void ScopeView::SmChip::setTint(juce::Colour c) {
  if (c != tint_) {
    tint_ = c;
    repaint();
  }
}

void ScopeView::SmChip::paint(juce::Graphics& g) {
  const auto r = getLocalBounds().toFloat();
  g.setColour(tint_.withAlpha(0.30f));
  g.fillRoundedRectangle(r, 4.0f);
  g.setColour(tint_.withAlpha(0.85f));
  g.drawRoundedRectangle(r, 4.0f, 1.0f);
  g.setColour(kScopeText);
  g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
  g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
}

void ScopeView::SmChip::setTooltip(const juce::String& newTooltip) {
  tooltipText = newTooltip;
}

juce::String ScopeView::SmChip::getTooltip() {
  return tooltipText;
}

juce::Rectangle<float> ScopeView::traceRect() const {
  auto r = getLocalBounds().toFloat();
  return r.withTop(r.getY() + kScopeStripH).reduced(2.0f);
}

juce::Rectangle<float> ScopeView::laneRect(int ch) const {
  const auto full = traceRect();
  if (chanMode_ != 3 || !stereo_)
    return full;
  const float half = full.getHeight() * 0.5f;
  if (ch == 0)
    return full.withHeight(half - 3.0f);
  return full.withY(full.getY() + half + 3.0f).withHeight(half - 3.0f);
}

int ScopeView::primaryChannel() const {
  return (chanMode_ == 1 && hasR_) ? 1 : 0;
}
bool ScopeView::channelActive(int ch) const {
  return stereo_ ? true : ch == prim_;
}

int ScopeView::lenFromH(double t) const {
  return juce::jlimit(256, 32768,
                      juce::roundToInt(256.0 * std::pow(128.0, t / 1000.0)));
}
double ScopeView::hFromLen(int len) const {
  return 1000.0 * std::log(static_cast<double>(len) / 256.0) / std::log(128.0);
}

void ScopeView::resized() {
  auto strip = getLocalBounds().removeFromTop(kScopeStripH).reduced(2);
  trackBtn_.setBounds(strip.removeFromLeft(112));
  strip.removeFromLeft(6);
  chanBtn_.setBounds(strip.removeFromLeft(58));
  strip.removeFromLeft(6);
  auto chipRect = strip.removeFromLeft(78);
  smChip_.setBounds(chipRect.withSizeKeepingCentre(chipRect.getWidth(),
                                                   chipRect.getHeight() - 6));
  strip.removeFromLeft(6);
  vZoom_.setBounds(strip.removeFromLeft(80));
  strip.removeFromLeft(6);
  hZoom_.setBounds(strip.removeFromLeft(130));
  strip.removeFromLeft(10);
  readoutRect_ = strip.toFloat();
  trailN_[0] = trailN_[1] = 0;
}

void ScopeView::mouseWheelMove(const juce::MouseEvent& e,
                               const juce::MouseWheelDetails& wheel) {
  // Proportional zoom: every wheel event changes the view by an amount
  // proportional to its own delta, so a single tick always zooms no matter
  // how the driver quantises notches. The clamp only tames momentum spikes.
  const float d = juce::jlimit(-2.0f, 2.0f, wheel.deltaY);  // >0 = up = in
  if (std::abs(d) < 1.0e-4f)
    return;
  if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isShiftDown())
    vZoom_.setValue(vZoom_.getValue() + d * 0.35, juce::sendNotificationSync);
  else
    hZoom_.setValue(hZoom_.getValue() - d * 30.0, juce::sendNotificationSync);
}

void ScopeView::timerCallback() {
  refresh();
  repaint();
}

void ScopeView::updateModeLabel() {
  juce::String s;
  if (!trackBtn_.getToggleState())
    s = "TRACK: OFF";
  else
    s = locked_ ? "TRACK: LOCK" : "TRACK: FREE";
  if (s != modeLabel_) {
    modeLabel_ = s;
    trackBtn_.setButtonText(s);
  }
}

void ScopeView::updateChanLabel() {
  static const char* kNames[4] = {"CH: L", "CH: R", "CH: L+R", "CH: L/R"};
  const juce::String s = hasR_ ? kNames[chanMode_] : juce::String("CH: L");
  if (chanBtn_.getButtonText() != s)
    chanBtn_.setButtonText(s);
}

void ScopeView::updateSmMetric() {
  if (!hasR_ || spanN_[0] == 0 || spanN_[1] == 0) {
    smDb_ = -120.0f;
    return;
  }
  const size_t stop = std::min(std::min(spanN_[0], spanN_[1]),
                               static_cast<size_t>(viewStart_ + viewLen_));
  double m2 = 0.0, s2 = 0.0;
  for (size_t i = static_cast<size_t>(viewStart_); i < stop; ++i) {
    const float l = span_[0][i], r = span_[1][i];
    const float mm = 0.5f * (l + r), ss = 0.5f * (l - r);
    m2 += static_cast<double>(mm) * mm;
    s2 += static_cast<double>(ss) * ss;
  }
  smDb_ =
      (m2 > 1e-12) ? static_cast<float>(10.0 * std::log10(s2 / m2)) : -120.0f;
}

void ScopeView::updateSmChip() {
  juce::String txt;
  juce::Colour col = kScopeMono;
  if (!hasR_ || smDb_ < -40.0f) {
    txt = "S/M: MONO";
  } else {
    txt = "S/M " +
          juce::String(static_cast<int>(juce::jlimit(-99.0f, 99.0f, smDb_))) +
          "dB";
    if (smDb_ > -10.0f)
      col = kScopeTraceR;  // wide: light up in the R hue
    else
      col = kScopeMono.brighter(0.2f);
  }
  smChip_.setText(txt);
  smChip_.setTint(col);
}

void ScopeView::refresh() {
  viewLen_ = lenFromH(hZoom_.getValue());
  vZoomGain_ = static_cast<float>(std::pow(2.0, vZoom_.getValue()));
  const size_t want = static_cast<size_t>(viewLen_) * 2 + 4096;
  const uint64_t W = static_cast<uint64_t>(proc_.getLatencySamples());

  for (int ch = 0; ch < 2; ++ch) {
    span_[ch].resize(want);
    uint64_t st = 0;
    spanN_[ch] = proc_.readScopeSpan(span_[ch].data(), want, st, ch);
    if (spanN_[ch] < static_cast<size_t>(viewLen_)) {
      spanN_[ch] = 0;
      continue;
    }
    spanStart_[ch] = st;
  }
  // R is usable for stereo only when its history exists and its ring cursor
  // matches L's (same emit count per block); otherwise degrade to single-ch
  hasR_ = spanN_[1] >= static_cast<size_t>(viewLen_) &&
          spanStart_[0] == spanStart_[1];
  prim_ = primaryChannel();
  stereo_ = hasR_ && chanMode_ >= 2;
  if (spanN_[prim_] == 0) {
    updateModeLabel();
    updateChanLabel();
    updateSmChip();
    return;
  }

  // Ghost input for active channels (single or stereo)
  for (int ch = 0; ch < 2; ++ch) {
    if (channelActive(ch) && W > 0 && spanStart_[ch] >= W) {
      inSpan_[ch].resize(want);
      inN_[ch] = proc_.readInputSpanAt(inSpan_[ch].data(), want,
                                       spanStart_[ch] - W, ch);
    } else {
      inN_[ch] = 0;
    }
  }

  // Shared trigger: primary channel in single modes, L in stereo modes
  const bool tracking = trackBtn_.getToggleState();
  int vs = -1;
  locked_ = false;
  if (tracking) {
    const int t = findTrigger(span_[prim_].data(), spanN_[prim_]);
    if (t >= 0) {
      vs = t - viewLen_ * 15 / 100;  // pre-trigger 15%
      if (vs < 0)
        vs = 0;
      locked_ = true;
    }
  }
  if (vs < 0)
    vs = static_cast<int>(spanN_[prim_]) - viewLen_;  // FREE / SCROLL
  viewStart_ = vs;

  marks_.resize(8192);
  markN_ = proc_.readMarkerSpan(marks_.data(), marks_.size());

  // Shared vertical scale (common V/div across modes and lanes)
  const float scale = traceRect().getHeight() * 0.45f * vZoomGain_;
  for (int ch = 0; ch < 2; ++ch) {
    if (!channelActive(ch) || spanN_[ch] == 0) {
      trailN_[ch] = 0;
      continue;
    }
    buildPaths(ch, laneRect(ch), scale, true);
  }

  if ((frame_++ % 10) == 0)
    updateSmMetric();
  updateModeLabel();
  updateChanLabel();
  updateSmChip();
}

int ScopeView::findTrigger(const float* s, size_t n) const {
  float peak = 0.0f;
  for (size_t i = 0; i < n; ++i)
    peak = std::max(peak, std::abs(s[i]));
  if (peak < 1.0e-4f)
    return -1;                      // silence / DC: nothing to lock onto
  const float gate = 0.05f * peak;  // schmitt hysteresis vs noise
  const int pre = viewLen_ * 15 / 100;
  const int limit = static_cast<int>(n);
  bool high = false;
  int best = -1;  // newest LOW->HIGH whose whole view still fits the span
  for (int i = 1; i < limit; ++i) {
    if (!high && s[i] >= gate) {
      high = true;
      if (i - pre + viewLen_ <= limit)
        best = i;
    } else if (high && s[i] <= -gate) {
      high = false;
    }
  }
  return best;
}

void ScopeView::buildPaths(int ch,
                           const juce::Rectangle<float>& r,
                           float scale,
                           bool withGhost) {
  if (r.isEmpty()) {
    trailN_[ch] = 0;
    return;
  }
  const int w = std::max(1, juce::roundToInt(r.getWidth()));
  const float cy = r.getCentreY();
  const auto& span = span_[ch];
  const size_t spanN = spanN_[ch];
  const auto& inSpan = inSpan_[ch];
  const size_t inN = withGhost ? inN_[ch] : 0;

  juce::Path out, in;
  bool firstOut = true, firstIn = true;
  juce::Path delta;
  std::vector<float> dxs, dOut, dIn;
  dxs.reserve(static_cast<size_t>(w));
  dOut.reserve(static_cast<size_t>(w));
  dIn.reserve(static_cast<size_t>(w));

  for (int c = 0; c < w; ++c) {
    const int p0 =
        viewStart_ + static_cast<int>(static_cast<int64_t>(c) * viewLen_ / w);
    const int p1 = viewStart_ +
                   static_cast<int>(static_cast<int64_t>(c + 1) * viewLen_ / w);
    const int q1 = std::max(p0 + 1, p1);
    const float x =
        r.getX() + (static_cast<float>(c) + 0.5f) * r.getWidth() / w;

    float mn = 1.0e9f, mx = -1.0e9f;
    for (int i = p0; i < q1 && i < static_cast<int>(spanN); ++i) {
      mn = std::min(mn, span[static_cast<size_t>(i)]);
      mx = std::max(mx, span[static_cast<size_t>(i)]);
    }
    if (mn <= mx) {
      if (firstOut) {
        out.startNewSubPath(x, cy - mx * scale);
        firstOut = false;
      } else
        out.lineTo(x, cy - mx * scale);
      out.lineTo(x, cy - mn * scale);
    }

    if (inN > 0) {
      // Column index maps 1:1 between output span and input span (both
      // rings share the stream indexing through the constant latency)
      const int j0 = p0, j1 = q1;
      float imn = 1.0e9f, imx = -1.0e9f;
      for (int j = j0; j < j1 && j < static_cast<int>(inN); ++j) {
        imn = std::min(imn, inSpan[static_cast<size_t>(j)]);
        imx = std::max(imx, inSpan[static_cast<size_t>(j)]);
      }
      if (imn <= imx) {
        if (firstIn) {
          in.startNewSubPath(x, cy - imx * scale);
          firstIn = false;
        } else
          in.lineTo(x, cy - imx * scale);
        in.lineTo(x, cy - imn * scale);
        if (mn <= mx) {
          dxs.push_back(x);
          dOut.push_back(cy - 0.5f * (mn + mx) * scale);
          dIn.push_back(cy - 0.5f * (imn + imx) * scale);
        }
      }
    }
  }

  if (dxs.size() >= 2) {
    delta.startNewSubPath(dxs[0], dOut[0]);
    for (size_t i = 1; i < dxs.size(); ++i)
      delta.lineTo(dxs[i], dOut[i]);
    for (size_t i = dxs.size(); i-- > 0;)
      delta.lineTo(dxs[i], dIn[i]);
    delta.closeSubPath();
  }
  deltaPath_[ch] = delta;
  inPath_[ch] = in;
  outPath_[ch] = out;

  if (trackBtn_.getToggleState() && locked_) {
    trail_[ch][0] = out;  // triggered: frames overlay, no trail
    trailN_[ch] = 1;
  } else {
    trail_[ch][2] = trail_[ch][1];  // scroll: phosphor trail
    trail_[ch][1] = trail_[ch][0];
    trail_[ch][0] = out;
    trailN_[ch] = std::min(3, trailN_[ch] + 1);
  }
}

void ScopeView::drawDbGrid(juce::Graphics& g,
                           const juce::Rectangle<float>& r,
                           float scale) const {
  const float cy = r.getCentreY();
  g.setColour(kScopeZero);
  g.drawLine({r.getX(), cy, r.getRight(), cy});

  struct DbLine {
    float y;
    int db;
  };
  juce::Array<DbLine> dbLines;
  for (const int db : {0, -3, -6, -12, -24, -36, -48}) {
    const float a = std::pow(10.0f, static_cast<float>(db) / 20.0f) * scale;
    for (const float sgn : {1.0f, -1.0f}) {
      const float y = cy - sgn * a;
      if (y < r.getY() + 1.0f || y > r.getBottom() - 1.0f)
        continue;
      if (std::abs(y - cy) < 12.0f)
        continue;  // hugs the zero line: nothing to label there
      dbLines.add({y, db});
    }
  }
  std::sort(dbLines.begin(), dbLines.end(),
            [](const DbLine& a, const DbLine& b) { return a.y < b.y; });
  constexpr float kMinDbGap = 18.0f;
  float lastY = -1.0e9f;
  const auto smallFont = juce::Font(juce::FontOptions().withHeight(10.0f));
  g.setFont(smallFont);
  for (const auto& L : dbLines) {
    if (L.y - lastY < kMinDbGap)
      continue;
    lastY = L.y;
    g.setColour(L.db == 0 ? kScopeZero : kScopeGrid);
    g.drawLine({r.getX(), L.y, r.getRight(), L.y});
    g.setColour(kScopeText);
    const int ly = juce::jlimit(juce::roundToInt(r.getY()) + 1,
                                juce::roundToInt(r.getBottom()) - 13,
                                juce::roundToInt(L.y) - 12);
    g.drawText(juce::String(L.db) + " dB", juce::roundToInt(r.getX()) + 2, ly,
               52, 12, juce::Justification::left, false);
  }
}

void ScopeView::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat();
  g.fillAll(kScopeBg);
  g.setColour(kScopeFrame);
  g.drawRect(bounds, 1.5f);

  const auto full = traceRect();
  if (full.isEmpty())
    return;
  const double sr = proc_.getSampleRate();
  const auto smallFont = juce::Font(juce::FontOptions().withHeight(10.0f));
  g.setFont(smallFont);
  const float scale = full.getHeight() * 0.45f * vZoomGain_;
  const bool split = stereo_ && chanMode_ == 3;

  // Vertical grid over the full rect: shared time axis in every mode
  const int div = niceDiv(viewLen_);
  for (int ago = 0; ago <= viewLen_; ago += div) {
    const float x =
        full.getRight() - static_cast<float>(ago) * full.getWidth() / viewLen_;
    if (x < full.getX() - 0.5f)
      break;
    g.setColour(ago == 0 ? kScopeZero : kScopeGrid);
    g.drawLine({x, full.getY(), x, full.getBottom()});
    const bool inGutter = x < full.getX() + 58.0f;  // left column = dB labels
    if (!inGutter) {
      g.setColour(kScopeText);
      g.drawText(juce::String(ago), juce::roundToInt(x) + 2,
                 juce::roundToInt(full.getY()) + 2, 64, 12,
                 juce::Justification::left, false);
      if (sr > 0.0 && ago > 0)
        g.drawText(juce::String(1000.0 * ago / sr, 2) + "ms",
                   juce::roundToInt(x) + 2,
                   juce::roundToInt(full.getBottom()) - 14, 72, 12,
                   juce::Justification::left, false);
    }
  }

  // Horizontal dB grid: one pass per distinct lane
  drawDbGrid(g, laneRect(0), scale);
  if (split)
    drawDbGrid(g, laneRect(1), scale);

  // Traces per active channel, clipped to their lane
  for (int ch = 0; ch < 2; ++ch) {
    if (!channelActive(ch) || spanN_[ch] == 0)
      continue;
    const auto r = laneRect(ch);
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    if (inN_[ch] > 0) {
      g.setColour(ch == 0 ? kScopeDelta : kScopeDeltaR);
      g.fillPath(deltaPath_[ch]);
      g.setColour(kScopeGhost);
      g.strokePath(inPath_[ch], juce::PathStrokeType(1.2f));
    }
    for (int i = std::min(trailN_[ch], 3) - 1; i >= 0; --i) {
      const float alpha = (i == 0) ? 1.0f : (i == 1 ? 0.35f : 0.15f);
      g.setColour(traceColour(ch).withAlpha(alpha));
      g.strokePath(trail_[ch][i], juce::PathStrokeType(1.6f));
    }
    // Markers of this channel, positioned on its own trace/lane
    const uint64_t viewAbs = spanStart_[ch] + static_cast<uint64_t>(viewStart_);
    const float cy = r.getCentreY();
    for (size_t m = 0; m < markN_; ++m) {
      const auto& mk = marks_[m];
      if (mk.ch != ch)
        continue;
      if (mk.pos < viewAbs ||
          mk.pos >= viewAbs + static_cast<uint64_t>(viewLen_))
        continue;
      const int p = static_cast<int>(mk.pos - viewAbs);
      const float x = r.getX() + (static_cast<float>(p) + 0.5f) * r.getWidth() /
                                     static_cast<float>(viewLen_);
      const float v = span_[ch][static_cast<size_t>(viewStart_ + p)];
      const float y = cy - v * scale;
      g.setColour(mk.kind == 0 ? extColour(ch) : kScopeMarkFall);
      g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
      g.setColour(kScopeBg.withAlpha(0.85f));
      g.fillEllipse(x - 1.2f, y - 1.2f, 2.4f, 2.4f);
    }
    g.restoreState();
  }

  // Legend chips, top-right of the trace area (L left of R, as convention)
  for (int ch = 0; ch < 2; ++ch) {
    if (!channelActive(ch) || spanN_[ch] == 0)
      continue;
    const int pos = stereo_ ? ch : 1;  // single chip hugs the right edge
    const float x =
        full.getRight() - 22.0f - 26.0f * static_cast<float>(1 - pos);
    g.setColour(traceColour(ch));
    g.fillRoundedRectangle(x, full.getY() + 3.0f, 12.0f, 12.0f, 3.0f);
    g.setColour(kScopeBg);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.drawText(ch == 0 ? "L" : "R", juce::roundToInt(x),
               juce::roundToInt(full.getY()) + 3, 12, 12,
               juce::Justification::centred, false);
  }
  g.setFont(smallFont);

  // Readout: visible window in samples / ms / window-Hz
  if (!readoutRect_.isEmpty()) {
    juce::String txt = juce::String(viewLen_) + " spl";
    if (sr > 0.0)
      txt += "  |  " + juce::String(1000.0 * viewLen_ / sr, 1) + " ms  |  " +
             juce::String(sr / viewLen_, 1) + " Hz";
    g.setColour(kScopeText);
    g.drawText(txt, readoutRect_.toNearestInt(),
               juce::Justification::centredLeft);
  }
}

}  // namespace audio_plugin