#pragma once
#include <cstdint>
#include <vector>

namespace audio_plugin {
// Scope overlay marker: ring position (output stream index) + kind
struct ScopeMarker {
  uint64_t pos = 0;  // output stream index = input abs + kWindowSize
  int kind = 0;      // 0 = kept extremum, 1 = fallback seam (no extremum found)
};

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

  WarpCurve& warp() { return warpCurve_; }
  void warpEdited() { warpSlot_.publish(warpCurve_); }

  juce::AudioProcessorValueTreeState& parameters() { return apvts; }

  // Scope data accessors (UI thread). Processed ch0 history ends kScopeMargin
  // before the write cursor; startAbs returns the ring position of dst[0].
  size_t readScopeSpan(float* dst, size_t n, uint64_t& startAbs) const;
  // Raw ch0 history at exact ring positions [startAbs, startAbs+n): used to
  // overlay the input aligned through the constant latency.
  size_t readInputSpanAt(float* dst, size_t n, uint64_t startAbs) const;
  // Most recent markers (extrema / fallback seams), newest last.
  size_t readMarkerSpan(ScopeMarker* dst, size_t n) const;

private:
  // Windowing constants
  static constexpr int kWindowSize = 2048;
  static constexpr int kHopSize = kWindowSize / 2;
  static constexpr int kMaxChannels = 2;

  // Parameter state. Defaults live in createParameterLayout()
  juce::AudioProcessorValueTreeState apvts;
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();

  // Cached atomic views of the parameters
  std::atomic<float>* thresholdPtr = nullptr;
  std::atomic<float>* drivePtr = nullptr;
  std::atomic<float>* smoothMixPtr = nullptr;

  static constexpr int kWarpLutSize = 1024;
  // Warp curve: canonical model lives on the message thread; the audio thread
  // sees it only through lock-free snapshots and its own baked LUT.
  WarpCurve warpCurve_;
  WarpSlot warpSlot_;
  WarpSnapshot warpSnap_;  // audio-thread copy
  unsigned warpSnapSeq_ = ~0u;
  std::array<float, static_cast<size_t>(kWarpLutSize)> warpLut_{};
  float warpEval(float t) const;

  // Scope rings: processed and raw ch0, plus analysis markers
  static constexpr int kScopeBits = 16;
  static constexpr size_t kScopeMask =
      (static_cast<size_t>(1) << kScopeBits) - 1;
  static constexpr size_t kScopeMargin = 2048;
  std::array<float, static_cast<size_t>(1) << kScopeBits> scopeRing_{};
  std::array<float, static_cast<size_t>(1) << kScopeBits> inputRing_{};
  std::atomic<uint64_t> scopeWrite_{0};
  std::atomic<uint64_t> inputWrite_{0};
  static constexpr int kMarkerBits = 13;
  static constexpr size_t kMarkerMask =
      (static_cast<size_t>(1) << kMarkerBits) - 1;
  std::array<ScopeMarker, static_cast<size_t>(1) << kMarkerBits> markerRing_{};
  std::atomic<uint64_t> markerWrite_{0};
  uint64_t inputBase_ = 0;  // absolute index of inputBuffer[ch][0]
  bool windowSilent_ =
      false;  // last analysis window was degenerate (silence/DC)

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