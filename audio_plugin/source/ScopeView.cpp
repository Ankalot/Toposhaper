#include <cmath>

namespace audio_plugin {
namespace {
// Palette (kept local until the global UI palette lands)
const juce::Colour kScopeBg{0xff101218};
const juce::Colour kScopeFrame{0x808a93a6};
const juce::Colour kScopeGrid{0x14ffffff};
const juce::Colour kScopeZero{0x33ffffff};
const juce::Colour kScopeTrace{0xff4f7dff};
const juce::Colour kScopeText{0x99e8ecf5};
const juce::Colour kScopeLock{0xff35d38a};
const juce::Colour kScopeFree{0xffe5a43a};
const juce::Colour kScopeScroll{0xff9aa3b5};
constexpr int kStripH = 24;

// "Nice" 1/2/5 x 10^k division closest to viewLen / 6
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
      "OFF = scroll (true-time, phosphor trail)");
  trackBtn_.onClick = [this] {
    trailN_ = 0;
    repaint();
  };
  addAndMakeVisible(trackBtn_);

  vZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
  vZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  vZoom_.setRange(-2.0, 2.0, 0.01);  // gain = 2^v: 0.25x .. 4x
  vZoom_.setValue(0.0, juce::dontSendNotification);
  vZoom_.setTooltip("Vertical zoom (amplitude): 0.25x .. 4x");
  vZoom_.onValueChange = [this] { repaint(); };
  addAndMakeVisible(vZoom_);

  hZoom_.setSliderStyle(juce::Slider::LinearHorizontal);
  hZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  hZoom_.setRange(0.0, 1000.0, 1.0);  // 256 .. 32768 samples, log
  hZoom_.setValue(hFromLen(2048), juce::dontSendNotification);
  hZoom_.setTooltip("Horizontal zoom: visible window 256 .. 32768 samples");
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
  return r.withTop(r.getY() + kStripH).reduced(2.0f);
}

int ScopeView::lenFromH(double t) const {
  return juce::jlimit(256, 32768,
                      juce::roundToInt(256.0 * std::pow(128.0, t / 1000.0)));
}

double ScopeView::hFromLen(int len) const {
  return 1000.0 * std::log(static_cast<double>(len) / 256.0) / std::log(128.0);
}

void ScopeView::resized() {
  auto strip = getLocalBounds().removeFromTop(kStripH).reduced(2);
  trackBtn_.setBounds(strip.removeFromLeft(64));
  strip.removeFromLeft(6);
  vZoom_.setBounds(strip.removeFromLeft(90));
  strip.removeFromLeft(6);
  hZoom_.setBounds(strip.removeFromLeft(150));
  strip.removeFromLeft(10);
  readoutRect_ = strip.toFloat();
  trailN_ = 0;
}

void ScopeView::timerCallback() {
  refresh();
  repaint();
}

void ScopeView::refresh() {
  viewLen_ = lenFromH(hZoom_.getValue());
  vZoomGain_ = static_cast<float>(std::pow(2.0, vZoom_.getValue()));

  const size_t want = static_cast<size_t>(viewLen_) * 2 + 4096;
  span_.resize(want);
  spanN_ = proc_.readScopeSpan(span_.data(), want);
  if (spanN_ < static_cast<size_t>(viewLen_)) {
    spanN_ = 0;
    return;
  }

  const bool tracking = trackBtn_.getToggleState();
  int start = -1;
  locked_ = false;
  if (tracking) {
    const int t = findTrigger();
    if (t >= 0) {
      start = t - viewLen_ * 15 / 100;  // pre-trigger 15%
      if (start < 0)
        start = 0;
      locked_ = true;
    }
  }
  if (start < 0)
    start = static_cast<int>(spanN_) - viewLen_;  // FREE / SCROLL: end-anchored
  viewStart_ = start;

  buildPath(traceRect());
}

int ScopeView::findTrigger() const {
  float peak = 0.0f;
  for (size_t i = 0; i < spanN_; ++i)
    peak = std::max(peak, std::abs(span_[i]));
  if (peak < 1.0e-4f)
    return -1;                      // silence / DC: nothing to lock onto
  const float gate = 0.05f * peak;  // schmitt hysteresis vs noise
  const int pre = viewLen_ * 15 / 100;
  bool high = false;
  int last = -1, prev = -1;
  for (size_t i = 1; i < spanN_; ++i) {
    if (!high && span_[i] >= gate) {
      high = true;
      prev = last;
      last = static_cast<int>(i);
    } else if (high && span_[i] <= -gate) {
      high = false;
    }
  }
  // the whole view must fit between the trigger and the span end
  const auto fits = [&](int t) {
    return t >= 0 && t - pre + viewLen_ <= static_cast<int>(spanN_);
  };
  if (fits(last))
    return last;
  if (fits(prev))
    return prev;
  return -1;
}

void ScopeView::buildPath(const juce::Rectangle<float>& r) {
  if (r.isEmpty()) {
    trailN_ = 0;
    return;
  }
  const int w = std::max(1, juce::roundToInt(r.getWidth()));
  const float cy = r.getCentreY();
  const float scale = r.getHeight() * 0.45f * vZoomGain_;  // +-1.0 at gain 1

  juce::Path p;
  bool first = true;
  for (int c = 0; c < w; ++c) {
    const int i0 =
        viewStart_ + static_cast<int>(static_cast<int64_t>(c) * viewLen_ / w);
    const int i1 = viewStart_ +
                   static_cast<int>(static_cast<int64_t>(c + 1) * viewLen_ / w);
    const int j1 = std::max(i0 + 1, i1);
    float mn = 1.0e9f, mx = -1.0e9f;
    for (int i = i0; i < j1 && i < static_cast<int>(spanN_); ++i) {
      mn = std::min(mn, span_[i]);
      mx = std::max(mx, span_[i]);
    }
    if (mn > mx)
      continue;
    const float x =
        r.getX() + (static_cast<float>(c) + 0.5f) * r.getWidth() / w;
    if (first) {
      p.startNewSubPath(x, cy - mx * scale);
      first = false;
    } else {
      p.lineTo(x, cy - mx * scale);
    }
    p.lineTo(x, cy - mn * scale);
  }

  if (trackBtn_.getToggleState() && locked_) {
    trail_[0] = p;  // triggered: frames overlay, no trail needed
    trailN_ = 1;
  } else {
    trail_[2] = trail_[1];  // scroll: phosphor trail of the last frames
    trail_[1] = trail_[0];
    trail_[0] = p;
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

  // Vertical grid: right edge = now; top labels = samples ago,
  // bottom labels = ms ago (Reaper-style rulers)
  const int div = niceDiv(viewLen_);
  g.setFont(smallFont);
  for (int ago = 0; ago <= viewLen_; ago += div) {
    const float x =
        r.getRight() - static_cast<float>(ago) * r.getWidth() / viewLen_;
    if (x < r.getX() - 0.5f)
      break;
    g.setColour(ago == 0 ? kScopeZero : kScopeGrid);
    g.drawLine({x, r.getY(), x, r.getBottom()});
    g.setColour(kScopeText);
    g.drawText(juce::String(ago), juce::roundToInt(x) + 2,
               juce::roundToInt(r.getY()) + 2, 64, 12,
               juce::Justification::left, false);
    if (sr > 0.0 && ago > 0)
      g.drawText(juce::String(1000.0 * ago / sr, 2) + "ms",
                 juce::roundToInt(x) + 2, juce::roundToInt(r.getBottom()) - 14,
                 72, 12, juce::Justification::left, false);
  }
  g.setColour(kScopeZero);
  g.drawLine({r.getX(), r.getCentreY(), r.getRight(), r.getCentreY()});

  // Trace: older trail frames dimmer (phosphor), newest full
  for (int i = std::min(trailN_, 3) - 1; i >= 0; --i) {
    const float a = (i == 0) ? 1.0f : (i == 1 ? 0.35f : 0.15f);
    g.setColour(kScopeTrace.withAlpha(a));
    g.strokePath(trail_[i], juce::PathStrokeType(1.6f));
  }

  // Mode indicator: LOCK / FREE / SCROLL
  const bool tracking = trackBtn_.getToggleState();
  const juce::String mode = !tracking ? "SCROLL" : (locked_ ? "LOCK" : "FREE");
  g.setColour(!tracking ? kScopeScroll : (locked_ ? kScopeLock : kScopeFree));
  g.setFont(smallFont);
  g.drawText(mode, juce::roundToInt(r.getRight()) - 44,
             juce::roundToInt(r.getY()) + 2, 42, 13,
             juce::Justification::centred, false);

  // Readout: visible window in samples / ms / "Hz of window"
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