#pragma once
#include <cstdint>
#include <vector>

namespace audio_plugin {
// Scope overlay marker: ring position (output stream index) + kind + ch
struct ScopeMarker {
  uint64_t pos = 0;  // output stream index = input abs + kWindowSize
  int kind = 0;      // 0 = kept extremum, 1 = fallback seam
  int ch = 0;        // 0 = L, 1 = R
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
  int osFactor() const;    // 1 / 2 / 4 / 8,  from the os_factor choice index
  int smoothMode() const;  // 0 = SG, 1 = PAVA (index choice smooth_mode)
  juce::String copyStateAsXml();
  bool applyStateXml(const juce::String& xml);
  static juce::File userPresetDirectory();
  const juce::String& presetName() const { return presetName_; }
  const juce::String& presetSource() const { return presetSource_; }
  void setPresetIdentity(const juce::String& name, const juce::String& source);
  bool presetDirty() const { return presetDirty_; }
  void setPresetDirty(bool dirty);

  // Scope data accessors (UI thread), per channel. Processed history ends
  // kScopeMargin before the write cursor; startAbs returns ring pos of dst[0].
  size_t readScopeSpan(float* dst, size_t n, uint64_t& startAbs, int ch) const;
  // Raw history at exact ring positions [startAbs, startAbs+n), per channel.
  size_t readInputSpanAt(float* dst, size_t n, uint64_t startAbs, int ch) const;
  size_t readMarkerSpan(ScopeMarker* dst, size_t n) const;

  // View-only recall (scope zoom / channel / track). Never automatable and
  // deliberately outside APVTS: presets and host automation must not touch
  // what the user sees, only what the user hears.
  struct UiState {
    int viewLen = 2048;  // semantic visible window, 256..32768
    float vExp = 0.0f;   // vertical gain exponent (-2..5)
    int chanMode = 0;    // 0=L, 1=R, 2=overlay, 3=split
    bool track = true;   // TRACK toggle
    uint32_t seq = 0;    // bumped on restore so open editors re-apply
  };
  UiState& uiState() { return uiState_; }
  const UiState& uiState() const { return uiState_; }

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
  std::atomic<float>* osPtr = nullptr;  // raw view of os_factor choice index
  std::atomic<float>* smoothModePtr =
      nullptr;  // raw view of smooth_mode choice index

  static constexpr int kWarpLutSize = 1024;
  // Warp curve: canonical model lives on the message thread; the audio thread
  // sees it only through lock-free snapshots and its own baked LUT.
  WarpCurve warpCurve_;
  WarpSlot warpSlot_;
  WarpSnapshot warpSnap_;  // audio-thread copy
  unsigned warpSnapSeq_ = ~0u;
  std::array<float, static_cast<size_t>(kWarpLutSize)> warpLut_{};
  float warpEval(float t) const;

  // Oversampling scratch for the warp nonlinearity: smoothed base + ping-pong
  std::vector<float> osS_, osA_, osB_;
  std::vector<float> pava_;  // scratch: mono interior of the section (PAVA)

  // =========================================================================
  // Scope / marker ring sizing (edit together, don't change one alone)
  //
  // Scope ring (2^kScopeBits samples, per channel):
  //   kScopeMargin     >= max host block (safety buffer against in-flight
  //   writes) kMaxSpan         = 2^kScopeBits - kScopeMargin  (what the reader
  //   can copy) UI want          = viewLen*2 + 4096, clipped to kMaxSpan by the
  //   reader Required depth   = kScopeMargin + kMaxSpan  (i.e. the full ring -
  //   trivial) Practical depth  = kScopeMargin + want_max  (real headroom the
  //   UI sees)
  //                      = 2048 + (32768*2 + 4096) = 71680 samples
  //   Current 2^16     = 65536 -> want gets clipped to 63488, still leaves
  //                      63488 - 32768 = 30720 samples (~700 ms at 44.1 kHz)
  //                      for trigger search behind the view. Plenty.
  //   To widen max H-zoom: grow kScopeBits so 2^kScopeBits >= kScopeMargin +
  //     (new_max_viewLen * 2 + 4096). Each +1 bit doubles memory (~128 KB/ch).
  //
  // Marker ring (2^kMarkerBits markers, single ring for both channels):
  //   P                = markers per analysis pass (~5 on clean sine, ~30-100
  //                      typical, ~300-500 on saw/noise with low threshold)
  //   display lag      ~ 5-7 passes (kScopeMargin + hop alignment)
  //   view width       ~ viewLen / kHopSize passes  (up to 32 at max zoom)
  //   needed depth     = (display_lag + view_passes) * P_max
  //                      = (7 + 32) * 500 = 19500 markers  <- tight!
  //   Current 2^14     = 16384 -> at P_max=500 we have ~32 passes depth,
  //                      4.5x the display lag. Sufficient but not generous.
  //   UI reads 8192 per frame; guard = cap - 64 = 16320 (so 8192 is safe).
  //   To grow: bump kMarkerBits; each +1 bit = +128 KB of ScopeMarker[].
  //   If you ever see markers vanish when lowering threshold on noisy
  //   material, that's P exploding past depth budget - grow kMarkerBits.
  // =========================================================================
  static constexpr int kScopeBits = 16;  // 65536 samples per channel
  static constexpr size_t kScopeMask =
      (static_cast<size_t>(1) << kScopeBits) - 1;
  static constexpr size_t kScopeMargin = 2048;
  std::array<float, static_cast<size_t>(1) << kScopeBits> scopeRing_{};
  std::array<float, static_cast<size_t>(1) << kScopeBits> scopeRingR_{};
  std::array<float, static_cast<size_t>(1) << kScopeBits> inputRing_{};
  std::array<float, static_cast<size_t>(1) << kScopeBits> inputRingR_{};
  std::atomic<uint64_t> scopeWrite_{0};
  std::atomic<uint64_t> scopeWriteR_{0};
  std::atomic<uint64_t> inputWrite_{0};
  std::atomic<uint64_t> inputWriteR_{0};
  static constexpr int kMarkerBits =
      14;  // 16384 markers (single ring, both ch)
  static constexpr size_t kMarkerMask =
      (static_cast<size_t>(1) << kMarkerBits) - 1;
  std::array<ScopeMarker, static_cast<size_t>(1) << kMarkerBits> markerRing_{};
  std::atomic<uint64_t> markerWrite_{0};
  uint64_t inputBase_ = 0;  // absolute index of inputBuffer[ch][0]
  bool windowSilent_ =
      false;         // last analysis window was degenerate (silence/DC)
  UiState uiState_;  // view recall mirror: editor writes, state serializes
  juce::String presetName_{"Init"}, presetSource_{"init"};
  bool presetDirty_ =
      false;  // '*' flag: state differs from applied/saved preset

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

  juce::ValueTree buildStateTree(bool includeUi);
  bool applyStateTree(juce::ValueTree tree);

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