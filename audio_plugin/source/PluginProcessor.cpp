namespace audio_plugin {
PluginProcessor::PluginProcessor()
    : AudioProcessor(
          BusesProperties()
#if !JUCE_IS_MIDI_EFFECT
#if !JUCE_IS_SYNTH
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
              ),
      apvts(*this,
            nullptr,
            juce::Identifier("Toposhaper"),
            createParameterLayout()) {
  thresholdPtr = apvts.getRawParameterValue("threshold");
  drivePtr = apvts.getRawParameterValue("drive");
  smoothMixPtr = apvts.getRawParameterValue("smooth_mix");
  osPtr = apvts.getRawParameterValue("os_factor");
  smoothModePtr = apvts.getRawParameterValue("smooth_mode");
  warpSlot_.publish(warpCurve_);
  // Per-parameter registration: there is no processor-wide hook for
  // parameter gestures, each AudioProcessorParameter owns its listeners.
  for (auto* p : getParameters())
    p->addListener(this);
}

PluginProcessor::~PluginProcessor() {
  // Before apvts (and its parameters) die: no dangling raw pointers
  // in parameter listener lists.
  for (auto* p : getParameters())
    p->removeListener(this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
PluginProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"threshold", 1}, "Threshold", 0.0f, 1.0f, 0.1f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"drive", 1}, "Drive", 0.0f, 1.0f, 1.0f));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"smooth_mix", 1}, "Smooth Mix", 0.0f, 1.0f, 0.0f));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"smooth_mode", 1}, "Smooth Mode",
      juce::StringArray{"SG", "PAVA"}, 0));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"os_factor", 1}, "Oversampling",
      juce::StringArray{"OFF", "X2", "X4", "X8"}, 1,
      // Quality switch: travels in state/presets and shows in generic UI,
      // but stays out of automation lanes and MIDI learn.
      juce::AudioParameterChoiceAttributes().withAutomatable(false)));
  return layout;
}

const juce::String PluginProcessor::getName() const {
  return JUCE_PLUGIN_NAME;
}

bool PluginProcessor::acceptsMidi() const {
#if JUCE_NEEDS_MIDI_INPUT
  return true;
#else
  return false;
#endif
}

bool PluginProcessor::producesMidi() const {
#if JUCE_NEEDS_MIDI_OUTPUT
  return true;
#else
  return false;
#endif
}

bool PluginProcessor::isMidiEffect() const {
#if JUCE_IS_MIDI_EFFECT
  return true;
#else
  return false;
#endif
}

double PluginProcessor::getTailLengthSeconds() const {
  return 0.0;
}

int PluginProcessor::getNumPrograms() {
  return 1;  // NB: some hosts don't cope very well if you tell them there are 0
             // programs, so this should be at least 1, even if you're not
             // really implementing programs.
}

int PluginProcessor::getCurrentProgram() {
  return 0;
}

void PluginProcessor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}

const juce::String PluginProcessor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}

void PluginProcessor::changeProgramName(int index,
                                        const juce::String& newName) {
  juce::ignoreUnused(index, newName);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  juce::ignoreUnused(sampleRate);
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    inputBuffer[ch].clear();
    inputBuffer[ch].reserve(static_cast<size_t>(kWindowSize) +
                            static_cast<size_t>(samplesPerBlock));
    // Prefill the output FIFO with silence (it is the reported latency)
    outputBuffer[ch].assign(static_cast<size_t>(kWindowSize), 0.0f);
    outputBuffer[ch].reserve(static_cast<size_t>(2 * kWindowSize) +
                             static_cast<size_t>(samplesPerBlock));
    prevZoneEnd[ch] = 0;
  }
  // Coherent stream restart: the buffers/FIFO above discard pending input
  // and inject fresh latency silence, so EVERY stream cursor must restart
  // with them. Otherwise marker positions (inputBase_ + idx + latency) and
  // the scope/input ring positions (scopeWrite_/inputWrite_) drift apart by
  // a constant offset - the "markers in wrong places after a sample-rate
  // change" bug. Resetting together also clears stale old-rate scope data
  // and markers in one shot.
  inputBase_ = 0;
  scopeWrite_.store(0, std::memory_order_release);
  scopeWriteR_.store(0, std::memory_order_release);
  inputWrite_.store(0, std::memory_order_release);
  inputWriteR_.store(0, std::memory_order_release);
  markerWrite_.store(0, std::memory_order_release);
  windowSilent_ = false;
  // Preallocate analysis scratch buffers once
  extrema_.clear();
  extrema_.reserve(static_cast<size_t>(kWindowSize));
  order_.resize(kWindowSize);
  parent_.resize(kWindowSize);
  active_.resize(kWindowSize);
  osS_.reserve(static_cast<size_t>(kWindowSize) + 2);
  osA_.reserve(static_cast<size_t>(8 * kWindowSize) + 1);
  osB_.reserve(static_cast<size_t>(4 * kWindowSize) + 1);
  pava_.reserve(static_cast<size_t>(kWindowSize));
  bakeWarpLut(warpSnap_, warpLut_.data(), kWarpLutSize);
  setLatencySamples(kWindowSize);
}

void PluginProcessor::releaseResources() {
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    inputBuffer[ch].clear();
    inputBuffer[ch].shrink_to_fit();
    outputBuffer[ch].clear();
    outputBuffer[ch].shrink_to_fit();
    prevZoneEnd[ch] = 0;
  }
  extrema_.clear();
  order_.clear();
  parent_.clear();
  active_.clear();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
#if JUCE_IS_MIDI_EFFECT
  juce::ignoreUnused(layouts);
  return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  // This checks if the input layout matches the output layout
#if !JUCE_IS_SYNTH
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}

int PluginProcessor::findRoot(int i) {
  // Iterative find with path halving
  while (parent_[i] != i) {
    parent_[i] = parent_[parent_[i]];
    i = parent_[i];
  }
  return i;
}

// True if the run from the window edge to idx holds no interior local
// minimum: only then is the edge sample an honest one-sided stand-in for a
// partner that lives outside the window. If an interior min exists, the
// merge maximum's true pairing partner is inside, the measured persistence
// is honest, and the threshold must judge it.
static bool edgeRunMonotone(const float* w, int n, int edge, int idx) {
  if (edge == 0) {
    for (int i = 1; i < idx; ++i)
      if (w[i] < w[i - 1] && w[i] <= w[i + 1])
        return false;
    return true;
  }
  for (int i = n - 2; i > idx; --i)
    if (w[i] < w[i + 1] && w[i] <= w[i - 1])
      return false;
  return true;
}

void PluginProcessor::findExtrema(const float* window) {
  extrema_.clear();

  // Window range used to normalise persistence
  float mn = window[0], mx = window[0];
  for (int i = 1; i < kWindowSize; ++i) {
    mn = std::min(mn, window[i]);
    mx = std::max(mx, window[i]);
  }
  const float range = mx - mn;
  windowSilent_ = range < 1e-9f;
  if (windowSilent_)
    return;  // silence / DC: nothing to keep, nothing to mark

  // Sweep sample indices by ascending value (ties by index -> deterministic).
  // Union-find flooding: components are born at minima and die at maxima.
  for (int i = 0; i < kWindowSize; ++i)
    order_[i] = i;
  std::sort(order_.begin(), order_.end(), [window](int p, int q) {
    if (window[p] != window[q])
      return window[p] < window[q];
    return p < q;
  });

  std::fill(active_.begin(), active_.end(), static_cast<uint8_t>(0));
  const float thresholdAbs = thresholdPtr->load() * range;

  for (int oi = 0; oi < kWindowSize; ++oi) {
    const int idx = order_[oi];
    active_[idx] = 1;
    parent_[idx] = idx;

    const bool leftActive = idx > 0 && active_[idx - 1];
    const bool rightActive = idx < kWindowSize - 1 && active_[idx + 1];

    if (!leftActive && !rightActive)
      continue;  // a new component is born at a local minimum

    if (leftActive && rightActive) {
      const int rL = findRoot(idx - 1);
      const int rR = findRoot(idx + 1);
      int survivor = rL, dying = rR;
      if (window[survivor] > window[dying])
        std::swap(survivor, dying);
      const float persistence = window[idx] - window[dying];

      const bool edgeBorn = (dying == 0) || (dying == kWindowSize - 1);
      const bool keep =
          persistence >= thresholdAbs ||
          (edgeBorn && edgeRunMonotone(window, kWindowSize, dying, idx));
      if (keep) {
        extrema_.push_back(dying);
        extrema_.push_back(idx);
      }
      parent_[dying] = survivor;
      parent_[idx] = survivor;
    } else {
      // One active neighbour: extend that component
      parent_[idx] = findRoot(leftActive ? idx - 1 : idx + 1);
    }
  }

  // The global minimum never dies: infinite persistence, always kept
  extrema_.push_back(findRoot(0));

  std::sort(extrema_.begin(), extrema_.end());

  // Extrema at the window edges are one-sided and unverifiable: drop them
  if (!extrema_.empty() && extrema_.front() == 0)
    extrema_.erase(extrema_.begin());
  if (!extrema_.empty() && extrema_.back() == kWindowSize - 1)
    extrema_.pop_back();
}

int PluginProcessor::findZoneEnd() {
  // kept is sorted ascending; first kept extremum in the second half
  for (const int idx : extrema_)
    if (idx >= kHopSize)
      return idx;
  return kWindowSize - 1;  // fallback: the zone runs to the end of the window
}

// Savitzky-Golay smoothing kernel; symmetric; coefficients sum to 1.
static constexpr int kSmoothHalf = 7;
static constexpr int kSmoothLen = 2 * kSmoothHalf + 1;  // = 15 taps
static constexpr float kSmoothCoeffs[kSmoothLen] = {
    -78.0f / 1105.0f,  // tap i-7
    -13.0f / 1105.0f,  // tap i-6
    42.0f / 1105.0f,   // tap i-5
    87.0f / 1105.0f,   // tap i-4
    122.0f / 1105.0f,  // tap i-3
    147.0f / 1105.0f,  // tap i-2
    162.0f / 1105.0f,  // tap i-1
    167.0f / 1105.0f,  // tap i (center)
    162.0f / 1105.0f,  // tap i+1
    147.0f / 1105.0f,  // tap i+2
    122.0f / 1105.0f,  // tap i+3
    87.0f / 1105.0f,   // tap i+4
    42.0f / 1105.0f,   // tap i+5
    -13.0f / 1105.0f,  // tap i+6
    -78.0f / 1105.0f,  // tap i+7
};

// Median of three: eliminates isolated spikes, preserves monotonicity.
static void median3(float* x, int n) {
  if (n < 3)
    return;
  float a = x[0];
  for (int i = 1; i < n - 1; ++i) {
    const float b = x[i], c = x[i + 1];
    const float m = std::max(std::min(a, b), std::min(std::max(a, b), c));
    a = b;  // the original value rolls into the next window
    x[i] = m;
  }
}

// PAVA (Pool Adjacent Violators Algorithm): nearest monotonic approximation
// (L2). dir = +1 for increase, -1 for decrease. Plateau = average of merged
// violators.
static void isotonicMono(float* x, int n, float dir) {
  if (n < 2)
    return;
  struct Blk {
    double sum = 0.0;
    int cnt = 0;
    double mean() const { return sum / (double)cnt; }
  };
  juce::Array<Blk> st;
  st.ensureStorageAllocated(64);
  for (int i = 0; i < n; ++i) {
    Blk b{(double)x[i] * (double)dir, 1};
    while (st.size() > 0 && st.getLast().mean() > b.mean()) {
      const Blk l = st.getLast();
      st.removeLast();
      b.sum += l.sum;
      b.cnt += l.cnt;
    }
    st.add(b);
  }
  int i = 0;
  for (const auto& b : st) {
    const float m = (float)b.mean() * dir;
    for (int k = 0; k < b.cnt; ++k)
      x[i++] = m;
  }
}

// Symmetric moving average: smooths out PAVA steps.
// Monotony preserves: y'[i+1]-y'[i] = (y[i+h+1]-y[i-h])/w >= 0.
static void movAvg(float* x, int n, int w) {
  const int h = w / 2;
  const std::vector<float> src(x, x + n);
  for (int i = 0; i < n; ++i) {
    double acc = 0.0;
    int cnt = 0;
    for (int k = -h; k <= h; ++k) {
      acc += src[juce::jlimit(0, n - 1, i + k)];
      ++cnt;
    }
    x[i] = (float)(acc / (double)cnt);
  }
}

size_t PluginProcessor::readScopeSpan(float* dst,
                                      size_t n,
                                      uint64_t& startAbs,
                                      int ch) const {
  const auto& ring = (ch == 1) ? scopeRingR_ : scopeRing_;
  const auto& cur = (ch == 1) ? scopeWriteR_ : scopeWrite_;
  if (n > kMaxSpan)
    n = kMaxSpan;
  const uint64_t w = cur.load(std::memory_order_acquire);
  if (w < static_cast<uint64_t>(kScopeMargin) + n) {
    startAbs = 0;
    return 0;
  }
  const uint64_t start = w - kScopeMargin - n;
  for (size_t i = 0; i < n; ++i)
    dst[i] = ring[static_cast<size_t>((start + i) & kScopeMask)];
  startAbs = start;
  return n;
}

size_t PluginProcessor::readInputSpanAt(float* dst,
                                        size_t n,
                                        uint64_t startAbs,
                                        int ch) const {
  const auto& ring = (ch == 1) ? inputRingR_ : inputRing_;
  const auto& cur = (ch == 1) ? inputWriteR_ : inputWrite_;
  const uint64_t w = cur.load(std::memory_order_acquire);
  if (w < static_cast<uint64_t>(kScopeMargin) + n)
    return 0;
  const uint64_t avail = w - kScopeMargin;
  if (startAbs + n > avail || n > kMaxSpan)
    return 0;
  for (size_t i = 0; i < n; ++i)
    dst[i] = ring[static_cast<size_t>((startAbs + i) & kScopeMask)];
  return n;
}

size_t PluginProcessor::readMarkerSpan(ScopeMarker* dst, size_t n) const {
  const uint64_t w = markerWrite_.load(std::memory_order_acquire);
  const uint64_t end = w;  // entries < w are final: the release store at the
                           // end of each write burst orders them; the next
                           // burst writes [w, w+burst), which cannot wrap into
                           // [w-n, w) because n + burst << ring size
  const uint64_t cap = static_cast<uint64_t>(1) << kMarkerBits;
  uint64_t start = end > n ? end - n : 0;
  if (end - start > cap - 64)
    start = end - (cap - 64);
  for (uint64_t i = start; i < end; ++i)
    dst[i - start] = markerRing_[static_cast<size_t>(i & kMarkerMask)];
  return static_cast<size_t>(end - start);
}

float PluginProcessor::warpEval(float t) const {
  const float pos =
      juce::jlimit(0.0f, 1.0f, t) * static_cast<float>(kWarpLutSize - 1);
  const auto i0 = static_cast<size_t>(pos);
  const auto i1 = std::min(i0 + 1, static_cast<size_t>(kWarpLutSize - 1));
  const float f = pos - static_cast<float>(i0);
  return warpLut_[i0] * (1.0f - f) + warpLut_[i1] * f;
}

int PluginProcessor::osFactor() const {
  static constexpr int kMap[4] = {1, 2, 4, 8};
  const int idx = juce::jlimit(
      0, 3, static_cast<int>(osPtr->load(std::memory_order_relaxed)));
  return kMap[idx];
}

int PluginProcessor::smoothMode() const {
  return juce::jlimit(
      0, 1, static_cast<int>(smoothModePtr->load(std::memory_order_relaxed)));
}

void PluginProcessor::setPresetIdentity(const juce::String& name,
                                        const juce::String& source) {
  presetName_ = name;
  presetSource_ = source;
}

void PluginProcessor::setPresetDirty(bool dirty) {
  presetDirty_ = dirty;
}

void PluginProcessor::deformSection(const float* window,
                                    int a,
                                    int b,
                                    float* out,
                                    int outOffset) {
  // Deform samples [a, b) between fixed anchors window[a] and window[b];
  // results go to out[i - outOffset]. Anchor a itself is copied unchanged.
  const float va = window[a];
  const float vb = window[b];
  out[a - outOffset] = va;
  if (b - a <= 1)
    return;

  const float span = vb - va;
  if (std::abs(span) < 1e-12f) {
    for (int i = a + 1; i < b; ++i)
      out[i - outOffset] = va;
    return;
  }

  const float smoothMix = smoothMixPtr->load();
  const float drive = drivePtr->load();

  // Optional Savitzky-Golay pre-smoothing of the section interior. Anchors
  // stay raw so seams remain click-free; padding reflects the section around
  // its anchors (zero slope at the anchor, no leakage across seams).
  // Sections shorter than the kernel span are left raw.
  const int mode = smoothMode();
  const bool smooth =
      mode == 0 && smoothMix > 0.0f && (b - a) >= 2 * kSmoothHalf;
  // PAVA: monotonize the section interior once, prior to warping. The SG
  // branch above wins when both are requested by a stale parameter combo.
  // Do not touch the anchors (seams stay click-free); clamp the interior to
  // [min(va, vb), max(va, vb)] - clamping a monotone sequence preserves
  // monotonicity and respects the anchors.
  const bool pava = mode == 1 && !smooth && smoothMix > 0.0f && (b - a) >= 2;
  if (pava) {
    const int L = b - a - 1;
    pava_.resize(static_cast<size_t>(L));
    for (int i = 0; i < L; ++i)
      pava_[static_cast<size_t>(i)] = window[a + 1 + i];
    median3(pava_.data(), L);
    isotonicMono(pava_.data(), L, vb >= va ? 1.0f : -1.0f);
    const float lo = std::min(va, vb), hi = std::max(va, vb);
    for (auto& v : pava_)
      v = juce::jlimit(lo, hi, v);
    movAvg(pava_.data(), L, 7);
  }
  const auto tapped = [window, a, b](int j) {
    if (j < a)
      j = 2 * a - j;  // reflect around the left anchor
    else if (j > b)
      j = 2 * b - j;  // reflect around the right anchor
    return window[j];
  };

  // Oversampling of the warp nonlinearity: engaged
  // whenever the OS factor > 1 and drive > 0 (drive = 0 keeps bit-exact
  // passthrough). The smoothed interior is linearly interpolated onto an
  // Fx grid, warped there, then decimated back with cascaded zero-phase
  // triangle filters (one stage per factor of two). Anchors stay raw, so
  // seams remain click-free; latency and tiling are untouched.
  const int osF = osFactor();
  if (osF > 1 && drive > 0.0f) {
    const int L = b - a;
    osS_.resize(static_cast<size_t>(L) + 2);
    for (int j = 0; j <= L; ++j) {
      float x = window[a + j];
      if (pava) {
        if (j >= 1 && j < L)  // anchors j=0 and j=L are raw
          x += smoothMix * (pava_[static_cast<size_t>(j - 1)] - x);
      } else if (smooth) {
        float acc = 0.0f;
        for (int k = -kSmoothHalf; k <= kSmoothHalf; ++k)
          acc += kSmoothCoeffs[k + kSmoothHalf] * tapped(a + j + k);
        x += smoothMix * (acc - x);
      }
      osS_[static_cast<size_t>(j)] = x;
    }
    osS_[static_cast<size_t>(L) + 1] = osS_[static_cast<size_t>(L)];  // pad
    const auto warped = [&](float v) {
      // Clipping! Theoretically this will never clip because of topology.
      const float t = juce::jlimit(0.0f, 1.0f, (v - va) / span);
      return va + ((1.0f - drive) * t + drive * warpEval(t)) * span;
    };
    const size_t M = static_cast<size_t>(L) * static_cast<size_t>(osF) + 1;
    osA_.resize(M);
    for (size_t m = 0; m < M; ++m) {
      const size_t j = m / static_cast<size_t>(osF);
      const float f = static_cast<float>(m % static_cast<size_t>(osF)) /
                      static_cast<float>(osF);
      osA_[m] = warped(osS_[j] * (1.0f - f) + osS_[j + 1] * f);
    }
    // Cascaded zero-phase triangle decimation: Fx -> Fx/2 -> ... -> 1x
    size_t cur = M;
    for (int stage = osF; stage > 1; stage /= 2) {
      const size_t nxt = (cur - 1) / 2 + 1;
      osB_.resize(nxt);
      for (size_t k = 0; k < nxt; ++k) {
        const size_t c = 2 * k;
        const size_t lm = (c == 0) ? 1 : c - 1;           // reflect at start
        const size_t rp = (c + 1 < cur) ? c + 1 : c - 1;  // reflect at end
        osB_[k] = 0.25f * osA_[lm] + 0.5f * osA_[c] + 0.25f * osA_[rp];
      }
      std::swap(osA_, osB_);
      cur = nxt;
    }
    for (int j = 1; j < L; ++j)
      out[a + j - outOffset] = osA_[static_cast<size_t>(j)];
    return;
  }

  for (int i = a + 1; i < b; ++i) {
    float x = window[i];
    if (pava) {
      x += smoothMix * (pava_[static_cast<size_t>(i - a - 1)] - x);
    } else if (smooth) {
      float acc = 0.0f;
      for (int k = -kSmoothHalf; k <= kSmoothHalf; ++k)
        acc += kSmoothCoeffs[k + kSmoothHalf] * tapped(i + k);
      x += smoothMix * (acc - x);  // lerp(raw, smoothed, mix)
    }
    float t = (x - va) / span;
    // Clipping! Theoretically this will never clip because of topology.
    t = juce::jlimit(0.0f, 1.0f, t);
    const float shaped = warpEval(t);
    const float mixed = (1.0f - drive) * t + drive * shaped;
    out[i - outOffset] = va + mixed * span;
  }
}

void PluginProcessor::processZone(const float* window,
                                  int zoneStart,
                                  int zoneEnd,
                                  float* out) {
  int anchor = zoneStart;
  for (const int idx : extrema_) {
    if (idx <= zoneStart)
      continue;
    if (idx >= zoneEnd)
      break;
    deformSection(window, anchor, idx, out, zoneStart);
    anchor = idx;
  }
  deformSection(window, anchor, zoneEnd, out, zoneStart);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages) {
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;

  const int numChannels = std::min(getTotalNumInputChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();

  // Pick up warp curve edits without locks; rebake LUT only on change
  WarpSnapshot snap;
  if (warpSlot_.take(snap, warpSnapSeq_)) {
    warpSnap_ = snap;
    bakeWarpLut(warpSnap_, warpLut_.data(), kWarpLutSize);
  }

  // Step 1: append incoming samples into per-channel input buffers
  for (int ch = 0; ch < numChannels; ++ch) {
    const float* in = buffer.getReadPointer(ch);
    inputBuffer[ch].insert(inputBuffer[ch].end(), in, in + numSamples);
    if (ch == 0 || ch == 1) {  // raw input rings for ghost traces
      auto& ring = (ch == 0) ? inputRing_ : inputRingR_;
      auto& cur = (ch == 0) ? inputWrite_ : inputWriteR_;
      const uint64_t w = cur.load(std::memory_order_relaxed);
      for (int i = 0; i < numSamples; ++i)
        ring[(w + static_cast<uint64_t>(i)) & kScopeMask] = in[i];
      cur.store(w + static_cast<uint64_t>(numSamples),
                std::memory_order_release);
    }
  }

  // Step 2: while a full analysis window is available, run ONE analysis pass
  // per channel, cut the next zone with it and append the processed content
  // to that channel's output FIFO. Zones tile the stream contiguously per
  // channel
  while (inputBuffer[0].size() >= static_cast<size_t>(kWindowSize)) {
    for (int ch = 0; ch < numChannels; ++ch) {
      const uint64_t winAbs = inputBase_;
      findExtrema(inputBuffer[ch].data());

      const int zoneEnd = findZoneEnd();      // in [kHopSize, kWindowSize - 1]
      const int zoneStart = prevZoneEnd[ch];  // in [0, kHopSize - 1]
      const int zoneLen = zoneEnd - zoneStart;  // in [1, kWindowSize - 1]

      if (ch == 0 || ch == 1) {  // publish exactly the anchors/seams this
                                 // pass's zone decomposition actually used:
                                 // markers can never orphan from the wave
        uint64_t mw = markerWrite_.load(std::memory_order_relaxed);
        const auto push = [&mw, this, winAbs, ch](int idx, int kind) {
          markerRing_[mw & kMarkerMask] = ScopeMarker{
              winAbs + static_cast<uint64_t>(idx) + kWindowSize, kind, ch};
          ++mw;
        };
        if (zoneStart == 0)
          push(0, 0);  // very first anchor of the stream
        for (const int idx : extrema_) {
          if (idx <= zoneStart)
            continue;
          if (idx >= zoneEnd)
            break;
          push(idx, 0);  // kept extremum that became an anchor
        }
        if (!(zoneEnd == kWindowSize - 1 && windowSilent_))
          push(zoneEnd,
               (zoneEnd == kWindowSize - 1) ? 1 : 0);  // seam / fallback seam
        markerWrite_.store(mw, std::memory_order_release);
      }

      const size_t oldSize = outputBuffer[ch].size();
      outputBuffer[ch].resize(oldSize + static_cast<size_t>(zoneLen));
      processZone(inputBuffer[ch].data(), zoneStart, zoneEnd,
                  outputBuffer[ch].data() + oldSize);

      prevZoneEnd[ch] = zoneEnd - kHopSize;
    }

    // Shift input buffers forward: next analysis window starts kHopSize later
    for (int ch = 0; ch < numChannels; ++ch) {
      inputBuffer[ch].erase(inputBuffer[ch].begin(),
                            inputBuffer[ch].begin() + kHopSize);
    }

    inputBase_ += kHopSize;
  }

  // Step 3: pull numSamples from the head of each output FIFO
  for (int ch = 0; ch < numChannels; ++ch) {
    float* out = buffer.getWritePointer(ch);
    const int toOutput =
        std::min(numSamples, static_cast<int>(outputBuffer[ch].size()));

    for (int i = 0; i < toOutput; ++i)
      out[i] = outputBuffer[ch][static_cast<size_t>(i)];

    // Unreachable in practice (FIFO size before emit is provably
    // >= numSamples + 1); kept so the output buffer is always fully written
    for (int i = toOutput; i < numSamples; ++i)
      out[i] = 0.0f;

    if ((ch == 0 || ch == 1) &&
        toOutput > 0) {  // feed the scope display rings (per channel), UI reads
                         // it lock-free
      auto& ring = (ch == 0) ? scopeRing_ : scopeRingR_;
      auto& cur = (ch == 0) ? scopeWrite_ : scopeWriteR_;
      const uint64_t w = cur.load(std::memory_order_relaxed);
      for (int i = 0; i < toOutput; ++i)
        ring[(w + static_cast<uint64_t>(i)) & kScopeMask] = out[i];
      cur.store(w + static_cast<uint64_t>(toOutput), std::memory_order_release);
    }

    if (toOutput > 0)
      outputBuffer[ch].erase(outputBuffer[ch].begin(),
                             outputBuffer[ch].begin() + toOutput);
  }

  // Clear extra output channels (in case of mono->stereo etc.)
  for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
    buffer.clear(ch, 0, numSamples);
}

bool PluginProcessor::hasEditor() const {
  return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
  return new PluginEditor(*this);
}

juce::ValueTree PluginProcessor::buildStateTree(bool includeUi) {
  auto state = apvts.copyState();
  state.appendChild(warpCurve_.toValueTree(), nullptr);
  juce::ValueTree meta("PRESET");
  meta.setProperty("name", presetName_, nullptr);
  meta.setProperty("source", presetSource_, nullptr);
  meta.setProperty("dirty", presetDirty_, nullptr);
  state.appendChild(meta, nullptr);
  if (includeUi) {  // view recall: only for host sessions, never for presets
    juce::ValueTree ui("UI");
    ui.setProperty("viewLen", uiState_.viewLen, nullptr);
    ui.setProperty("vExp", uiState_.vExp, nullptr);
    ui.setProperty("chan", uiState_.chanMode, nullptr);
    ui.setProperty("track", uiState_.track, nullptr);
    state.appendChild(ui, nullptr);
  }
  return state;
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
  if (std::unique_ptr<juce::XmlElement> xml = buildStateTree(true).createXml())
    copyXmlToBinary(*xml, destData);
}

juce::String PluginProcessor::copyStateAsXml() {
  if (std::unique_ptr<juce::XmlElement> xml = buildStateTree(false).createXml())
    return xml->toString();
  return {};
}

bool PluginProcessor::applyStateTree(juce::ValueTree tree) {
  // Foreign or garbage XML: touch nothing (warp included), report failure
  // so callers can voice it. Parseable != ours.
  if (!tree.isValid() || !tree.hasType(apvts.state.getType()))
    return false;
  auto warp = tree.getChildWithName(WarpCurve::kStateTag);
  if (warp.isValid()) {
    warpCurve_.fromValueTree(warp);
    tree.removeChild(warp, nullptr);
    warpSlot_.publish(warpCurve_);
  }
  auto ui = tree.getChildWithName("UI");
  if (ui.isValid()) {
    uiState_.viewLen = juce::jlimit(
        256, 32768, static_cast<int>(ui.getProperty("viewLen", 2048)));
    uiState_.vExp = juce::jlimit(
        -2.0f, 5.0f, static_cast<float>(ui.getProperty("vExp", 0.0)));
    uiState_.chanMode =
        juce::jlimit(0, 3, static_cast<int>(ui.getProperty("chan", 0)));
    uiState_.track = static_cast<bool>(ui.getProperty("track", true));
    tree.removeChild(ui, nullptr);
    ++uiState_.seq;
  }
  auto meta = tree.getChildWithName("PRESET");
  if (meta.isValid()) {
    presetName_ = meta.getProperty("name", "Init").toString();
    presetSource_ = meta.getProperty("source", "Basic").toString();
    presetDirty_ = static_cast<bool>(meta.getProperty("dirty", false));
    tree.removeChild(meta, nullptr);
  } else {
    presetName_ = "Init";  // old sessions / preset blobs: no identity
    presetSource_ = "Basic";
    presetDirty_ = false;
  }
  apvts.replaceState(tree);
  return true;
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml == nullptr)
    return;
  applyStateTree(juce::ValueTree::fromXml(*xml));
  undoStack_.clear();  // host-driven state (project load, host undo)
  redoStack_.clear();  // invalidates our history: snapshots no longer apply
}

bool PluginProcessor::applyStateXml(const juce::String& xmlText) {
  if (std::unique_ptr<juce::XmlElement> xml =
          juce::XmlDocument::parse(xmlText)) {
    auto tree = juce::ValueTree::fromXml(*xml);
    // Presets must never yank the view: strip UI even if an old/foreign
    // blob carries it, so apply-path is sound-only by construction.
    if (auto ui = tree.getChildWithName("UI"); ui.isValid())
      tree.removeChild(ui, nullptr);
    if (auto meta = tree.getChildWithName("PRESET"); meta.isValid())
      tree.removeChild(meta, nullptr);
    return applyStateTree(tree);
  }
  return false;
}

juce::File PluginProcessor::userPresetDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("Toposhaper")
          .getChildFile("Presets");
  dir.createDirectory();
  return dir;
}

void PluginProcessor::parameterValueChanged(int, float) {
  // Value changes are observed elsewhere (editor's APVTS listener for the
  // dirty flag); the undo stack only cares about gesture boundaries.
}

void PluginProcessor::parameterGestureChanged(int, bool gestureIsStarting) {
  if (!gestureIsStarting)
    return;
  // SliderAttachment gestures come from the message thread; host
  // automation can gesture from anywhere else. The undo stack is
  // message-thread state (ValueTrees, vectors) - ignore foreign threads.
  if (juce::MessageManager::existsAndIsCurrentThread())
    pushUndo();
}

void PluginProcessor::pushUndo() {
  pushUndoTree(buildStateTree(false));
}

void PluginProcessor::pushUndoTree(juce::ValueTree pre) {
  undoStack_.push_back(std::move(pre));
  if (undoStack_.size() > kHistoryDepth)
    undoStack_.erase(undoStack_.begin());
  redoStack_.clear();  // new branch of history kills the old future
}

bool PluginProcessor::undo() {
  if (undoStack_.empty())
    return false;
  redoStack_.push_back(buildStateTree(false));
  if (redoStack_.size() > kHistoryDepth)
    redoStack_.erase(redoStack_.begin());
  restoreSnapshot(std::move(undoStack_.back()));
  undoStack_.pop_back();
  return true;
}

bool PluginProcessor::redo() {
  if (redoStack_.empty())
    return false;
  undoStack_.push_back(buildStateTree(false));
  if (undoStack_.size() > kHistoryDepth)
    undoStack_.erase(undoStack_.begin());
  restoreSnapshot(std::move(redoStack_.back()));
  redoStack_.pop_back();
  return true;
}

void PluginProcessor::restoreSnapshot(juce::ValueTree t) {
  std::vector<float> before;
  before.reserve(static_cast<size_t>(getParameters().size()));
  for (auto* p : getParameters())
    before.push_back(p->getValue());
  applyStateTree(std::move(t));  // warp + meta + replaceState; no UI child
  size_t i = 0;
  for (auto* p : getParameters()) {
    const float v = p->getValue();
    if (std::abs(v - before[i++]) > 1.0e-6f)
      p->setValueNotifyingHost(v);  // keep host UI/automation honest
  }
}
}  // namespace audio_plugin

// This creates new instances of the plugin.
// This function definition must be in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new audio_plugin::PluginProcessor();
}
