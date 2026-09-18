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
  juce::ignoreUnused(sampleRate, samplesPerBlock);

  // Build periodic Hann window (sums to constant with 50% overlap)
  hannWindow.resize(kWindowSize);
  for (int i = 0; i < kWindowSize; ++i) {
    hannWindow[i] =
        0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i /
                                kWindowSize));
  }

  // Pre-allocate temporary buffer for a processed window
  processedWindow.assign(kWindowSize, 0.0f);

  // Clear and reserve per-channel buffers to avoid allocations in processBlock
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    inputBuffer[ch].clear();
    outputBuffer[ch].clear();
    weightBuffer[ch].clear();
    inputBuffer[ch].reserve(kWindowSize * 2);
    outputBuffer[ch].reserve(kWindowSize * 2);
    weightBuffer[ch].reserve(kWindowSize * 2);
  }

  outHeadPos = -kWindowSize;
  windowsProcessed = 0;

  // Report latency so the host compensates for the windowing delay
  setLatencySamples(kWindowSize);
}

void PluginProcessor::releaseResources() {
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    inputBuffer[ch].clear();
    outputBuffer[ch].clear();
    weightBuffer[ch].clear();
    inputBuffer[ch].shrink_to_fit();
    outputBuffer[ch].shrink_to_fit();
    weightBuffer[ch].shrink_to_fit();
  }
  processedWindow.clear();
  hannWindow.clear();
  outHeadPos = -kWindowSize;
  windowsProcessed = 0;
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

void PluginProcessor::processWindow(const float* input, float* output) {
  // Passthrough stub. Will be replaced with the topological deformation
  // algorithm.
  std::copy(input, input + kWindowSize, output);
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

  // Step 2: process as many full windows as possible, stepping by hopSize.
  // Each window n covers absolute output range [n * kHopSize, n * kHopSize +
  // kWindowSize), so it must be overlap-added at offset (n * kHopSize -
  // outHeadPos), NOT at the tail.
  while (inputBuffer[0].size() >= static_cast<size_t>(kWindowSize)) {
    const juce::int64 writeOffset = windowsProcessed * kHopSize - outHeadPos;

    for (int ch = 0; ch < numChannels; ++ch) {
      // Process this window
      processWindow(inputBuffer[ch].data(), processedWindow.data());

      // Make room so the window fits at writeOffset
      const size_t required = static_cast<size_t>(writeOffset) + kWindowSize;
      if (outputBuffer[ch].size() < required) {
        outputBuffer[ch].resize(required, 0.0f);
        weightBuffer[ch].resize(required, 0.0f);
      }

      // Overlap-add the Hann-weighted processed window at the correct position
      for (int i = 0; i < kWindowSize; ++i) {
        outputBuffer[ch][static_cast<size_t>(writeOffset) + i] +=
            processedWindow[i] * hannWindow[i];
        weightBuffer[ch][static_cast<size_t>(writeOffset) + i] += hannWindow[i];
      }
    }

    ++windowsProcessed;

    // Shift input buffers forward by hopSize
    for (int ch = 0; ch < numChannels; ++ch) {
      inputBuffer[ch].erase(inputBuffer[ch].begin(),
                            inputBuffer[ch].begin() + kHopSize);
    }
  }

  // Step 3: pull numSamples out of the output buffers, normalising by weights
  for (int ch = 0; ch < numChannels; ++ch) {
    float* out = buffer.getWritePointer(ch);
    const int toOutput =
        std::min(numSamples, static_cast<int>(outputBuffer[ch].size()));

    for (int i = 0; i < toOutput; ++i) {
      const float w = weightBuffer[ch][i];
      out[i] = (w > 1e-8f) ? outputBuffer[ch][i] / w : 0.0f;
    }
    // Fallback silence for the initial fill-up period (before first window is
    // ready)
    for (int i = toOutput; i < numSamples; ++i) {
      out[i] = 0.0f;
    }

    // Shift output/weight buffers forward and advance the head position
    if (toOutput > 0) {
      outputBuffer[ch].erase(outputBuffer[ch].begin(),
                             outputBuffer[ch].begin() + toOutput);
      weightBuffer[ch].erase(weightBuffer[ch].begin(),
                             weightBuffer[ch].begin() + toOutput);
    }
  }
  outHeadPos += numSamples;

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
