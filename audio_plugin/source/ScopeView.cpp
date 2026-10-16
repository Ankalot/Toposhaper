#include <cmath>

namespace audio_plugin {
namespace {
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

// Detent centres under the printed L / R / L&R / L|R labels,
// as fractions of the groove width. Re-measure from the case art.
constexpr float kDetentX[4] = {0.128f, 0.285f, 0.53f, 0.84f};

constexpr float kPreTriggerFrac = 0.15f;

// Ask for more than the ring can hold: readScopeSpan clamps the request to
// its capacity, so the frozen record depth becomes a constant (ring
// capacity) instead of a function of the H-zoom at the freeze moment.
constexpr size_t kFreezeDepth = size_t(1) << 20;
}  // namespace

ScopeView::ScopeView(PluginProcessor& p) : proc_(p) {
  trackBtn_.setClickingTogglesState(true);
  trackBtn_.setToggleState(proc_.uiState().track, juce::dontSendNotification);
  trackBtn_.setTooltip(
      "Waveform tracking: ON = triggered (stable picture), OFF = scroll "
      "(true-time).");
  trackBtn_.onClick = [this] {
    if (freeze_)
      return;
    trailN_[0] = trailN_[1] = 0;
    repaint();
  };

  freezeBtn_.onToggle = [this] {
    freeze_ = freezeBtn_.isFrozen();
    if (freeze_) {
      freezeCapture();
      freezeRight_ = viewStart_ + viewLen_;
      trailN_[0] = std::min(trailN_[0], 1);
      trailN_[1] = std::min(trailN_[1], 1);
    } else {
      trailN_[0] = trailN_[1] = 0;
    }
    repaint();
  };
  addAndMakeVisible(freezeBtn_);

  chanBtn_.onModeChange_ = [this](int mode) {
    if (mode == chanMode_)
      return;
    chanMode_ = mode;
    if (!freeze_)
      trailN_[0] = trailN_[1] = 0;
    repaint();
  };
  chanBtn_.setTooltip(
      "Display channel switch: L, R, L&R (overlay) or L|R (split).");

  // Passive stereo indicator chip
  smChip_.setTooltip(
      "The indicator is active when the signal is stereo (the side "
      "signal (L-R) peaks above " +
      juce::String(SmChip::kWidthDb, 0) +
      " dB relative to the mid (L+R) and above " +
      juce::String(SmChip::kFloorDbfs, 0) + " dBFS absolute).");

  vZoom_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  vZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  vZoom_.setRange(-2.0, 5.0, 0.01);  // gain = 2^v: 0.25x .. 32x
  vZoom_.setValue(proc_.uiState().vExp, juce::dontSendNotification);
  vZoom_.setTooltip(
      "Vertical zoom (amplitude), shared by both channels. It can also be "
      "controlled using Ctrl/Shift + MWheel on the display.");
  vZoom_.setScrollWheelEnabled(false);
  vZoom_.onValueChange = [this] { repaint(); };

  hZoom_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  hZoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  hZoom_.setRange(0.0, 1000.0, 1.0);  // 256..32768 samples, log
  hZoom_.setValue(hFromLen(proc_.uiState().viewLen),
                  juce::dontSendNotification);
  hZoom_.setTooltip(
      "Horizontal zoom: visible window 256 .. 32768 samples. It can also be "
      "controlled using MWheel on the display.");
  hZoom_.setScrollWheelEnabled(false);
  hZoom_.onValueChange = [this] {
    if (!freeze_)
      trailN_[0] = trailN_[1] = 0;
    repaint();
  };

  addAndMakeVisible(trackBtn_);
  trackBtn_.setAlpha(0.0f);
  addAndMakeVisible(chanBtn_);
  chanBtn_.setAlpha(0.0f);
  addAndMakeVisible(vZoom_);
  vZoom_.setAlpha(0.0f);
  addAndMakeVisible(hZoom_);
  hZoom_.setAlpha(0.0f);
  addAndMakeVisible(smChip_);

  trackBtn_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  chanBtn_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  vZoom_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  hZoom_.setMouseCursor(juce::MouseCursor::PointingHandCursor);

  caseImg = juce::ImageCache::getFromMemory(BinaryData::oscilloscope_png,
                                            BinaryData::oscilloscope_pngSize);

  chanMode_ = proc_.uiState().chanMode;
  uiSeq_ = proc_.uiState().seq;
  setOpaque(false);
  startTimerHz(60);
}

void ScopeView::SmChip::setLit(bool lit) {
  if (lit != lit_) {
    lit_ = lit;
    repaint();
  }
}

void ScopeView::SmChip::paint(juce::Graphics& g) {
  const auto r = getLocalBounds().toFloat().reduced(1.0f);
  g.setColour(lit_ ? ui::accentR : ui::chipIdle);
  g.fillEllipse(r);
}

void ScopeView::SmChip::setTooltip(const juce::String& newTooltip) {
  tooltipText = newTooltip;
}

juce::String ScopeView::SmChip::getTooltip() {
  return tooltipText;
}

void ScopeView::ModeSwitch::pick(const juce::MouseEvent& e) {
  const auto r = getLocalBounds().toFloat();
  const float t =
      juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
  int best = 0;
  float bestD = 1.0e9f;
  for (int i = 0; i < 4; ++i) {  // nearest printed label wins
    const float d = std::abs(t - kDetentX[i]);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (onModeChange_)
    onModeChange_(best);
}

void ScopeView::ModeSwitch::mouseDown(const juce::MouseEvent& e) {
  dragged_ = false;
  pick(e);  // the press lands on the detent under the cursor at once
}

void ScopeView::ModeSwitch::mouseDrag(const juce::MouseEvent& e) {
  if (std::abs(e.getDistanceFromDragStartX()) < 4.0f &&
      std::abs(e.getDistanceFromDragStartY()) < 4.0f)
    return;  // jitter this small is still just a press
  dragged_ = true;
  pick(e);
}

void ScopeView::ModeSwitch::mouseUp(const juce::MouseEvent&) {
  dragged_ = false;
  // No TextButton::mouseUp on purpose: the onClick cycle is dead,
  // a release must never step away from where the user pressed.
}

juce::Rectangle<float> ScopeView::traceRect() const {
  const auto r = getLocalBounds().toFloat();
  return {14.0f, 40.0f, 517.0f, 300.0f};
}

juce::Rectangle<float> ScopeView::laneRect(int ch) const {
  const auto full = traceRect();
  if (chanMode_ != 3 || !stereo_)
    return full;
  const float half = full.getHeight() * 0.5f;
  if (ch == 0)
    return full.withHeight(half - ui::laneGap);
  return full.withY(full.getY() + half + ui::laneGap)
      .withHeight(half - ui::laneGap);
}

int ScopeView::primaryChannel() const {
  return (chanMode_ == 1 && hasR_) ? 1 : 0;
}

bool ScopeView::channelActive(int ch) const {
  return stereo_ ? true : ch == prim_;
}

int ScopeView::lenFromH(double t) const {
  return juce::jlimit(
      256, 32768,
      juce::roundToInt(256.0 * std::pow(128.0, (1000.0 - t) / 1000.0)));
}
double ScopeView::hFromLen(int len) const {
  return 1000.0 -
         1000.0 * std::log(static_cast<double>(len) / 256.0) / std::log(128.0);
}

void ScopeView::resized() {
  trackBtn_.setBounds(juce::Rectangle<int>(565, 111, 27, 27));  // LOCK
  freezeBtn_.setBounds(juce::Rectangle<int>(590, 60, 21, 21));  // FREEZE
  smChip_.setBounds(juce::Rectangle<int>(588, 152, 21, 21));    // CHS Indicator
  chanBtn_.setBounds(juce::Rectangle<int>(536, 203, 83, 18));   // CHS Groove
  vZoom_.setBounds(juce::Rectangle<int>(567, 254, 27, 27));     // V-ZOOM
  hZoom_.setBounds(juce::Rectangle<int>(567, 303, 27, 27));     // H-ZOOM
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
    hZoom_.setValue(hZoom_.getValue() + d * 30.0, juce::sendNotificationSync);
}

void ScopeView::timerCallback() {
  const auto& s = proc_.uiState();
  if (s.seq != uiSeq_) {  // state restored while the editor was open
    uiSeq_ = s.seq;
    applyUiState();
  }
  refresh();
  repaint();
}

float ScopeView::SmChip::dbToRatio(float db) {
  return std::pow(10.0f, db / 20.0f);
}

void ScopeView::updateSmChip() {
  // Two-part gate; both thresholds live in SmChip as dB constants:
  // 1) scale-free width: side peak vs mid peak ratio,
  // 2) absolute floor: near-silent passages stay grey.
  const float widthRatio = SmChip::dbToRatio(SmChip::kWidthDb);
  const float floorLin = SmChip::dbToRatio(SmChip::kFloorDbfs);
  bool lit = false;
  if (hasR_) {
    const size_t stop = std::min(std::min(spanN_[0], spanN_[1]),
                                 static_cast<size_t>(viewStart_ + viewLen_));
    float sidePeak = 0.0f, midPeak = 0.0f;
    for (size_t i = static_cast<size_t>(viewStart_); i < stop; ++i) {
      const float l = span_[0][i], r = span_[1][i];
      sidePeak = std::max(sidePeak, std::abs(l - r));
      midPeak = std::max(midPeak, std::abs(l + r));
    }
    lit = sidePeak > floorLin && sidePeak > widthRatio * midPeak;
  }
  smChip_.setLit(lit);
}

void ScopeView::pushUiState() {
  auto& s = proc_.uiState();
  s.viewLen = lenFromH(hZoom_.getValue());
  s.vExp = static_cast<float>(vZoom_.getValue());
  s.chanMode = chanMode_;
  s.track = trackBtn_.getToggleState();
}

void ScopeView::applyUiState() {
  const auto& s = proc_.uiState();
  hZoom_.setValue(hFromLen(s.viewLen), juce::dontSendNotification);
  vZoom_.setValue(s.vExp, juce::dontSendNotification);
  chanMode_ = s.chanMode;
  trackBtn_.setToggleState(s.track, juce::dontSendNotification);
  freezeBtn_.setFrozen(false);  // session restore always unfreezes
  freeze_ = false;
  trailN_[0] = trailN_[1] = 0;
}

void ScopeView::freezeCapture() {
  const size_t keepN[2] = {spanN_[0], spanN_[1]};
  size_t got[2] = {0, 0};
  uint64_t st[2] = {0, 0};

  // Deep read of the output history.
  for (int ch = 0; ch < 2; ++ch) {
    span_[ch].resize(kFreezeDepth);
    got[ch] = proc_.readScopeSpan(span_[ch].data(), kFreezeDepth, st[ch], ch);
    if (got[ch] > 0) {
      spanN_[ch] = got[ch];
      spanStart_[ch] = st[ch];
      span_[ch].resize(got[ch]);
    } else {
      span_[ch].resize(keepN[ch]);  // resize up preserved the old samples
    }
  }
  hasR_ = spanN_[1] > 0 && spanStart_[0] == spanStart_[1];
  stereo_ = hasR_ && chanMode_ >= 2;

  // Span indices shifted back by the extra depth: keep the same absolute
  // view window on screen (no visual jump on the click).
  if (spanN_[prim_] > keepN[prim_])
    viewStart_ += static_cast<int>(spanN_[prim_] - keepN[prim_]);

  // Trigger reference lives in old span coordinates: drop it.
  prevVs_ = -1;
  trigBuf_.clear();

  // Markers: re-read deep so anchor dots cover the whole frozen record.
  // Reader clamps to cap - 64 internally, so ring size is the ceiling;
  // refresh() shrinks the vector back to 8192 on the next live frame.
  marks_.resize(16384);
  markN_ = proc_.readMarkerSpan(marks_.data(), marks_.size());
  marks_.resize(markN_);

  // Ghost input: same 1:1 alignment as refresh() (start = spanStart - W),
  // just deeper. Short/failed read disables the ghost rather than misaligns.
  const uint64_t W = proc_.getLatencySamples();
  for (int ch = 0; ch < 2; ++ch) {
    if (channelActive(ch) && W > 0 && spanStart_[ch] >= W && spanN_[ch] > 0) {
      inSpan_[ch].resize(spanN_[ch]);
      const size_t g = proc_.readInputSpanAt(inSpan_[ch].data(), spanN_[ch],
                                             spanStart_[ch] - W, ch);
      inN_[ch] = (g == spanN_[ch]) ? g : 0;
      if (inN_[ch] == 0)
        inSpan_[ch].resize(0);
    } else {
      inN_[ch] = 0;
      inSpan_[ch].resize(0);
    }
  }
}

void ScopeView::refresh() {
  viewLen_ = lenFromH(hZoom_.getValue());
  vZoomGain_ = static_cast<float>(std::pow(2.0, vZoom_.getValue()));

  if (freeze_) {
    const int held = static_cast<int>(spanN_[prim_]);
    viewLen_ = std::min(viewLen_, std::min(held, freezeRight_));
    viewStart_ = freezeRight_ - viewLen_;  // right edge is fixed recording
    hZoom_.setValue(hFromLen(viewLen_), juce::dontSendNotification);
    const float scale = traceRect().getHeight() * 0.45f * vZoomGain_;
    for (int ch = 0; ch < 2; ++ch) {
      if (!channelActive(ch) || spanN_[ch] == 0) {
        trailN_[ch] = 0;
        continue;
      }
      buildPaths(ch, laneRect(ch), scale, true);
    }
    updateSmChip();
    pushUiState();
    return;
  }

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

  // Shared trigger: primary channel in single modes, L in stereo modes.
  // LOCK = newest gate crossing, refined by corrscope-style frame-to-frame
  // alignment: among the crossings near the predicted position, take the
  // one whose displayed window correlates best with the previous frame's
  // window. Chords and quasi-periodic waves hold still by continuity,
  // without any period estimation or grid.
  const bool tracking = trackBtn_.getToggleState();
  int vs = -1;
  locked_ = false;
  if (prim_ != prevPrim_) {  // reference belongs to another channel
    trigBuf_.clear();
    prevVs_ = -1;
    prevPrim_ = prim_;
  }
  if (tracking) {
    const int t = findTrigger(span_[prim_].data(), spanN_[prim_]);
    if (t >= 0) {
      vs = t - static_cast<int>(viewLen_ * kPreTriggerFrac);  // pre-trigger 15%
      if (vs < 0)
        vs = 0;
      locked_ = true;
      if (!trigBuf_.empty() && prevVs_ >= 0) {
        const uint64_t adv = spanStart_[prim_] >= prevSpanStart_
                                 ? spanStart_[prim_] - prevSpanStart_
                                 : 0;
        int pred = prevVs_ - static_cast<int>(adv);
        pred =
            juce::jlimit(0, static_cast<int>(spanN_[prim_]) - viewLen_, pred);
        const int aligned = findTriggerAligned(
            span_[prim_].data(), spanN_[prim_], pred, trigWidth_ / 2);
        if (aligned >= 0)
          vs = aligned;
      }
      storeTriggerRef(span_[prim_].data(), vs, spanN_[prim_]);
      prevVs_ = vs;
      prevSpanStart_ = spanStart_[prim_];
    } else {  // silence / DC: forget the reference
      trigBuf_.clear();
      prevVs_ = -1;
    }
  } else {
    trigBuf_.clear();
    prevVs_ = -1;
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

  updateSmChip();
  pushUiState();  // keep the processor mirror fresh for getStateInformation
}

int ScopeView::findTrigger(const float* s, size_t n) const {
  float peak = 0.0f;
  for (size_t i = 0; i < n; ++i)
    peak = std::max(peak, std::abs(s[i]));
  if (peak < 1.0e-4f)
    return -1;                      // silence / DC: nothing to lock onto
  const float gate = 0.05f * peak;  // schmitt hysteresis vs noise
  const int pre = static_cast<int>(viewLen_ * kPreTriggerFrac);
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

int ScopeView::findTriggerAligned(const float* s,
                                  size_t n,
                                  int center,
                                  int radius) const {
  if (trigBuf_.empty() || trigBufNorm_ < 1e-9f)
    return -1;
  const int W = trigWidth_;
  const int limit = static_cast<int>(n);
  float peak = 0.0f;
  for (size_t i = 0; i < n; ++i)
    peak = std::max(peak, std::abs(s[i]));
  if (peak < 1.0e-4f)
    return -1;
  const float gate = 0.05f * peak;
  const int pre = static_cast<int>(viewLen_ * kPreTriggerFrac);
  const int lo = std::max(0, center - radius);
  const int hi = std::min(limit - viewLen_, center + radius);
  bool high = false;
  float bestCorr = 0.3f;  // acceptance floor: below = re-lock to newest
  int best = -1;
  for (int i = 1; i < limit; ++i) {
    if (!high && s[i] >= gate) {
      high = true;
      const int v = i - pre;
      if (v >= lo && v <= hi) {
        double dot = 0.0, sn = 0.0;
        for (int j = 0; j < W; ++j) {
          const float x = s[static_cast<size_t>(v + j)];
          dot += static_cast<double>(x) * trigBuf_[static_cast<size_t>(j)];
          sn += static_cast<double>(x) * x;
        }
        if (sn > 1e-9) {
          const float c = static_cast<float>(
              dot / std::sqrt(sn * static_cast<double>(trigBufNorm_)));
          if (c > bestCorr) {
            bestCorr = c;
            best = v;
          }
        }
      }
    } else if (high && s[i] <= -gate) {
      high = false;
    }
  }
  return best;
}

void ScopeView::storeTriggerRef(const float* s, int vs, size_t n) {
  const int W = std::min(viewLen_, 2048);
  if (W != trigWidth_ || vs < 0 || static_cast<size_t>(vs + W) > n) {
    trigBuf_.clear();  // view length changed or bad offset: resync next frame
    trigWidth_ = W;
    return;
  }
  trigBuf_.resize(static_cast<size_t>(W));
  double norm = 0.0;
  for (int i = 0; i < W; ++i) {
    const float v = s[static_cast<size_t>(vs + i)];
    trigBuf_[static_cast<size_t>(i)] = v;
    norm += static_cast<double>(v) * v;
  }
  trigBufNorm_ = static_cast<float>(norm);
  trigWidth_ = W;
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

  if (freeze_) {
    // Simply redraw the main track into the freeze area without shifting the
    // phosphor trail.
    trail_[ch][0] = out;
    if (trailN_[ch] == 0)
      trailN_[ch] = 1;
  } else if (trackBtn_.getToggleState() && locked_) {
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
  g.setColour(ui::zero);
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
      if (std::abs(y - cy) < ui::lineBoxH)
        continue;  // hugs the zero line: nothing to label there
      dbLines.add({y, db});
    }
  }
  std::sort(dbLines.begin(), dbLines.end(),
            [](const DbLine& a, const DbLine& b) { return a.y < b.y; });
  constexpr float kMinDbGap = 18.0f;  // = lineBoxH + 6: labels never collide
  float lastY = -1.0e9f;
  const auto smallFont =
      juce::Font(juce::FontOptions().withHeight(ui::fontSmall));
  g.setFont(smallFont);
  for (const auto& L : dbLines) {
    if (L.y - lastY < kMinDbGap)
      continue;
    lastY = L.y;
    g.setColour(L.db == 0 ? ui::zero : ui::grid);
    g.drawLine({r.getX(), L.y, r.getRight(), L.y});
    g.setColour(ui::text);
    const int textY = juce::roundToInt(L.y) - 12;
    const int minY = juce::roundToInt(r.getY()) + 20;
    const int maxY = juce::roundToInt(r.getBottom()) - 35;
    if (textY >= minY && textY <= maxY) {
      g.drawText(juce::String(L.db) + " dB", juce::roundToInt(r.getX()) + 2,
                 textY, 52, juce::roundToInt(ui::lineBoxH),
                 juce::Justification::left, false);
    }
  }
}

void ScopeView::paint(juce::Graphics& g) {
  const auto full = traceRect();
  if (full.isEmpty())
    return;
  g.saveState();
  g.reduceClipRegion(full.toNearestInt());

  const double sr = proc_.getSampleRate();
  const auto smallFont =
      juce::Font(juce::FontOptions().withHeight(ui::fontSmall));
  g.setFont(smallFont);
  const float scale = full.getHeight() * 0.45f * vZoomGain_;
  const bool split = stereo_ && chanMode_ == 3;

  // --- HUD line metrics: [L][R] + spl | ms | Hz, top right of the glass. ---
  juce::String infoTxt = juce::String(viewLen_) + " spl";
  if (sr > 0.0)
    infoTxt += "  |  " + juce::String(1000.0 * viewLen_ / sr, 1) + " ms  |  " +
               juce::String(sr / viewLen_, 1) + " Hz";
  const float infoTextW = static_cast<float>(
      juce::GlyphArrangement::getStringWidthInt(smallFont, infoTxt));
  int swatches = 0;
  for (int ch = 0; ch < 2; ++ch)
    if (channelActive(ch) && spanN_[ch] > 0)
      ++swatches;
  const float infoW =
      static_cast<float>(swatches) * (ui::chipSize + 4.0f) + 8.0f + infoTextW;
  const float infoLeft = full.getRight() - 10.0f - infoW;
  const juce::Rectangle<float> infoPlate(infoLeft - 4.0f, full.getY(),
                                         infoW + 10.0f, ui::lineBoxH + 4.0f);

  // Vertical grid
  const int div = niceDiv(viewLen_);
  for (int ago = 0; ago <= viewLen_; ago += div) {
    const float x =
        full.getRight() - static_cast<float>(ago) * full.getWidth() / viewLen_;
    if (x < full.getX() - 0.5f)
      break;
    g.setColour(ago == 0 ? ui::zero : ui::grid);
    g.drawLine({x, full.getY(), x, full.getBottom()});
    const bool inGutter = x < full.getX() + ui::gutterL;
    if (!inGutter) {
      if (x + 46.0f < infoLeft) {
        g.setColour(ui::text);
        g.drawText(juce::String(ago), juce::roundToInt(x) + 2,
                   juce::roundToInt(full.getY()) + 2, 44,
                   juce::roundToInt(ui::lineBoxH), juce::Justification::left,
                   false);
      }
      if (sr > 0.0 && ago > 0) {
        g.setColour(ui::text);
        g.drawText(
            juce::String(1000.0 * ago / sr, 2) + "ms", juce::roundToInt(x) + 2,
            juce::roundToInt(full.getBottom()) - 14, 72,
            juce::roundToInt(ui::lineBoxH), juce::Justification::left, false);
      }
    }
  }

  // Horizontal dB grid
  drawDbGrid(g, laneRect(0), scale);
  if (split)
    drawDbGrid(g, laneRect(1), scale);

  // Traces, trails, markers
  for (int ch = 1; ch >= 0; --ch) {
    if (!channelActive(ch) || spanN_[ch] == 0)
      continue;
    const auto r = laneRect(ch);
    g.saveState();
    g.reduceClipRegion(r.toNearestInt());
    if (inN_[ch] > 0) {
      g.setColour(ui::fillOf(ch));
      g.fillPath(deltaPath_[ch]);
      g.setColour(ui::ghost);
      g.strokePath(inPath_[ch], juce::PathStrokeType(ui::ghostW));
    }
    for (int i = std::min(trailN_[ch], 3) - 1; i >= 0; --i) {
      const float alpha = (i == 0) ? 1.0f : (i == 1 ? 0.35f : 0.15f);
      g.setColour(ui::traceOf(ch).withAlpha(alpha));
      g.strokePath(trail_[ch][i], juce::PathStrokeType(ui::thinW));
    }
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
      const int idx = viewStart_ + p;
      if (idx < 0 || idx >= static_cast<int>(spanN_[ch]))
        continue;
      const float v = span_[ch][static_cast<size_t>(idx)];
      const float x = r.getX() + (static_cast<float>(p) + 0.5f) * r.getWidth() /
                                     static_cast<float>(viewLen_);
      const float y = cy - v * scale;
      g.setColour(mk.kind == 0 ? ui::markOf(ch) : ui::markFall);
      g.fillEllipse(x - ui::dotR, y - ui::dotR, ui::dotR * 2.0f,
                    ui::dotR * 2.0f);
      g.setColour(ui::scope.withAlpha(0.85f));
      g.fillEllipse(x - ui::dotHoleR, y - ui::dotHoleR, ui::dotHoleR * 2.0f,
                    ui::dotHoleR * 2.0f);
    }
    g.restoreState();
  }

  // --- HUD: panel + channel swatches + single-line readout ---
  g.setColour(ui::scope);
  g.fillRoundedRectangle(infoPlate, 4.0f);
  float cursor_ = infoLeft;
  g.setFont(smallFont);
  for (int ch = 0; ch < 2; ++ch) {
    if (!channelActive(ch) || spanN_[ch] == 0)
      continue;
    g.setColour(ui::traceOf(ch));
    g.fillRoundedRectangle(cursor_, full.getY() + 3.0f, ui::chipSize,
                           ui::chipSize, ui::cornerSm);
    g.setColour(ui::scope);
    g.setFont(juce::Font(juce::FontOptions().withHeight(ui::fontTiny)));
    g.drawText(ch == 0 ? "L" : "R", juce::roundToInt(cursor_),
               juce::roundToInt(full.getY()) + 3,
               juce::roundToInt(ui::chipSize), juce::roundToInt(ui::chipSize),
               juce::Justification::centred, false);
    g.setFont(smallFont);
    cursor_ += ui::chipSize + 4.0f;
  }
  g.setColour(ui::text);
  g.drawText(infoTxt, juce::roundToInt(cursor_ + 8.0f),
             juce::roundToInt(full.getY() + 3.0f),
             juce::roundToInt(infoTextW + 4.0f), juce::roundToInt(ui::lineBoxH),
             juce::Justification::centredLeft, false);

  g.restoreState();

  // Oscilloscope overlay
  g.drawImage(caseImg, getLocalBounds().toFloat());

  // --- Notches over the printed rotary knobs ---
  auto knobTick = [&g](float cx, float cy, float t) {
    // 7 hours (t=0) -> 12 hours (t=0.5) -> 5 hours (t=1)
    const float a = juce::MathConstants<float>::pi *
                    (-0.75f + 1.5f * juce::jlimit(0.0f, 1.0f, t));
    const juce::Point<float> d{std::sin(a), -std::cos(a)};
    g.setColour(ui::black);
    const float radius = 12.5f;
    g.drawLine(juce::Line<float>(cx, cy, cx + d.x * radius, cy + d.y * radius),
               2.0f);
  };
  knobTick(956.0f - 376.0f, 570.0f - 303.0f,
           static_cast<float>((vZoom_.getValue() - vZoom_.getMinimum()) /
                              (vZoom_.getMaximum() - vZoom_.getMinimum())));
  knobTick(956.0f - 376.0f, 619.0f - 303.0f,
           static_cast<float>(hZoom_.getValue() / 1000.0));

  // --- LOCK: two-position toggle knob (straight lever + hub), as in art ---
  const auto lockC = trackBtn_.getBounds().toFloat().getCentre();
  const bool on = trackBtn_.getToggleState();
  // ON aims at the printed "ON" label (10:30), OFF at "OFF" (1:30).
  const float ang = juce::MathConstants<float>::pi * (on ? -0.25f : 0.25f);
  const juce::Point<float> d{std::sin(ang), -std::cos(ang)};
  g.setColour(ui::black);  // same ink as the printed labels
  // Constant-width lever with a flat tip: no taper, no arrowhead
  g.drawLine({lockC.x, lockC.y, lockC.x + d.x * 22.0f, lockC.y + d.y * 22.0f},
             7.0f);
  // Hub disc last so the lever root reads as a clean pivot
  constexpr float kHubR = 8.0f;
  g.fillEllipse(lockC.x - kHubR, lockC.y - kHubR, kHubR * 2.0f, kHubR * 2.0f);

  // --- CHS groove: four-detent notch (black disc + stem, as in art) ---
  {
    const auto gr = chanBtn_.getBounds().toFloat();
    const int mode = juce::jlimit(0, 3, chanMode_);
    const float cx = gr.getX() + kDetentX[mode] * gr.getWidth();
    const float cy = gr.getCentreY();
    g.setColour(ui::black);
    // Stem: from above the groove top down into the disc
    g.fillRect(cx - 2.0f, gr.getY() - 6.0f, 4.0f,
               (cy - 3.0f) - (gr.getY() - 6.0f));
    // Disc: the knob body sitting in the groove
    g.fillEllipse(cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
  }
}
}  // namespace audio_plugin