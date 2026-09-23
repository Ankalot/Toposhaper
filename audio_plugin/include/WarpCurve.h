#pragma once
#include <atomic>
#include <vector>

namespace audio_plugin {

// Warp curve g:[0,1]->[0,1], g(0)=0, g(1)=1. Serum-style model:
// two fixed endpoints, movable interior points, one quadratic Bezier handle per
// segment (the "curve point" that bends the line).
class WarpCurve {
public:
  static constexpr int kMaxPoints = 30;  // including the two fixed ends
  static constexpr int kMaxSegments = kMaxPoints - 1;
  static constexpr const char* kStateTag = "WARP";

  struct Node {
    float x = 0.0f, y = 0.0f;
  };

  struct Seg {
    Node p0, c, p1;
  };

  WarpCurve() { reset(); }

  void reset();  // identity diagonal, single segment
  int numPoints() const { return static_cast<int>(pts_.size()); }
  int numSegments() const { return numPoints() - 1; }
  const Node& point(int i) const { return pts_[static_cast<size_t>(i)]; }
  const Node& handle(int s) const { return hnd_[static_cast<size_t>(s)]; }

  // Message-thread editors. Endpoints immovable; all inputs clamped so pts_
  // stay x-sorted with non-decreasing y and handles stay inside their segment
  // box => every quadratic segment, and the whole curve, stays monotone.
  bool movePoint(int i, float x, float y);
  bool moveHandle(int s, float x, float y);
  int addPoint(float x);    // exact de Casteljau split of the segment under x
                            // (shape preserved), y snapped onto the curve;
                            // returns new point index or -1 if full/invalid
  bool removePoint(int i);  // merges neighbouring segments; merged handle
                            // keeps the curve passing through removed point

  float eval(float t) const;           // exact evaluation, any thread
  void bake(float* dst, int n) const;  // y at uniform x in [0,1]

  juce::ValueTree toValueTree() const;
  bool fromValueTree(const juce::ValueTree& v);  // clamps whatever it reads

private:
  Seg segment(int s) const;
  static float solveTau(const Seg& s, float x);
  void clampHandlesAround(int i);
  void clampAll();  // enforce invariants after bulk load

  std::vector<Node> pts_;  // [0]=(0,0), back=(1,1), x-sorted, y non-decreasing
  std::vector<Node> hnd_;  // one per segment, inside its segment box
};

// Fixed-size POD snapshot for lock-free message->audio handoff.
struct WarpSnapshot {
  int n = 2;
  float px[WarpCurve::kMaxPoints] = {0.0f, 1.0f};
  float py[WarpCurve::kMaxPoints] = {0.0f, 1.0f};
  float hx[WarpCurve::kMaxSegments] = {0.5f};
  float hy[WarpCurve::kMaxSegments] = {0.5f};
};

WarpSnapshot makeWarpSnapshot(const WarpCurve& c);
void bakeWarpLut(const WarpSnapshot& s, float* dst, int n);

// Seqlock slot: single writer (message thread), single reader (audio thread).
struct WarpSlot {
  void publish(const WarpCurve& c);
  bool take(WarpSnapshot& dst, unsigned& lastSeq) const;

private:
  WarpSnapshot data_;
  std::atomic<unsigned> seq_{0};
};

}  // namespace audio_plugin