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
      ) {
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

  // Preallocate analysis scratch buffers once
  extrema_.clear();
  extrema_.reserve(static_cast<size_t>(kWindowSize));
  order_.resize(kWindowSize);
  parent_.resize(kWindowSize);
  active_.resize(kWindowSize);

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

void PluginProcessor::findExtrema(const float* window) {
  extrema_.clear();

  // Window range used to normalise persistence
  float mn = window[0], mx = window[0];
  for (int i = 1; i < kWindowSize; ++i) {
    mn = std::min(mn, window[i]);
    mx = std::max(mx, window[i]);
  }
  const float range = mx - mn;
  if (range < 1e-9f)
    return;  // silence / DC: nothing to keep

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
  const float thresholdAbs = kThreshold * range;

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

      // A component born at a window edge has its true pairing partner outside
      // the window, so the measured persistence underestimates the real one.
      // Treat such pairs as infinitely persistent: keep the maximum.
      const bool edgeBorn = (dying == 0) || (dying == kWindowSize - 1);

      if (edgeBorn || persistence >= thresholdAbs) {
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

  // Optional Savitzky-Golay pre-smoothing of the section interior. Anchors
  // stay raw so seams remain click-free; padding reflects the section around
  // its anchors (zero slope at the anchor, no leakage across seams).
  // Sections shorter than the kernel span are left raw.
  const bool smooth = kSmoothMix > 0.0f && (b - a) >= 2 * kSmoothHalf;
  const auto tapped = [window, a, b](int j) {
    if (j < a)
      j = 2 * a - j;  // reflect around the left anchor
    else if (j > b)
      j = 2 * b - j;  // reflect around the right anchor
    return window[j];
  };

  for (int i = a + 1; i < b; ++i) {
    float x = window[i];
    if (smooth) {
      float acc = 0.0f;
      for (int k = -kSmoothHalf; k <= kSmoothHalf; ++k)
        acc += kSmoothCoeffs[k + kSmoothHalf] * tapped(i + k);
      x += kSmoothMix * (acc - x);  // lerp(raw, smoothed, mix)
    }
    float t = (x - va) / span;
    // Clipping! Theoretically this will never clip because of topology.
    t = juce::jlimit(0.0f, 1.0f, t);
    const float shaped = std::pow(t, kDeformPower);
    const float mixed = (1.0f - kDrive) * t + kDrive * shaped;
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

  // Step 1: append incoming samples into per-channel input buffers
  for (int ch = 0; ch < numChannels; ++ch) {
    const float* in = buffer.getReadPointer(ch);
    inputBuffer[ch].insert(inputBuffer[ch].end(), in, in + numSamples);
  }

  // Step 2: while a full analysis window is available, run ONE analysis pass
  // per channel, cut the next zone with it and append the processed content
  // to that channel's output FIFO. Zones tile the stream contiguously per
  // channel
  while (inputBuffer[0].size() >= static_cast<size_t>(kWindowSize)) {
    for (int ch = 0; ch < numChannels; ++ch) {
      findExtrema(inputBuffer[ch].data());

      const int zoneEnd = findZoneEnd();      // in [kHopSize, kWindowSize - 1]
      const int zoneStart = prevZoneEnd[ch];  // in [0, kHopSize - 1]
      const int zoneLen = zoneEnd - zoneStart;  // in [1, kWindowSize - 1]

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

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
  // You should use this method to store your parameters in the memory block.
  // You could do that either as raw data, or use the XML or ValueTree classes
  // as intermediaries to make it easy to save and load complex data.
  juce::ignoreUnused(destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
  // You should use this method to restore your parameters from this memory
  // block, whose contents will have been created by the getStateInformation()
  // call.
  juce::ignoreUnused(data, sizeInBytes);
}
}  // namespace audio_plugin

// This creates new instances of the plugin.
// This function definition must be in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new audio_plugin::PluginProcessor();
}
