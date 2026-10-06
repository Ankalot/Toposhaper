#include <audio_plugin/audio_plugin.h>
#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

namespace audio_plugin {
namespace {

constexpr double kSr = 44100.0;
constexpr int kBlock = 512;

void setParam(PluginProcessor& p, const char* id, float normalized) {
  auto* param = p.parameters().getParameter(id);
  ASSERT_TRUE(param != nullptr);
  param->setValueNotifyingHost(normalized);
}

std::vector<float> makeInput(int n, bool noisy) {
  std::vector<float> v(static_cast<size_t>(n));
  std::mt19937 rng{1234};
  std::uniform_real_distribution<float> noise{-0.06f, 0.06f};
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kSr;
    float s =
        static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
    if (noisy)
      s += noise(rng);
    v[static_cast<size_t>(i)] = s;
  }
  return v;
}

std::vector<float> runBlocks(PluginProcessor& p,
                             const std::vector<float>& in,
                             int channels = 2) {
  std::vector<float> out;
  juce::MidiBuffer midi;
  for (size_t i = 0; i < in.size(); i += static_cast<size_t>(kBlock)) {
    const auto n = static_cast<int>(std::min<size_t>(kBlock, in.size() - i));
    juce::AudioBuffer<float> buf{channels, n};
    for (int ch = 0; ch < channels; ++ch)
      buf.copyFrom(ch, 0, in.data() + i, n);
    p.processBlock(buf, midi);
    for (int k = 0; k < n; ++k)
      out.push_back(buf.getSample(0, k));
  }
  return out;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
  double d = 0.0;
  const auto n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i)
    d = std::max(d, static_cast<double>(std::abs(a[i] - b[i])));
  return d;
}

}  // namespace

TEST(Processor, DefaultsAndParameterFlags) {
  PluginProcessor p;
  EXPECT_NEAR(p.parameters().getParameter("threshold")->getValue(), 0.1f,
              1e-6f);
  EXPECT_NEAR(p.parameters().getParameter("drive")->getValue(), 1.0f, 1e-6f);
  EXPECT_NEAR(p.parameters().getParameter("smooth_mix")->getValue(), 0.0f,
              1e-6f);
  auto* smoothModeParam = dynamic_cast<juce::AudioParameterChoice*>(
      p.parameters().getParameter("smooth_mode"));
  auto* osFactorParam = dynamic_cast<juce::AudioParameterChoice*>(
      p.parameters().getParameter("os_factor"));
  ASSERT_NE(smoothModeParam, nullptr);
  ASSERT_NE(osFactorParam, nullptr);
  EXPECT_EQ(smoothModeParam->getIndex(), 0);
  EXPECT_EQ(osFactorParam->getIndex(), 0);
  EXPECT_TRUE(p.parameters().getParameter("drive")->isAutomatable());
  EXPECT_FALSE(p.parameters()
                   .getParameter("os_factor")
                   ->isAutomatable());  // quality, not automation
  EXPECT_EQ(p.osFactor(), 1);
  EXPECT_EQ(p.smoothMode(), 0);
}

TEST(Processor, LatencyIsWindowSizeAndOversamplingIndependent) {
  PluginProcessor p;
  p.prepareToPlay(kSr, kBlock);
  const int base = p.getLatencySamples();
  EXPECT_GT(base, 0);
  for (float os : {0.5f, 1.0f}) {  // X2, X4
    setParam(p, "os_factor", os);
    p.prepareToPlay(kSr, kBlock);
    EXPECT_EQ(p.getLatencySamples(),
              base);  // Zero-phase within the zone: latency does not drift
  }
}

TEST(Processor, DriveZeroIsDelayedPassthrough) {
  PluginProcessor p;
  setParam(p, "drive", 0.0f);
  p.prepareToPlay(kSr, kBlock);
  const int L = p.getLatencySamples();
  const auto in = makeInput(L + 4 * kBlock, false);
  const auto out = runBlocks(p, in);
  ASSERT_GE(out.size(), in.size());
  for (size_t i = 0; i + static_cast<size_t>(L) < in.size(); ++i)
    EXPECT_NEAR(out[i + static_cast<size_t>(L)], in[i], 1e-6f) << "i=" << i;
}

TEST(Processor, IdentityCurveIsPassthroughEvenWithFullDrive) {
  PluginProcessor p;
  p.warp().moveHandle(0, 0.5f, 0.5f);  // y = x
  p.warpEdited();
  p.prepareToPlay(kSr, kBlock);
  const int L = p.getLatencySamples();
  const auto in = makeInput(L + 4 * kBlock, false);
  const auto out = runBlocks(p, in);
  for (size_t i = 0; i + static_cast<size_t>(L) < in.size(); ++i)
    EXPECT_NEAR(out[i + static_cast<size_t>(L)], in[i], 1e-5f);
}

TEST(Processor, DefaultCurveActuallyDeforms) {
  PluginProcessor p;  // parabola from reset, drive = 1 by default
  p.prepareToPlay(kSr, kBlock);
  const int L = p.getLatencySamples();
  const auto in = makeInput(L + 4 * kBlock, false);
  const auto out = runBlocks(p, in);
  std::vector<float> delayed(in.size());
  for (size_t i = 0; i + static_cast<size_t>(L) < in.size(); ++i)
    delayed[i + static_cast<size_t>(L)] = in[i];
  EXPECT_GT(maxAbsDiff(out, delayed),
            1e-3);  // plugin sounds good right out of the box
}

TEST(Processor, SilenceAndDcPassUnchanged) {
  PluginProcessor p;
  p.prepareToPlay(kSr, kBlock);
  const int L = p.getLatencySamples();
  {  // тишина
    const std::vector<float> in(static_cast<size_t>(L) + 2048, 0.0f);
    const auto out = runBlocks(p, in);
    for (size_t i = static_cast<size_t>(L); i < in.size(); ++i)
      EXPECT_NEAR(out[i], 0.0f, 1e-6f);
  }
  PluginProcessor q;
  q.prepareToPlay(kSr, kBlock);
  {  // DC: zero window range -> section is degenerate -> constant persists
    const std::vector<float> in(static_cast<size_t>(L) + 2048, 0.5f);
    const auto out = runBlocks(q, in);
    for (size_t i = static_cast<size_t>(L); i < in.size(); ++i)
      EXPECT_NEAR(out[i], 0.5f, 1e-5f);
  }
}

TEST(Processor, FiniteAcrossRatesBlocksAndParameterExtremes) {
  for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
    for (int block : {1, 7, 64, 512, 2048}) {
      PluginProcessor p;
      p.prepareToPlay(sr, block);
      juce::MidiBuffer midi;
      juce::AudioBuffer<float> buf{2, block};
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
          buf.setSample(ch, i, 0.4f * std::sin(0.05f * static_cast<float>(i)));
      p.processBlock(buf, midi);
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < block; ++i)
          ASSERT_TRUE(std::isfinite(buf.getSample(ch, i)))
              << sr << "/" << block;
    }
  }
  PluginProcessor p;
  p.prepareToPlay(kSr, kBlock);
  for (const char* id : {"drive", "threshold", "smooth_mix"}) {
    for (float v : {0.0f, 1.0f}) {
      setParam(p, id, v);
      for (float mode : {0.0f, 1.0f}) {
        setParam(p, "smooth_mode", mode);
        for (float os : {0.0f, 0.5f, 1.0f}) {
          setParam(p, "os_factor", os);
          auto buf = juce::AudioBuffer<float>{2, kBlock};
          const auto in = makeInput(kBlock, true);
          buf.copyFrom(0, 0, in.data(), kBlock);
          buf.copyFrom(1, 0, in.data(), kBlock);
          juce::MidiBuffer midi;
          p.processBlock(buf, midi);
          for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
              ASSERT_TRUE(std::isfinite(buf.getSample(ch, i)))
                  << id << v << mode << os;
        }
      }
    }
  }
}

TEST(Processor, MonoLayoutProcesses) {
  PluginProcessor p;
  const juce::AudioProcessor::BusesLayout mono{{juce::AudioChannelSet::mono()},
                                               {juce::AudioChannelSet::mono()}};
  ASSERT_TRUE(p.setBusesLayout(mono));
  p.prepareToPlay(kSr, kBlock);
  const auto in = makeInput(16 * kBlock, false);
  const auto out = runBlocks(p, in, 1);
  for (const auto v : out)
    ASSERT_TRUE(std::isfinite(v));
}

TEST(Processor, ProcessWithoutPrepareEmitsSilenceNotGarbage) {
  PluginProcessor p;
  juce::AudioBuffer<float> buf{2, kBlock};
  const auto in = makeInput(kBlock, false);
  buf.copyFrom(0, 0, in.data(), kBlock);
  buf.copyFrom(1, 0, in.data(), kBlock);
  juce::MidiBuffer midi;
  p.processBlock(buf, midi);
  for (int ch = 0; ch < 2; ++ch)
    for (int i = 0; i < kBlock; ++i)
      EXPECT_EQ(buf.getSample(ch, i),
                0.0f);  // FIFO empty -> silence, not garbage.
}

TEST(Processor, SmoothModesProduceDifferentMath) {
  auto render = [](float mode) {
    PluginProcessor p;
    setParam(p, "smooth_mix", 1.0f);
    setParam(p, "smooth_mode", mode);
    setParam(p, "threshold", 0.05f);  // many sections
    p.prepareToPlay(kSr, kBlock);
    return runBlocks(p, makeInput(8 * kBlock, true));
  };
  EXPECT_GT(maxAbsDiff(render(0.0f), render(1.0f)), 1e-4);  // SG != PAVA
}

TEST(Processor, ThresholdControlsAnchorCountViaMarkers) {
  auto markerCount = [](float thr) {
    PluginProcessor p;
    setParam(p, "threshold", thr);
    p.prepareToPlay(kSr, kBlock);
    runBlocks(p, makeInput(8 * kBlock, true));
    std::vector<ScopeMarker> marks(8192);
    return p.readMarkerSpan(marks.data(), marks.size());
  };
  EXPECT_GT(markerCount(0.0f),
            markerCount(1.0f));  // noise became texture -> fewer anchors
}

TEST(Processor, MarkersAreSortedAndWellFormed) {
  PluginProcessor p;
  setParam(p, "threshold", 0.05f);
  p.prepareToPlay(kSr, kBlock);
  runBlocks(p, makeInput(8 * kBlock, true));
  std::vector<ScopeMarker> marks(8192);
  const auto n = p.readMarkerSpan(marks.data(), marks.size());
  ASSERT_GT(n, 0u);
  uint64_t prevPerChannel[2] = {0, 0};
  bool seen[2] = {false, false};
  for (size_t i = 0; i < n; ++i) {
    ASSERT_GE(marks[i].ch, 0);
    ASSERT_LE(marks[i].ch, 1);
    ASSERT_GE(marks[i].kind, 0);
    ASSERT_LE(marks[i].kind, 1);
    if (seen[marks[i].ch])
      EXPECT_GT(marks[i].pos, prevPerChannel[marks[i].ch]) << "i=" << i;
    prevPerChannel[marks[i].ch] = marks[i].pos;
    seen[marks[i].ch] = true;
  }
}

TEST(Processor, InputRingReturnsExactlyWhatWasFed) {
    auto p = std::make_unique<PluginProcessor>();
    p->prepareToPlay(kSr, kBlock);
    constexpr int kFeed = 32 * kBlock;
    const auto in = makeInput(kFeed, false);
    runBlocks(*p, in);
    std::vector<float> back(2048);
    const auto got = p->readInputSpanAt(back.data(), back.size(), 0, 0);
    ASSERT_EQ(got, back.size()) << "feed=" << kFeed << ": ring gate not reached";
    for (size_t i = 0; i < back.size(); ++i) EXPECT_EQ(back[i], in[i]);
}

TEST(Processor, ScopeSpansOfBothChannelsStayAligned) {
  PluginProcessor p;
  p.prepareToPlay(kSr, kBlock);
  runBlocks(p, makeInput(16 * kBlock, false));
  std::vector<float> a(2048), b(2048);
  uint64_t sa = 0, sb = 0;
  ASSERT_EQ(p.readScopeSpan(a.data(), a.size(), sa, 0), a.size());
  ASSERT_EQ(p.readScopeSpan(b.data(), b.size(), sb, 1), b.size());
  EXPECT_EQ(sa,
            sb);  // The ring cursors did not drift apart: the ghosts and the
                  // delta will align on the X-axis.
}

TEST(Processor, UiStateDoesNotLeakIntoAudio) {
  auto render = [](bool tweakUi) {
    PluginProcessor p;
    p.prepareToPlay(kSr, kBlock);
    if (tweakUi) {  // view-scope settings: zoom, channels, track
      auto& s = p.uiState();
      s.viewLen = 8192;
      s.vExp = 3.5f;
      s.chanMode = 3;
      s.track = false;
    }
    return runBlocks(p, makeInput(6 * kBlock, false));
  };
  EXPECT_EQ(maxAbsDiff(render(false), render(true)), 0.0);
}

TEST(Processor, WarpEditRepublishChangesAudio) {
  auto render = [](bool identity) {
    PluginProcessor p;
    if (identity) {
      p.warp().moveHandle(0, 0.5f, 0.5f);
      p.warpEdited();  // publish to slot -> LUT is updated in processBlock
    }
    p.prepareToPlay(kSr, kBlock);
    return runBlocks(p, makeInput(6 * kBlock, false));
  };
  EXPECT_GT(maxAbsDiff(render(false), render(true)), 1e-3);
}

TEST(Processor, MonotonicLockToggleChangesOutput) {
  auto render = [](bool locked) {
    auto p = std::make_unique<PluginProcessor>();
    auto& w = p->warp();
    w.setMonotonicLock(false);
    w.addPoint(0.3f);
    w.addPoint(0.7f);
    w.movePoint(1, 0.3f, 0.8f);
    w.movePoint(2, 0.7f, 0.15f);
    w.setMonotonicLock(locked);
    p->warpEdited();
    p->prepareToPlay(kSr, kBlock);
    return runBlocks(*p, makeInput(6 * kBlock, false));
  };
  EXPECT_GT(maxAbsDiff(render(false), render(true)), 1e-3);
}

TEST(State, HostRoundtripPreservesParamsWarpAndPresetMeta) {
  PluginProcessor writer;
  setParam(writer, "drive", 0.37f);
  setParam(writer, "threshold", 0.62f);
  setParam(writer, "smooth_mode", 1.0f);
  setParam(writer, "os_factor", 0.5f);
  writer.warp().addPoint(0.42f);
  writer.warp().moveHandle(1, 0.7f, 0.3f);
  writer.warpEdited();
  writer.setPresetIdentity("Lab Rat", "user");
  writer.setPresetDirty(true);
  juce::MemoryBlock block;
  writer.getStateInformation(block);

  PluginProcessor reader;
  reader.setStateInformation(block.getData(),
                             static_cast<int>(block.getSize()));
  EXPECT_EQ(reader.copyStateAsXml(),
            writer.copyStateAsXml());  // params + warp + PRESET
  EXPECT_EQ(reader.uiState().viewLen,
            writer.uiState().viewLen);  // The UI view has arrived as well.
}

TEST(State, PresetXmlPathStripsUiAndMeta) {
  PluginProcessor writer;
  writer.setPresetIdentity("Secret", "factory");
  writer.uiState().viewLen = 1234;
  const auto xml = writer.copyStateAsXml();  // preset path: headless

  PluginProcessor reader;
  reader.uiState().viewLen = 2048;
  const auto seqBefore = reader.uiState().seq;
  ASSERT_TRUE(reader.applyStateXml(xml));
  EXPECT_EQ(reader.uiState().viewLen, 2048);  // the view didn't move
  EXPECT_EQ(reader.uiState().seq,
            seqBefore);  // and the sequence remained untouched.
}

TEST(State, GarbageStateKeepsProcessorSane) {
  PluginProcessor p;
  std::mt19937 rng{7};
  std::uniform_int_distribution<int> bytes{0, 255};
  juce::MemoryBlock garbage{512};
  for (size_t i = 0; i < garbage.getSize(); ++i)
    garbage[i] = static_cast<char>(bytes(rng));
  p.setStateInformation(garbage.getData(), static_cast<int>(garbage.getSize()));
  for (auto* param : p.getParameters()) {
    EXPECT_GE(param->getValue(), 0.0f);
    EXPECT_LE(param->getValue(), 1.0f);
  }
  p.prepareToPlay(kSr, kBlock);
  const auto out = runBlocks(p, makeInput(2 * kBlock, true));
  for (const auto v : out)
    ASSERT_TRUE(std::isfinite(v));
}

}  // namespace audio_plugin