#include <cmath>

namespace audio_plugin {
namespace {
// Scope-prefixed: unity build merges anonymous namespaces across .cpp files
const juce::Colour kScopeBg{0xff101218};
const juce::Colour kScopeFrame{0x808a93a6};
const juce::Colour kScopeGrid{0x14ffffff};
const juce::Colour kScopeZero{0x33ffffff};
const juce::Colour kScopeTrace{0xff4f7dff};
const juce::Colour kScopeGhost{0x38e8ecf5};
const juce::Colour kScopeText{0x99e8ecf5};
const juce::Colour kScopeMarkExt{0xff5aa2ff};   // kept persistent extrema
const juce::Colour kScopeMarkFall{0xffe5484d};  // fallback seam (no extremum)
const juce::Colour kScopeDelta{0x1f4f7dff};
constexpr int kScopeStripH = 24;

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
    trailN_ = 0;
    updateModeLabel();
    repaint();
  };
  addAndMakeVisible(trackBtn_);

  vZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
  vZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  vZoom_.setRange(-2.0, 5.0, 0.01);  // gain = 2^v: 0.25x .. 32x
  vZoom_.setValue(0.0, juce::dontSendNotification);
  vZoom_.setTooltip("Vertical zoom (amplitude): 0.25x .. 32x");
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
    trailN_ = 0;
    repaint();
  };
  addAndMakeVisible(hZoom_);

  setOpaque(true);
  startTimerHz(60);
}

juce::Rectangle<float> ScopeView::traceRect() const {
  auto r = getLocalBounds().toFloat();
  return r.withTop(r.getY() + kScopeStripH).reduced(2.0f);
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
  trackBtn_.setBounds(strip.removeFromLeft(118));
  strip.removeFromLeft(6);
  vZoom_.setBounds(strip.removeFromLeft(90));
  strip.removeFromLeft(6);
  hZoom_.setBounds(strip.removeFromLeft(150));
  strip.removeFromLeft(10);
  readoutRect_ = strip.toFloat();
  trailN_ = 0;
}

void ScopeView::mouseWheelMove(const juce::MouseEvent& e,
                               const juce::MouseWheelDetails& wheel) {
  // Proportional zoom: every wheel event changes the view by an amount
  // proportional to its own delta, so a single tick always zooms no matter
  // how the driver quantises notches (high-res mice send ~0.25-0.35 deltaY
  // per tick, coarse mice ~1.0, trackpads send tiny fractions). The clamp
  // only tames momentum spikes.
  const float d = juce::jlimit(-2.0f, 2.0f, wheel.deltaY);  // >0 = up = in
  if (std::abs(d) < 1.0e-4f)
    return;
  if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isShiftDown())
    vZoom_.setValue(vZoom_.getValue() + d * 0.35,  // gain x~1.08 per 0.3-tick
                    juce::sendNotificationSync);
  else
    hZoom_.setValue(hZoom_.getValue() - d * 30.0,  // ~4-5% window per 0.3-tick
                    juce::sendNotificationSync);
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

void ScopeView::refresh() {
  viewLen_ = lenFromH(hZoom_.getValue());
  vZoomGain_ = static_cast<float>(std::pow(2.0, vZoom_.getValue()));

  const size_t want = static_cast<size_t>(viewLen_) * 2 + 4096;
  span_.resize(want);
  uint64_t start = 0;
  spanN_ = proc_.readScopeSpan(span_.data(), want, start);
  if (spanN_ < static_cast<size_t>(viewLen_)) {
    spanN_ = 0;
    updateModeLabel();
    return;
  }
  spanStart_ = start;

  // Raw input aligned through the constant latency: ring pos p <-> input p-W
  const uint64_t W = static_cast<uint64_t>(proc_.getLatencySamples());
  inSpan_.resize(want);
  inN_ = (W > 0 && spanStart_ >= W)
             ? proc_.readInputSpanAt(inSpan_.data(), want, spanStart_ - W)
             : 0;

  marks_.resize(1024);
  markN_ = proc_.readMarkerSpan(marks_.data(), marks_.size());

  const bool tracking = trackBtn_.getToggleState();
  int vs = -1;
  locked_ = false;
  if (tracking) {
    const int t = findTrigger();
    if (t >= 0) {
      vs = t - viewLen_ * 15 / 100;  // pre-trigger 15%
      if (vs < 0)
        vs = 0;
      locked_ = true;
    }
  }
  if (vs < 0)
    vs = static_cast<int>(spanN_) - viewLen_;  // FREE / SCROLL: end-anchored
  viewStart_ = vs;

  buildPaths(traceRect());
  updateModeLabel();
}

int ScopeView::findTrigger() const {
  float peak = 0.0f;
  for (size_t i = 0; i < spanN_; ++i)
    peak = std::max(peak, std::abs(span_[i]));
  if (peak < 1.0e-4f)
    return -1;                      // silence / DC: nothing to lock onto
  const float gate = 0.05f * peak;  // schmitt hysteresis vs noise
  const int pre = viewLen_ * 15 / 100;
  const int limit = static_cast<int>(spanN_);
  bool high = false;
  int best = -1;  // newest LOW->HIGH whose whole view still fits the span
  for (int i = 1; i < limit; ++i) {
    if (!high && span_[i] >= gate) {
      high = true;
      if (i - pre + viewLen_ <= limit)
        best = i;
    } else if (high && span_[i] <= -gate) {
      high = false;
    }
  }
  return best;
}

void ScopeView::buildPaths(const juce::Rectangle<float>& r) {
  if (r.isEmpty()) {
    trailN_ = 0;
    return;
  }
  const int w = std::max(1, juce::roundToInt(r.getWidth()));
  const float cy = r.getCentreY();
  const float scale = r.getHeight() * 0.45f * vZoomGain_;

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
    for (int i = p0; i < q1 && i < static_cast<int>(spanN_); ++i) {
      mn = std::min(mn, span_[static_cast<size_t>(i)]);
      mx = std::max(mx, span_[static_cast<size_t>(i)]);
    }
    if (mn <= mx) {
      if (firstOut) {
        out.startNewSubPath(x, cy - mx * scale);
        firstOut = false;
      } else
        out.lineTo(x, cy - mx * scale);
      out.lineTo(x, cy - mn * scale);
    }

    // Ghost input: output ring pos q corresponds to input ring pos q - W,
    // and inSpan_[j] sits at input ring pos (spanStart_ - W) + j, so the
    // column index maps 1:1 (j == p). The old +W offset shifted the ghost
    // by a full latency and starved the last W samples of the view.
    if (inN_ > 0) {
      const int j0 = p0;
      const int j1 = q1;
      float imn = 1.0e9f, imx = -1.0e9f;
      for (int j = j0; j < j1 && j < static_cast<int>(inN_); ++j) {
        imn = std::min(imn, inSpan_[static_cast<size_t>(j)]);
        imx = std::max(imx, inSpan_[static_cast<size_t>(j)]);
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
  deltaPath_ = delta;
  inPath_ = in;
  outPath_ = out;

  if (trackBtn_.getToggleState() && locked_) {
    trail_[0] = out;  // triggered: frames overlay, no trail
    trailN_ = 1;
  } else {
    trail_[2] = trail_[1];  // scroll: phosphor trail
    trail_[1] = trail_[0];
    trail_[0] = out;
    trailN_ = std::min(3, trailN_ + 1);
  }
}

void ScopeView::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat();
  g.fillAll(kScopeBg);
  g.setColour(kScopeFrame);
  g.drawRect(bounds, 1.5f);

  const auto r = traceRect();
  if (r.isEmpty())
    return;
  const double sr = proc_.getSampleRate();
  const auto smallFont = juce::Font(juce::FontOptions().withHeight(10.0f));
  g.setFont(smallFont);

  const float cy = r.getCentreY();
  const float scale = r.getHeight() * 0.45f * vZoomGain_;

  // Vertical grid: right edge = now; top = samples ago, bottom = ms ago
  const int div = niceDiv(viewLen_);
  for (int ago = 0; ago <= viewLen_; ago += div) {
    const float x =
        r.getRight() - static_cast<float>(ago) * r.getWidth() / viewLen_;
    if (x < r.getX() - 0.5f)
      break;
    g.setColour(ago == 0 ? kScopeZero : kScopeGrid);
    g.drawLine({x, r.getY(), x, r.getBottom()});
    const bool inGutter = x < r.getX() + 58.0f;  // left column reserved for dB
    if (!inGutter) {
      g.setColour(kScopeText);
      g.drawText(juce::String(ago), juce::roundToInt(x) + 2,
                 juce::roundToInt(r.getY()) + 2, 64, 12,
                 juce::Justification::left, false);
      if (sr > 0.0 && ago > 0)
        g.drawText(juce::String(1000.0 * ago / sr, 2) + "ms",
                   juce::roundToInt(x) + 2,
                   juce::roundToInt(r.getBottom()) - 14, 72, 12,
                   juce::Justification::left, false);
    }
  }

  // Centre zero line
  g.setColour(kScopeZero);
  g.drawLine({r.getX(), cy, r.getRight(), cy});

  // Horizontal dB grid (0 dBFS = amplitude 1.0), both polarities. At low
  // zoom deep-dB lines collapse onto the zero line and onto each other,
  // so candidates closer than kMinDbGap to an already kept line (or to
  // the zero line) are skipped together with their labels.
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

  // Traces, ghost and markers: clipped to the trace rect so high vertical
  // zoom can never paint over the control strip
  g.saveState();
  g.reduceClipRegion(r.toNearestInt());
  if (inN_ > 0) {
    g.setColour(kScopeDelta);
    g.fillPath(deltaPath_);
    g.setColour(kScopeGhost);
    g.strokePath(inPath_, juce::PathStrokeType(1.2f));
  }
  for (int i = std::min(trailN_, 3) - 1; i >= 0; --i) {
    const float alpha = (i == 0) ? 1.0f : (i == 1 ? 0.35f : 0.15f);
    g.setColour(kScopeTrace.withAlpha(alpha));
    g.strokePath(trail_[i], juce::PathStrokeType(1.6f));
  }
  // Markers: kept extrema and fallback seams, positioned on the output trace
  const uint64_t viewAbs = spanStart_ + static_cast<uint64_t>(viewStart_);
  for (size_t m = 0; m < markN_; ++m) {
    const auto& mk = marks_[m];
    if (mk.pos < viewAbs || mk.pos >= viewAbs + static_cast<uint64_t>(viewLen_))
      continue;
    const int p = static_cast<int>(mk.pos - viewAbs);
    const float x = r.getX() + (static_cast<float>(p) + 0.5f) * r.getWidth() /
                                   static_cast<float>(viewLen_);
    const float v = span_[static_cast<size_t>(viewStart_ + p)];
    const float y = cy - v * scale;
    g.setColour(mk.kind == 0 ? kScopeMarkExt : kScopeMarkFall);
    g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    g.setColour(kScopeBg.withAlpha(0.85f));
    g.fillEllipse(x - 1.2f, y - 1.2f, 2.4f, 2.4f);
  }
  g.restoreState();

  // Readout: visible window in samples / ms / window-Hz
  if (!readoutRect_.isEmpty()) {
    juce::String txt = juce::String(viewLen_) + " spl";
    if (sr > 0.0)
      txt += "  |  " + juce::String(1000.0 * viewLen_ / sr, 1) + " ms  |  " +
             juce::String(sr / viewLen_, 1) + " Hz";
    g.setColour(kScopeText);
    g.setFont(smallFont);
    g.drawText(txt, readoutRect_.toNearestInt(),
               juce::Justification::centredLeft);
  }
}

}  // namespace audio_plugin