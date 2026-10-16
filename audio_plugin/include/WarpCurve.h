#pragma once
#include <atomic>
#include <cmath>
#include <vector>

namespace audio_plugin {

// Monotone warp curve g:[0,1]->[0,1], g(0)=0, g(1)=1. Serum-style layout:
// two fixed endpoints, movable interior points (double-click in the UI), one
// quadratic Bezier handle per segment (asymmetric bends preserved). The
// USER-FACING handle is the ON-CURVE midpoint Q = B(0.5); the stored control
// point C is recovered as C = 2*Q - mid, so the dot the user sees always lies
// on the curve while dragging it in 2D spans the full monotone Bezier family.
// C is clamped into the segment box => every segment stays monotone for any
// input, so the topology contract of the deform stage never breaks.
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

  bool monotonicLock() const { return monotonicLock_; }
  void setMonotonicLock(bool on);  // ON: re-clamps handles into segment boxes

  void reset();
  int numPoints() const { return static_cast<int>(pts_.size()); }
  int numSegments() const { return numPoints() - 1; }
  const Node& point(int i) const { return pts_[static_cast<size_t>(i)]; }
  const Node& control(int s) const { return hnd_[static_cast<size_t>(s)]; }
  Node handleNode(int s) const;  // on-curve handle Q = B(0.5), for the UI

  bool movePoint(int i, float x, float y);
  bool moveHandle(int s, float qx, float qy);  // q = desired on-curve position
  bool canAddPoint(float x) const;  // true iff addPoint(x) would succeed
  bool canRemovePoint(int i) const;
  int addPoint(float x);    // exact de Casteljau split at x, y snapped to curve
  bool removePoint(int i);  // merges segments, refit through removed point

  float eval(float t) const;           // exact evaluation, any thread
  void bake(float* dst, int n) const;  // y at uniform x in [0,1]

  juce::ValueTree toValueTree() const;
  bool fromValueTree(const juce::ValueTree& v);  // clamps whatever it reads

private:
  Seg segment(int s) const;
  static Node bezier(const Seg& s, float tau);
  static float solveTau(const Seg& s, float x);
  void clampAll();                       // enforce invariants after bulk load
  int segmentContaining(float x) const;  // shared by canAddPoint and addPoint

  bool monotonicLock_ = true;
  float clampCy(int s, float y) const;  // segment box (lock) or unit square

  std::vector<Node> pts_;  // [0]=(0,0), back=(1,1), x-sorted, y non-decreasing
  std::vector<Node> hnd_;  // stored Bezier control point per segment (in box)
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