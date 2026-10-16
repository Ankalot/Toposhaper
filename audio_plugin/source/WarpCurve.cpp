namespace audio_plugin {

constexpr float kEps = 1e-4f;

float clamp01(float v) {
  return juce::jlimit(0.0f, 1.0f, v);
}

WarpCurve::Node lerp(const WarpCurve::Node& a,
                     const WarpCurve::Node& b,
                     float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

WarpCurve::Node WarpCurve::bezier(const Seg& s, float tau) {
  const float om = 1.0f - tau;
  const float w0 = om * om, w1 = 2.0f * tau * om, w2 = tau * tau;
  return {w0 * s.p0.x + w1 * s.c.x + w2 * s.p1.x,
          w0 * s.p0.y + w1 * s.c.y + w2 * s.p1.y};
}

float WarpCurve::solveTau(const Seg& s, float x) {
  // Bisection on x(tau): monotone because C is clamped into the segment box.
  // (Fixed: compare the actual x coordinate, not a degenerate component.)
  float lo = 0.0f, hi = 1.0f;
  for (int it = 0; it < 24; ++it) {
    const float m = 0.5f * (lo + hi);
    const float om = 1.0f - m;
    const float xm = om * om * s.p0.x + 2.0f * m * om * s.c.x + m * m * s.p1.x;
    if (xm < x)
      lo = m;
    else
      hi = m;
  }
  return 0.5f * (lo + hi);
}

WarpCurve::Node WarpCurve::handleNode(int s) const {
  return bezier(segment(s), 0.5f);  // Q = (C + mid)/2, always on the curve
}

void WarpCurve::reset() {
  pts_ = {{0.0f, 0.0f}, {1.0f, 1.0f}};
  // Default character: the exact parabola y = x^2. A quadratic Bezier with
  // the control point at (0.5, 0) reproduces it exactly (x(t) = t, y(t) = t^2),
  // so the plugin audibly deforms out of the box instead of sitting in a
  // silent identity bypass. Matches the old kDeformPower = 2.0 character.
  hnd_ = {{0.5f, 0.0f}};
}

WarpCurve::Seg WarpCurve::segment(int s) const {
  return {pts_[static_cast<size_t>(s)], hnd_[static_cast<size_t>(s)],
          pts_[static_cast<size_t>(s) + 1]};
}

bool WarpCurve::movePoint(int i, float x, float y) {
  if (i <= 0 || i >= numPoints() - 1)
    return false;  // endpoints immovable
  Node& p = pts_[static_cast<size_t>(i)];
  p.x = juce::jlimit(pts_[static_cast<size_t>(i) - 1].x,
                     pts_[static_cast<size_t>(i) + 1].x, x);
  p.y = monotonicLock_ ? juce::jlimit(pts_[static_cast<size_t>(i) - 1].y,
                                      pts_[static_cast<size_t>(i) + 1].y, y)
                       : clamp01(y);  // fold: free node height in [0,1]
  for (int s : {i - 1, i}) {  // keep neighbouring C inside the new boxes
    if (s < 0 || s >= numSegments())
      continue;
    const Node& a = pts_[static_cast<size_t>(s)];
    const Node& b = pts_[static_cast<size_t>(s) + 1];
    Node& c = hnd_[static_cast<size_t>(s)];
    c.x = juce::jlimit(a.x, b.x, c.x);
    c.y = clampCy(s, c.y);
  }
  return true;
}

bool WarpCurve::moveHandle(int s, float qx, float qy) {
  if (s < 0 || s >= numSegments())
    return false;
  const Node& a = pts_[static_cast<size_t>(s)];
  const Node& b = pts_[static_cast<size_t>(s) + 1];
  const Node mid{0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
  Node c{2.0f * qx - mid.x, 2.0f * qy - mid.y};  // C = 2*Q - mid
  c.x = juce::jlimit(a.x, b.x, c.x);             // monotonicity guard
  c.y = clampCy(s, c.y);
  hnd_[static_cast<size_t>(s)] = c;
  return true;
}

int WarpCurve::segmentContaining(float x) const {
  const float cx = juce::jlimit(kEps, 1.0f - kEps, x);
  for (int i = 0; i < numSegments(); ++i)
    if (cx > pts_[static_cast<size_t>(i)].x + kEps &&
        cx < pts_[static_cast<size_t>(i) + 1].x - kEps)
      return i;
  return -1;
}

bool WarpCurve::canAddPoint(float x) const {
  return numPoints() < kMaxPoints && segmentContaining(x) >= 0;
}

int WarpCurve::addPoint(float x) {
  if (!canAddPoint(x))
    return -1;  // single guard: can* and the mutator cannot drift apart
  const int s = segmentContaining(x);
  const float cx = juce::jlimit(kEps, 1.0f - kEps, x);
  // Exact de Casteljau split: curve shape preserved, new point on the curve
  const Seg seg = segment(s);
  const float tau = solveTau(seg, cx);
  const Node m = bezier(seg, tau);
  const Node hL = lerp(seg.p0, seg.c, tau);
  const Node hR = lerp(seg.c, seg.p1, tau);
  pts_.insert(pts_.begin() + s + 1, m);
  hnd_[static_cast<size_t>(s)] = hL;
  hnd_.insert(hnd_.begin() + s + 1, hR);
  return s + 1;
}

bool WarpCurve::canRemovePoint(int i) const {
  return i > 0 && i < numPoints() - 1 && numPoints() > 2;
}

bool WarpCurve::removePoint(int i) {
  if (!canRemovePoint(i))
    return false;
  const Node p0 = pts_[static_cast<size_t>(i) - 1];
  const Node m = pts_[static_cast<size_t>(i)];
  const Node p1 = pts_[static_cast<size_t>(i) + 1];
  // Refit one handle so the merged segment still passes through m
  const float denom = p1.x - p0.x;
  float tau = 0.5f;
  if (denom > 1e-6f)
    tau = juce::jlimit(0.05f, 0.95f, (m.x - p0.x) / denom);
  const float k = 2.0f * tau * (1.0f - tau);
  Node c{0.5f, 0.5f};
  if (k > 1e-6f) {
    const float om = 1.0f - tau;
    c.x = (m.x - om * om * p0.x - tau * tau * p1.x) / k;
    c.y = (m.y - om * om * p0.y - tau * tau * p1.y) / k;
    c.x = juce::jlimit(p0.x, p1.x, c.x);
    c.y = juce::jlimit(p0.y, p1.y, c.y);
  }
  pts_.erase(pts_.begin() + i);
  hnd_.erase(hnd_.begin() + i);
  hnd_[static_cast<size_t>(i) - 1] = c;
  return true;
}

float WarpCurve::eval(float t) const {
  const float x = clamp01(t);
  int s = 0;
  while (s < numSegments() - 1 && x >= pts_[static_cast<size_t>(s) + 1].x)
    ++s;
  return bezier(segment(s), solveTau(segment(s), x)).y;
}

void WarpCurve::bake(float* dst, int n) const {
  bakeWarpLut(makeWarpSnapshot(*this), dst, n);
}

void WarpCurve::clampAll() {
  if (pts_.size() < 2) {
    reset();
    return;
  }
  pts_.front() = {0.0f, 0.0f};
  pts_.back() = {1.0f, 1.0f};
  for (auto& p : pts_) {
    p.x = clamp01(p.x);
    p.y = clamp01(p.y);
  }
  std::sort(pts_.begin() + 1, pts_.end() - 1,
            [](const Node& a, const Node& b) { return a.x < b.x; });
  if (monotonicLock_)
    for (size_t i = 1; i < pts_.size(); ++i)
      pts_[i].y = std::max(pts_[i].y, pts_[i - 1].y);
  hnd_.resize(pts_.size() - 1);
  for (int s = 0; s < numSegments(); ++s) {
    const Node& a = pts_[static_cast<size_t>(s)];
    const Node& b = pts_[static_cast<size_t>(s) + 1];
    Node& c = hnd_[static_cast<size_t>(s)];
    c.x = juce::jlimit(a.x, b.x, c.x);
    c.y = clampCy(s, c.y);
  }
}

float WarpCurve::clampCy(int s, float y) const {
  const Node& a = pts_[static_cast<size_t>(s)];
  const Node& b = pts_[static_cast<size_t>(s) + 1];
  if (monotonicLock_)
    return juce::jlimit(a.y, b.y, y);  // monotone contract, unchanged
  // Exact range containment: quadratic Bezier stays in [0,1] iff c.y is in
  // these bounds (from y(t*) = a - (a-c)^2 / (a - 2c + b)).
  const float lo = -std::sqrt(a.y * b.y);
  const float hi = 1.0f + std::sqrt((1.0f - a.y) * (1.0f - b.y));
  return juce::jlimit(lo, hi, y);
}

void WarpCurve::setMonotonicLock(bool on) {
  if (on && !monotonicLock_) {
    monotonicLock_ = true;
    clampAll();  // monotonizes node y + returns C into segment boxes
  } else {
    monotonicLock_ = on;
  }
}

juce::ValueTree WarpCurve::toValueTree() const {
  juce::ValueTree t(kStateTag);
  for (int s = 0; s < numSegments(); ++s) {
    juce::ValueTree p("P");
    p.setProperty("x", pts_[static_cast<size_t>(s)].x, nullptr);
    p.setProperty("y", pts_[static_cast<size_t>(s)].y, nullptr);
    t.appendChild(p, nullptr);
    juce::ValueTree h("H");
    h.setProperty("x", hnd_[static_cast<size_t>(s)].x, nullptr);
    h.setProperty("y", hnd_[static_cast<size_t>(s)].y, nullptr);
    t.appendChild(h, nullptr);
  }
  juce::ValueTree p("P");
  p.setProperty("x", pts_.back().x, nullptr);
  p.setProperty("y", pts_.back().y, nullptr);
  t.appendChild(p, nullptr);
  t.setProperty("lock", monotonicLock_, nullptr);
  return t;
}

bool WarpCurve::fromValueTree(const juce::ValueTree& v) {
  std::vector<Node> pts;
  std::vector<Node> hnd;
  for (const auto& child : v) {
    Node n{static_cast<float>(child.getProperty("x", 0.5)),
           static_cast<float>(child.getProperty("y", 0.5))};
    if (child.hasType("P"))
      pts.push_back(n);
    else if (child.hasType("H"))
      hnd.push_back(n);
  }
  if (pts.size() < 2 || hnd.size() + 1 != pts.size()) {
    reset();
    return false;
  }
  pts_ = std::move(pts);
  hnd_ = std::move(hnd);
  monotonicLock_ = static_cast<bool>(v.getProperty("lock", true));
  clampAll();
  return true;
}

WarpSnapshot makeWarpSnapshot(const WarpCurve& c) {
  WarpSnapshot s;
  s.n = c.numPoints();
  for (int i = 0; i < s.n; ++i) {
    s.px[i] = c.point(i).x;
    s.py[i] = c.point(i).y;
  }
  for (int q = 0; q < s.n - 1; ++q) {
    s.hx[q] = c.control(q).x;
    s.hy[q] = c.control(q).y;
  }
  return s;
}

void bakeWarpLut(const WarpSnapshot& s, float* dst, int n) {
  for (int i = 0; i < n; ++i) {
    const float x = static_cast<float>(i) / static_cast<float>(n - 1);
    int seg = 0;
    while (seg < s.n - 2 && x >= s.px[seg + 1])
      ++seg;
    const float x0 = s.px[seg], x1 = s.px[seg + 1], cx = s.hx[seg];
    const float y0 = s.py[seg], y1 = s.py[seg + 1], cy = s.hy[seg];
    float tau = 0.5f;
    if (x1 - x0 > 1e-6f) {
      float lo = 0.0f, hi = 1.0f;
      for (int it = 0; it < 24; ++it) {
        const float m = 0.5f * (lo + hi);
        const float om = 1.0f - m;
        const float xm = om * om * x0 + 2.0f * m * om * cx + m * m * x1;
        if (xm < x)
          lo = m;
        else
          hi = m;
      }
      tau = 0.5f * (lo + hi);
    }
    const float om = 1.0f - tau;
    dst[i] = om * om * y0 + 2.0f * tau * om * cy + tau * tau * y1;
  }
}

void WarpSlot::publish(const WarpCurve& c) {
  const unsigned v = seq_.load(std::memory_order_relaxed);
  seq_.store(v | 1u, std::memory_order_relaxed);
  data_ = makeWarpSnapshot(c);
  seq_.store(v + 2, std::memory_order_release);
}

bool WarpSlot::take(WarpSnapshot& dst, unsigned& lastSeq) const {
  unsigned v0 = seq_.load(std::memory_order_acquire);
  if (v0 == lastSeq)
    return false;
  for (int spin = 0; spin < 64; ++spin) {
    if (v0 & 1u) {
      v0 = seq_.load(std::memory_order_acquire);
      continue;
    }
    dst = data_;
    const unsigned v1 = seq_.load(std::memory_order_acquire);
    if (v1 == v0) {
      lastSeq = v0;
      return true;
    }
    v0 = v1;
  }
  return false;
}

}  // namespace audio_plugin