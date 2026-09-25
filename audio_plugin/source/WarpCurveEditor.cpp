namespace audio_plugin {

namespace {
// Palette (kept local until the global UI palette lands)
const juce::Colour kBg{0xff14161c};
const juce::Colour kFrame{0x808a93a6};
const juce::Colour kGrid{0x14ffffff};
const juce::Colour kDiag{0x26ffffff};
const juce::Colour kCurve{0xff4f7dff};
const juce::Colour kGlow{0x264f7dff};
const juce::Colour kFill{0x144f7dff};
const juce::Colour kPointEdge{0xffe8ecf5};
const juce::Colour kHandle{0xff35d38a};
const juce::Colour kEndpoint{0xff9aa3b5};
constexpr float kHitRadius = 9.0f;
constexpr float kLineHit = 6.0f;
}  // namespace

WarpCurveEditor::WarpCurveEditor(WarpCurve& curve,
                                 std::function<void()> onChange)
    : curve_(curve), onChange_(std::move(onChange)) {
  startTimerHz(30);  // reflect edits made outside the editor (state load)
}

juce::Rectangle<float> WarpCurveEditor::curveRect() const {
  const auto b = getLocalBounds().toFloat().reduced(8.0f);
  const float side = std::min(b.getWidth(), b.getHeight());
  if (side <= 0.0f)
    return {};
  return {b.getCentreX() - side * 0.5f, b.getCentreY() - side * 0.5f, side,
          side};
}

juce::Point<float> WarpCurveEditor::toUI(const WarpCurve::Node& n) const {
  const auto r = curveRect();
  return {r.getX() + n.x * r.getWidth(), r.getBottom() - n.y * r.getHeight()};
}

WarpCurve::Node WarpCurveEditor::fromUI(juce::Point<float> p) const {
  const auto r = curveRect();
  WarpCurve::Node n;
  n.x = (p.x - r.getX()) / r.getWidth();
  n.y = (r.getBottom() - p.y) / r.getHeight();
  return n;
}

float WarpCurveEditor::distToSegmentCurve(juce::Point<float> p, int s) const {
  const auto a = toUI(curve_.point(s));
  const auto c = toUI(curve_.control(s));
  const auto b = toUI(curve_.point(s + 1));
  float best = 1.0e9f;
  for (int i = 0; i <= 16; ++i) {
    const float t = static_cast<float>(i) / 16.0f;
    const float om = 1.0f - t;
    const juce::Point<float> q{
        om * om * a.x + 2.0f * t * om * c.x + t * t * b.x,
        om * om * a.y + 2.0f * t * om * c.y + t * t * b.y};
    best = std::min(best, p.getDistanceFrom(q));
  }
  return best;
}

WarpCurveEditor::Hit WarpCurveEditor::hitTest(juce::Point<float> p) const {
  Hit best;
  float bestD = kHitRadius;
  for (int i = 1; i < curve_.numPoints() - 1; ++i) {  // interior points first
    const float d = p.getDistanceFrom(toUI(curve_.point(i)));
    if (d < bestD) {
      bestD = d;
      best = {Kind::point, i};
    }
  }
  for (int s = 0; s < curve_.numSegments(); ++s) {  // then on-curve handles
    const float d = p.getDistanceFrom(toUI(curve_.handleNode(s)));
    if (d < bestD) {
      bestD = d;
      best = {Kind::handle, s};
    }
  }
  if (best.kind != Kind::none)
    return best;
  float bestLine = kLineHit;  // then the curve line -> its segment's handle
  for (int s = 0; s < curve_.numSegments(); ++s) {
    const float d = distToSegmentCurve(p, s);
    if (d < bestLine) {
      bestLine = d;
      best = {Kind::handle, s};
    }
  }
  return best;
}

void WarpCurveEditor::notify() {
  if (onChange_)
    onChange_();
  repaint();
}

void WarpCurveEditor::timerCallback() {
  repaint();
}

void WarpCurveEditor::mouseDown(const juce::MouseEvent& e) {
  const auto hit = hitTest(e.position);
  grab_ = hit.kind;
  grabIndex_ = hit.index;
  if (hit.kind == Kind::point) {
    grabOffset_ = toUI(curve_.point(hit.index)) - e.position;
  } else if (hit.kind == Kind::handle) {
    grabOffset_ = toUI(curve_.handleNode(hit.index)) - e.position;
    lockedHandleX_ = curve_.handleNode(hit.index).x;
  }
  repaint();
}

void WarpCurveEditor::mouseDrag(const juce::MouseEvent& e) {
  if (grab_ == Kind::none)
    return;
  if (grab_ == Kind::point) {
    const auto n = fromUI(e.position + grabOffset_);  // no jump on grab
    curve_.movePoint(grabIndex_, n.x, n.y);
  } else {
    auto n = fromUI(e.position + grabOffset_);
    if (e.mods.isShiftDown())
      n.x = lockedHandleX_;  // Serum-style vertical-only bend
    curve_.moveHandle(grabIndex_, n.x, n.y);
  }
  notify();
}

void WarpCurveEditor::mouseUp(const juce::MouseEvent&) {
  grab_ = Kind::none;
  grabIndex_ = -1;
  repaint();
}

void WarpCurveEditor::mouseMove(const juce::MouseEvent& e) {
  const auto h = hitTest(e.position);
  if (h.kind != hover_.kind || h.index != hover_.index) {
    hover_ = h;
    setMouseCursor(h.kind == Kind::none
                       ? juce::MouseCursor::NormalCursor
                       : juce::MouseCursor::PointingHandCursor);
    repaint();
  }
}

void WarpCurveEditor::mouseDoubleClick(const juce::MouseEvent& e) {
  const auto hit = hitTest(e.position);
  if (hit.kind == Kind::point) {  // double-click on a point removes it
    if (curve_.removePoint(hit.index))
      notify();
    return;
  }
  if (curveRect().contains(e.position) &&  // double-click on empty adds one
      curve_.addPoint(fromUI(e.position).x) >= 0)
    notify();
}

void WarpCurveEditor::mouseExit(const juce::MouseEvent&) {
  hover_ = {};
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
}

void WarpCurveEditor::paint(juce::Graphics& g) {
  const auto bounds = getLocalBounds().toFloat();
  const auto r = curveRect();
  if (r.isEmpty())
    return;

  // Panel
  g.setColour(kBg);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(kFrame);
  g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

  // Quarter grid
  g.setColour(kGrid);
  for (int i = 1; i < 4; ++i) {
    const float f = static_cast<float>(i) * 0.25f;
    g.drawLine({r.getX() + f * r.getWidth(), r.getY(),
                r.getX() + f * r.getWidth(), r.getBottom()});
    g.drawLine({r.getX(), r.getBottom() - f * r.getHeight(), r.getRight(),
                r.getBottom() - f * r.getHeight()});
  }

  // Identity diagonal, dashed
  g.setColour(kDiag);
  for (int i = 0; i < 24; i += 2) {
    const float t0 = static_cast<float>(i) / 24.0f;
    const float t1 = static_cast<float>(i + 1) / 24.0f;
    g.drawLine(
        {r.getX() + t0 * r.getWidth(), r.getBottom() - t0 * r.getHeight(),
         r.getX() + t1 * r.getWidth(), r.getBottom() - t1 * r.getHeight()},
        1.0f);
  }

  // Curve as exact quadratic Beziers (same math as the DSP LUT)
  juce::Path curve;
  curve.startNewSubPath(toUI(curve_.point(0)));
  for (int s = 0; s < curve_.numSegments(); ++s)
    curve.quadraticTo(toUI(curve_.control(s)), toUI(curve_.point(s + 1)));

  {  // soft fill under the curve
    juce::Path fill = curve;
    fill.lineTo(r.getRight(), r.getBottom());
    fill.lineTo(r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour(kFill);
    g.fillPath(fill);
  }

  // Control polygon + hollow C marker of the hovered/dragged segment
  const int activeSeg = grab_ == Kind::handle
                            ? grabIndex_
                            : (hover_.kind == Kind::handle ? hover_.index : -1);
  if (activeSeg >= 0) {
    const auto a = toUI(curve_.point(activeSeg));
    const auto c = toUI(curve_.control(activeSeg));
    const auto b = toUI(curve_.point(activeSeg + 1));
    g.setColour(kHandle.withAlpha(0.35f));
    g.drawLine({a, c});
    g.drawLine({c, b});
    g.setColour(kHandle.withAlpha(0.6f));
    g.drawEllipse(c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f, 1.2f);
  }

  // Glow + stroke
  g.setColour(kGlow);
  g.strokePath(curve, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
  g.setColour(kCurve);
  g.strokePath(curve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

  // On-curve handles: the dots the user actually drags
  for (int s = 0; s < curve_.numSegments(); ++s) {
    const bool hot = (hover_.kind == Kind::handle && hover_.index == s) ||
                     (grab_ == Kind::handle && grabIndex_ == s);
    const auto q = toUI(curve_.handleNode(s));
    const float rad = hot ? 5.5f : 4.0f;
    g.setColour(kHandle);
    g.fillEllipse(q.x - rad, q.y - rad, rad * 2.0f, rad * 2.0f);
    g.setColour(kBg.withAlpha(0.9f));
    g.fillEllipse(q.x - 1.5f, q.y - 1.5f, 3.0f, 3.0f);
  }

  // Fixed endpoints: small diamonds (visibly not draggable)
  for (const int i : {0, curve_.numPoints() - 1}) {
    const auto p = toUI(curve_.point(i));
    juce::Path diamond;
    diamond.startNewSubPath(p.x, p.y - 4.5f);
    diamond.lineTo(p.x + 4.5f, p.y);
    diamond.lineTo(p.x, p.y + 4.5f);
    diamond.lineTo(p.x - 4.5f, p.y);
    diamond.closeSubPath();
    g.setColour(kEndpoint);
    g.fillPath(diamond);
  }

  // Interior points
  for (int i = 1; i < curve_.numPoints() - 1; ++i) {
    const bool hot = (hover_.kind == Kind::point && hover_.index == i) ||
                     (grab_ == Kind::point && grabIndex_ == i);
    const auto p = toUI(curve_.point(i));
    const float rad = hot ? 6.5f : 5.0f;
    g.setColour(kCurve);
    g.fillEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
    g.setColour(kPointEdge);
    g.drawEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 1.6f);
  }
}

}  // namespace audio_plugin