#include <audio_plugin/audio_plugin.h>
#include <gtest/gtest.h>

#include <cmath>

namespace audio_plugin {
namespace {
constexpr float kTol = 1e-4f;
std::vector<float> sampleEval(const WarpCurve& c, int n = 64) {
  std::vector<float> ys;
  for (int i = 0; i <= n; ++i)
    ys.push_back(c.eval(static_cast<float>(i) / static_cast<float>(n)));
  return ys;
}
}  // namespace

TEST(WarpCurve, ResetIsExactParabola) {
  WarpCurve c;
  c.reset();
  for (int i = 0; i <= 32; ++i) {
    const float t = static_cast<float>(i) / 32.0f;
    EXPECT_NEAR(c.eval(t), t * t, kTol);
  }
}

TEST(WarpCurve, EndpointsAreImmovable) {
  WarpCurve c;
  c.reset();
  c.addPoint(0.5f);
  EXPECT_FALSE(c.movePoint(0, 0.2f, 0.2f));
  EXPECT_FALSE(c.movePoint(c.numPoints() - 1, 0.8f, 0.8f));
  EXPECT_FLOAT_EQ(c.point(0).x, 0.0f);
  EXPECT_FLOAT_EQ(c.point(0).y, 0.0f);
  EXPECT_FLOAT_EQ(c.point(c.numPoints() - 1).x, 1.0f);
  EXPECT_FLOAT_EQ(c.point(c.numPoints() - 1).y, 1.0f);
}

TEST(WarpCurve, AddPointSplitsWithoutChangingShape) {
  WarpCurve c;
  c.reset();
  c.moveHandle(0, 0.3f, 0.8f);  // free-form prior to the split
  const auto before = sampleEval(c);
  const int idx = c.addPoint(0.37f);
  ASSERT_GT(idx, 0);
  const auto after = sampleEval(c);
  ASSERT_EQ(before.size(), after.size());
  for (size_t i = 0; i < before.size(); ++i)
    EXPECT_NEAR(before[i], after[i], 1e-3f) << "x=" << i;
  // The new point lies on the curve.
  EXPECT_NEAR(c.eval(c.point(idx).x), c.point(idx).y, 1e-3f);
}

TEST(WarpCurve, AddPointRejectsEdgesAndNearNodes) {
  WarpCurve c;
  c.reset();
  EXPECT_EQ(c.addPoint(0.0f), -1);          // reject
  EXPECT_EQ(c.addPoint(1.0f), -1);          // reject
  EXPECT_EQ(c.addPoint(0.5f), 1);           // valid
  EXPECT_EQ(c.addPoint(0.5f), -1);          // reject
  EXPECT_EQ(c.addPoint(0.5f + 1e-5f), -1);  // reject
}

TEST(WarpCurve, AddPointRejectsWhenCapacityExhausted) {
  WarpCurve c;
  c.reset();
  int added = 0;
  while (added < 128) {
    int best = -1;
    float bestW = 0.0f;
    for (int s = 0; s < c.numSegments(); ++s) {
      const float w = c.point(s + 1).x - c.point(s).x;
      if (w > bestW) {
        bestW = w;
        best = s;
      }
    }
    const float x = 0.5f * (c.point(best).x + c.point(best + 1).x);
    if (c.addPoint(x) < 0)
      break;
    ++added;
  }
  const int maxPts = c.numPoints();
  EXPECT_GT(maxPts, 2);
  EXPECT_EQ(c.addPoint(0.31415f), -1);
  EXPECT_EQ(c.numPoints(), maxPts);
}

TEST(WarpCurve, RemovePointKeepsEndpointsAndRejectsInvalid) {
  WarpCurve c;
  c.reset();
  ASSERT_GE(c.addPoint(0.4f), 0);
  ASSERT_GE(c.addPoint(0.7f), 0);
  const int n = c.numPoints();
  EXPECT_FALSE(c.removePoint(0));
  EXPECT_FALSE(c.removePoint(n - 1));
  EXPECT_TRUE(c.removePoint(1));
  EXPECT_EQ(c.numPoints(), n - 1);
  EXPECT_FLOAT_EQ(c.point(0).y, 0.0f);
  EXPECT_FLOAT_EQ(c.point(c.numPoints() - 1).y, 1.0f);
  for (const auto v : sampleEval(c))
    EXPECT_TRUE(std::isfinite(v));
}

TEST(WarpCurve, MonotonicLockClampsNodeYBetweenNeighbours) {
  WarpCurve c;
  c.reset();
  c.setMonotonicLock(true);
  c.addPoint(0.5f);
  c.movePoint(1, 0.5f, -0.2f);
  EXPECT_NEAR(c.point(1).y, 0.0f, kTol);  // wedged against the neighbor below
  c.movePoint(1, 0.5f, 1.5f);
  EXPECT_NEAR(c.point(1).y, 1.0f, kTol);  // wedged against the top one
}

TEST(WarpCurve, FoldRangeContainmentOnInteriorSegment) {
  WarpCurve c;
  c.reset();
  c.setMonotonicLock(false);
  c.addPoint(0.5f);
  c.movePoint(1, 0.5f, 0.5f);
  c.moveHandle(0, 0.25f, -5.0f);
  c.moveHandle(1, 0.75f, 5.0f);
  for (const auto v : sampleEval(c)) {
    EXPECT_GE(v, -1e-4f);
    EXPECT_LE(v, 1.0f + 1e-4f);
  }
}

TEST(WarpCurve, EnablingLockMonotonizesExistingFold) {
  WarpCurve c;
  c.reset();
  c.setMonotonicLock(false);
  c.addPoint(0.3f);
  c.addPoint(0.7f);
  c.movePoint(1, 0.3f, 0.8f);
  c.movePoint(2, 0.7f, 0.2f);
  ASSERT_LT(c.point(2).y, c.point(1).y);
  c.setMonotonicLock(true);
  for (int i = 1; i < c.numPoints(); ++i)
    EXPECT_GE(c.point(i).y, c.point(i - 1).y - kTol);
}

TEST(WarpCurve, HandleNodeLiesOnCurve) {
  WarpCurve c;
  c.reset();
  c.moveHandle(0, 0.62f, 0.21f);
  const auto q = c.handleNode(0);
  EXPECT_NEAR(c.eval(q.x), q.y, 1e-3f);
}

TEST(WarpCurve, ControlStaysInsideSegmentBox) {
  WarpCurve c;
  c.reset();
  c.setMonotonicLock(false);
  c.addPoint(0.5f);
  c.moveHandle(0, -2.0f, 3.0f);
  const auto& a = c.point(0);
  const auto& b = c.point(1);
  EXPECT_GE(c.control(0).x, a.x - kTol);
  EXPECT_LE(c.control(0).x, b.x + kTol);
}

TEST(WarpCurve, ValueTreeRoundtrip) {
  WarpCurve c;
  c.reset();
  c.setMonotonicLock(false);
  c.addPoint(0.33f);
  c.moveHandle(1, 0.8f, 0.4f);
  c.setMonotonicLock(true);
  WarpCurve d;
  ASSERT_TRUE(d.fromValueTree(c.toValueTree()));
  EXPECT_EQ(d.numPoints(), c.numPoints());
  EXPECT_EQ(d.monotonicLock(), c.monotonicLock());
  for (int i = 0; i < c.numPoints(); ++i) {
    EXPECT_FLOAT_EQ(d.point(i).x, c.point(i).x);
    EXPECT_FLOAT_EQ(d.point(i).y, c.point(i).y);
  }
}

TEST(WarpCurve, ValueTreeGarbageFallsBackToReset) {
  WarpCurve d;
  juce::ValueTree broken{WarpCurve::kStateTag};
  broken.appendChild(juce::ValueTree{"P"}, nullptr);  // fewer than two points
  EXPECT_FALSE(d.fromValueTree(broken));
  EXPECT_EQ(d.numPoints(), 2);
  EXPECT_NEAR(d.eval(0.5f), 0.25f, kTol);  // a parabola again
}

TEST(WarpCurve, BakeLutMatchesEvalAndRange) {
  WarpCurve c;
  c.reset();
  c.moveHandle(0, 0.4f, 0.7f);
  constexpr int kN = 257;
  std::vector<float> lut(kN);
  c.bake(lut.data(), kN);
  EXPECT_NEAR(lut[0], c.eval(0.0f), 1e-3f);
  EXPECT_NEAR(lut[kN - 1], c.eval(1.0f), 1e-3f);
  for (const auto v : lut) {
    EXPECT_TRUE(std::isfinite(v));
    EXPECT_GE(v, -1e-4f);
    EXPECT_LE(v, 1.0f + 1e-4f);
  }
}

TEST(WarpSlot, PublishTakeHandshake) {
  WarpSlot slot;
  WarpCurve c;
  c.reset();
  slot.publish(c);
  WarpSnapshot s{};
  unsigned seq = 0;
  ASSERT_TRUE(slot.take(s, seq));
  EXPECT_FLOAT_EQ(s.px[0], 0.0f);
  EXPECT_FLOAT_EQ(s.py[s.n - 1], 1.0f);
  EXPECT_FALSE(slot.take(s, seq));  // No data without a new publish.
  c.moveHandle(0, 0.5f, 0.5f);
  slot.publish(c);
  EXPECT_TRUE(slot.take(s, seq));
  EXPECT_NEAR(s.hy[0], 0.5f, kTol);
}

}  // namespace audio_plugin