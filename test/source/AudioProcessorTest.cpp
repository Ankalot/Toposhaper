#include <audio_plugin/audio_plugin.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace audio_plugin {

TEST(AudioProcessor, ConstructsAndDestructs) {
  auto processor = std::make_unique<PluginProcessor>();
}

TEST(AudioProcessor, MetadataIsSane) {
  auto p = std::make_unique<PluginProcessor>();
  EXPECT_FALSE(p->getName().isEmpty());
  EXPECT_DOUBLE_EQ(p->getTailLengthSeconds(), 0.0);
  EXPECT_EQ(p->getNumPrograms(), 1);
  EXPECT_TRUE(p->hasEditor());
}

TEST(AudioProcessor, ExposesExactlyTheContractParameters) {
  auto p = std::make_unique<PluginProcessor>();
  std::vector<juce::String> ids;
  for (auto* param : p->getParameters()) {
    if (auto* paramWithID =
            dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
      ids.emplace_back(paramWithID->getParameterID());
    } else {
      ids.emplace_back(juce::String(param->getParameterIndex()));
    }
  }
  std::sort(ids.begin(), ids.end());
  const std::vector<juce::String> expected{"drive", "os_factor", "smooth_mix",
                                           "smooth_mode", "threshold"};
  EXPECT_EQ(ids, expected);
}

TEST(AudioProcessor, BusesAcceptMonoAndStereoOnly) {
  auto p = std::make_unique<PluginProcessor>();
  const juce::AudioProcessor::BusesLayout stereo{
      {juce::AudioChannelSet::stereo()}, {juce::AudioChannelSet::stereo()}};
  const juce::AudioProcessor::BusesLayout mono{{juce::AudioChannelSet::mono()},
                                               {juce::AudioChannelSet::mono()}};
  const juce::AudioProcessor::BusesLayout mismatched{
      {juce::AudioChannelSet::stereo()}, {juce::AudioChannelSet::mono()}};
  const juce::AudioProcessor::BusesLayout quad{
      {juce::AudioChannelSet::quadraphonic()},
      {juce::AudioChannelSet::quadraphonic()}};
  EXPECT_TRUE(p->isBusesLayoutSupported(stereo));
  EXPECT_TRUE(p->isBusesLayoutSupported(mono));
  EXPECT_FALSE(p->isBusesLayoutSupported(mismatched));
  EXPECT_FALSE(p->isBusesLayoutSupported(quad));
}

}  // namespace audio_plugin