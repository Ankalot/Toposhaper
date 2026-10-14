namespace audio_plugin {
namespace {
// Printed unit square on the board, in component-local px.
// Component bounds = board interior;
constexpr float kUnitX = 20.0f;
constexpr float kUnitY = 51.0f;
constexpr float kUnitW = 160.0f;
constexpr float kUnitH = 160.0f;
constexpr float kCurveW = 2.6f;  // chalk stroke for the curve
constexpr float kGridW = 1.0f;   // chalk stroke for the half grid

// Widget-private geometry (not shared design tokens)
constexpr float kPointR = 5.0f;
constexpr float kPointRHot = 6.5f;
constexpr float kHandleR = 4.0f;
constexpr float kHandleRHot = 5.5f;

constexpr float kSnapStep = 0.05f;  // Alt-snap grid; includes drawn quarters
float snapped(float v) {
  return std::round(v / kSnapStep) * kSnapStep + 0.0f;  // fold -0.0 to +0.0
}
}  // namespace

WarpCurveEditor::WarpCurveEditor(WarpCurve& curve,
                                 std::function<void()> onChange)
    : curve_(curve), onChange_(std::move(onChange)) {
  startTimerHz(30);  // reflect edits made outside the editor (state load)
}

juce::Rectangle<float> WarpCurveEditor::curveRect() const {
  return {kUnitX, kUnitY, kUnitW, kUnitH};  // the art prints the square
}

juce::Rectangle<float> WarpCurveEditor::readoutRect() const {
  // Handwritten coords live top-right of the unit square, as in the art
  return {kUnitX + kUnitW + 24.0f, kUnitY + 4.0f, 130.0f, 24.0f};
}

void WarpCurveEditor::paint(juce::Graphics& g) {
  const auto r = curveRect();
  if (r.isEmpty())
    return;
  // Quarter grid by default: chalk lines at 0.25 / 0.5 / 0.75 on both axes
  g.setColour(ui::chalk.withAlpha(0.55f));
  for (int i = 1; i < 4; ++i) {
    const float f = static_cast<float>(i) * 0.25f;
    g.drawLine({r.getX() + f * r.getWidth(), r.getY(),
                r.getX() + f * r.getWidth(), r.getBottom()},
               kGridW);
    g.drawLine({r.getX(), r.getBottom() - f * r.getHeight(), r.getRight(),
                r.getBottom() - f * r.getHeight()},
               kGridW);
  }
  // Fine snap grid while Alt is held: fainter chalk, same 0.05 step
  const bool altGrid =
      juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown() &&
      (grab_ != Kind::none ||
       curveRect().contains(getMouseXYRelative().toFloat()));
  if (altGrid) {
    g.setColour(ui::chalk.withAlpha(0.18f));
    for (int i = 1; i < 20; ++i) {
      if (i % 5 == 0)
        continue;  // halves are already drawn
      const float f = static_cast<float>(i) * kSnapStep;
      g.drawLine({r.getX() + f * r.getWidth(), r.getY(),
                  r.getX() + f * r.getWidth(), r.getBottom()},
                 1.0f);
      g.drawLine({r.getX(), r.getBottom() - f * r.getHeight(), r.getRight(),
                  r.getBottom() - f * r.getHeight()},
                 1.0f);
    }
  }
  // Curve as exact quadratic Beziers (same math as the DSP LUT), chalk
  juce::Path curve;
  curve.startNewSubPath(toUI(curve_.point(0)));
  for (int s = 0; s < curve_.numSegments(); ++s)
    curve.quadraticTo(toUI(curve_.control(s)), toUI(curve_.point(s + 1)));
  g.setColour(ui::chalk);
  g.strokePath(curve,
               juce::PathStrokeType(kCurveW, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded));
  // On-curve handles: amber chalk dots - the dots the user drags
  for (int s = 0; s < curve_.numSegments(); ++s) {
    const bool hot = (hover_.kind == Kind::handle && hover_.index == s) ||
                     (grab_ == Kind::handle && grabIndex_ == s);
    const auto q = toUI(curve_.handleNode(s));
    const float rad = hot ? kHandleRHot : kHandleR;
    g.setColour(ui::accentR);
    g.fillEllipse(q.x - rad, q.y - rad, rad * 2.0f, rad * 2.0f);
    if (hot) {  // chalk ring around the grabbed handle
      g.setColour(ui::chalk);
      g.drawEllipse(q.x - rad - 2.0f, q.y - rad - 2.0f, rad * 2.0f + 4.0f,
                    rad * 2.0f + 4.0f, 1.5f);
    }
  }
  // Interior points: plain white chalk dots
  for (int i = 1; i < curve_.numPoints() - 1; ++i) {
    const bool hot = (hover_.kind == Kind::point && hover_.index == i) ||
                     (grab_ == Kind::point && grabIndex_ == i);
    const auto p = toUI(curve_.point(i));
    const float rad = hot ? kPointRHot : kPointR;
    g.setColour(ui::chalk);
    g.fillEllipse(p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
  }
  // Live handwritten readout while dragging, exactly as in the art mock
  if (grab_ != Kind::none && grabIndex_ >= 0) {
    const auto v = (grab_ == Kind::point) ? curve_.point(grabIndex_)
                                          : curve_.handleNode(grabIndex_);
    const juce::String txt =
        "x: " + juce::String(v.x, 2) + "; y: " + juce::String(v.y, 2);
    g.setFont(readoutFont_);
    g.setColour(ui::chalk);
    g.drawText(txt, readoutRect(), juce::Justification::centredLeft, false);
  }
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

WarpCurveEditor::Hit WarpCurveEditor::hitTestCurve(juce::Point<float> p) const {
  Hit best;
  float bestD = ui::hitR;
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
  float bestLine = ui::lineHitR;  // then the curve line -> its segment handle
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
  const auto hit = hitTestCurve(e.position);
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
  auto n = fromUI(e.position + grabOffset_);  // no jump on grab
  if (e.mods.isAltDown()) {
    n.x = snapped(n.x);
    n.y = snapped(n.y);
  }
  if (grab_ == Kind::point) {
    curve_.movePoint(grabIndex_, n.x, n.y);
  } else {
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
  const auto h = hitTestCurve(e.position);
  if (h.kind != hover_.kind || h.index != hover_.index) {
    hover_ = h;
    setMouseCursor(h.kind == Kind::none
                       ? juce::MouseCursor::NormalCursor
                       : juce::MouseCursor::PointingHandCursor);
    repaint();
  }
}

void WarpCurveEditor::mouseDoubleClick(const juce::MouseEvent& e) {
  const auto hit = hitTestCurve(e.position);
  if (hit.kind == Kind::point) {  // double-click on a point removes it
    if (curve_.removePoint(hit.index))
      notify();
    return;
  }
  auto pos = fromUI(e.position);
  if (e.mods.isAltDown())
    pos.x = snapped(pos.x);
  if (curveRect().contains(e.position) && curve_.addPoint(pos.x) >= 0)
    notify();
}

void WarpCurveEditor::mouseExit(const juce::MouseEvent&) {
  hover_ = {};
  setMouseCursor(juce::MouseCursor::NormalCursor);
  repaint();
}

juce::String WarpCurveEditor::getTooltip() {
  // The board speaks only where the curve lives
  if (grab_ == Kind::none &&
      !curveRect().contains(getMouseXYRelative().toFloat()))
    return {};
  return "Drag points and handles; double-click adds or removes points.\n"
         "Alt: snap to the fine grid. Shift: vertical-only bend.";
}
}  // namespace audio_plugin