namespace audio_plugin {
    
namespace {
constexpr float kEps = 1e-4f;

float clamp01(float v) {
  return juce::jlimit(0.0f, 1.0f, v);
}

WarpCurve::Node lerp(const WarpCurve::Node& a,
                     const WarpCurve::Node& b,
                     float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

WarpCurve::Node bezier(const WarpCurve::Seg& s, float tau) {
  const float om = 1.0f - tau;
  const float w0 = om * om, w1 = 2.0f * tau * om, w2 = tau * tau;
  return {w0 * s.p0.x + w1 * s.c.x + w2 * s.p1.x,
          w0 * s.p0.y + w1 * s.c.y + w2 * s.p1.y};
}
}  // namespace

void WarpCurve::reset() {
  pts_ = {{0.0f, 0.0f}, {1.0f, 1.0f}};
  hnd_ = {{0.5f, 0.5f}};
}

WarpCurve::Seg WarpCurve::segment(int s) const {
  return {pts_[static_cast<size_t>(s)], hnd_[static_cast<size_t>(s)],
          pts_[static_cast<size_t>(s) + 1]};
}

float WarpCurve::solveTau(const Seg& s, float x) {
  // x(tau) is monotone because the handle is clamped into the segment box
  float lo = 0.0f, hi = 1.0f;
  for (int it = 0; it < 24; ++it) {
    const float m = 0.5f * (lo + hi);
    if (bezier({{s.p0.x, 0.0f}, {s.c.x, 0.0f}, {s.p1.x, 0.0f}}, m).y < x)
      lo = m;
    else
      hi = m;
  }
  return 0.5f * (lo + hi);
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

void WarpCurve::clampHandlesAround(int i) {
  for (int s : {i - 1, i}) {
    if (s < 0 || s >= numSegments())
      continue;
    const Node& a = pts_[static_cast<size_t>(s)];
    const Node& b = pts_[static_cast<size_t>(s) + 1];
    Node& h = hnd_[static_cast<size_t>(s)];
    h.x = juce::jlimit(a.x, b.x, h.x);
    h.y = juce::jlimit(a.y, b.y, h.y);
  }
}

bool WarpCurve::movePoint(int i, float x, float y) {
  if (i <= 0 || i >= numPoints() - 1)
    return false;  // endpoints immovable
  Node& p = pts_[static_cast<size_t>(i)];
  p.x = juce::jlimit(pts_[static_cast<size_t>(i) - 1].x,
                     pts_[static_cast<size_t>(i) + 1].x, x);
  p.y = juce::jlimit(pts_[static_cast<size_t>(i) - 1].y,
                     pts_[static_cast<size_t>(i) + 1].y, y);
  clampHandlesAround(i);
  return true;
}

bool WarpCurve::moveHandle(int s, float x, float y) {
  if (s < 0 || s >= numSegments())
    return false;
  const Node& a = pts_[static_cast<size_t>(s)];
  const Node& b = pts_[static_cast<size_t>(s) + 1];
  Node& h = hnd_[static_cast<size_t>(s)];
  h.x = juce::jlimit(a.x, b.x, x);
  h.y = juce::jlimit(a.y, b.y, y);
  return true;
}

int WarpCurve::addPoint(float x) {
  if (numPoints() >= kMaxPoints)
    return -1;
  const float cx = juce::jlimit(kEps, 1.0f - kEps, x);
  int s = -1;
  for (int i = 0; i < numSegments(); ++i) {
    if (cx > pts_[static_cast<size_t>(i)].x + kEps &&
        cx < pts_[static_cast<size_t>(i) + 1].x - kEps) {
      s = i;
      break;
    }
  }
  if (s < 0)
    return -1;
  // Exact split: de Casteljau at the parameter where the segment crosses x
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

bool WarpCurve::removePoint(int i) {
  if (i <= 0 || i >= numPoints() - 1 || numPoints() <= 2)
    return false;
  const Node p0 = pts_[static_cast<size_t>(i) - 1];
  const Node m = pts_[static_cast<size_t>(i)];
  const Node p1 = pts_[static_cast<size_t>(i) + 1];
  // Re-fit one handle so the merged segment still passes through m
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
  hnd_.erase(hnd_.begin() + i);          // drop right handle...
  hnd_[static_cast<size_t>(i) - 1] = c;  // ...re-fit the left one
  return true;
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
  for (size_t i = 1; i < pts_.size(); ++i)
    pts_[i].y = std::max(pts_[i].y, pts_[i - 1].y);
  hnd_.resize(pts_.size() - 1);
  for (int s = 0; s < numSegments(); ++s)
    clampHandlesAround(s);
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
    s.hx[q] = c.handle(q).x;
    s.hy[q] = c.handle(q).y;
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
  seq_.store(v | 1u, std::memory_order_relaxed);  // odd = write in progress
  data_ = makeWarpSnapshot(c);
  seq_.store(v + 2, std::memory_order_release);
}

bool WarpSlot::take(WarpSnapshot& dst, unsigned& lastSeq) const {
  unsigned v0 = seq_.load(std::memory_order_acquire);
  if (v0 == lastSeq)
    return false;  // nothing new
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
  return false;  // contended: skip, take next block
}

}  // namespace audio_plugin