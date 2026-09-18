#pragma once
#include <vector>

namespace audio_plugin {
class PluginProcessor : public juce::AudioProcessor {
public:
  PluginProcessor();

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String& newName) override;

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

private:
  // Windowing constants
  static constexpr int kWindowSize = 2048;
  static constexpr int kHopSize = 1024;
  static constexpr int kMaxChannels = 2;

  // Process a single window: input and output pointers are kWindowSize long.
  void processWindow(const float* input, float* output);

  // Per-channel circular buffers
  std::vector<float> inputBuffer[kMaxChannels];
  std::vector<float> outputBuffer[kMaxChannels];
  std::vector<float> weightBuffer[kMaxChannels];

  // Hann window (periodic, suitable for overlap-add)
  std::vector<float> hannWindow;

  // Temp buffer for one processed window
  std::vector<float> processedWindow;

  // Absolute sample index of outputBuffer[0] (same for all channels)
  juce::int64 outHeadPos = -kWindowSize;
  
  // Number of windows processed so far (window n starts at n * kHopSize)
  juce::int64 windowsProcessed = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

}  // namespace audio_plugin