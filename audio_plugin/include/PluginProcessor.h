#pragma once
#include <cstdint>
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
  static constexpr int kHopSize = kWindowSize / 2;
  static constexpr int kMaxChannels = 2;

  // DSP constants (become APVTS parameters later)
  static constexpr float kThreshold =
      0.05f;  // persistence threshold, normalised units
  static constexpr float kDrive = 0.0f;  // deformation depth: 0 = passthrough
  static constexpr float kDeformPower = 2.5f;  // power-curve exponent

  void findExtrema(const float* window);
  int findRoot(int i);

  // Seam of the current zone: first kept extremum in [kHopSize, kWindowSize -
  // 1], fallback kWindowSize-1.
  int findZoneEnd();

  // Deform zone [zoneStart, zoneEnd) using anchors = forced seams + kept
  // extrema inside the zone. Anchor values are fixed. Writes zoneEnd -
  // zoneStart samples into out.
  void processZone(const float* window, int zoneStart, int zoneEnd, float* out);
  void deformSection(const float* window,
                     int a,
                     int b,
                     float* out,
                     int outOffset);

  // Per-channel input accumulation and output FIFO. The FIFO is prefilled
  // with kWindowSize zeros: that silence IS the reported latency, so the
  // first zone sample lands exactly at raw position kWindowSize.
  std::vector<float> inputBuffer[kMaxChannels];
  std::vector<float> outputBuffer[kMaxChannels];

  // Per-channel zone start index inside current window, in [0, kHopSize - 1]
  int prevZoneEnd[kMaxChannels] = {0, 0};

  // Reusable analysis state in window (preallocated)
  std::vector<int> extrema_;     // size = number of extrema in window
  std::vector<int> order_;       // size = kWindowSize
  std::vector<int> parent_;      // size = kWindowSize
  std::vector<uint8_t> active_;  // size = kWindowSize

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

}  // namespace audio_plugin