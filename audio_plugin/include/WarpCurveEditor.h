#pragma once
#include <functional>

namespace audio_plugin {

// Serum-style editor for the monotone warp curve: fixed endpoints, draggable
// interior points, one ON-CURVE handle per segment (the Bezier midpoint
// Q = B(0.5)); dragging it in 2D bends the segment asymmetrically, Shift
// locks vertical. Double-click adds/removes points. All clamps live in
// WarpCurve, so the editor cannot break monotonicity.
class WarpCurveEditor : public juce::Component, private juce::Timer {
public:
  // onChange may be passed here or later via setOnChange()
  WarpCurveEditor(WarpCurve& curve, std::function<void()> onChange = {});
  void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  void mouseDoubleClick(const juce::MouseEvent&) override;
  void mouseExit(const juce::MouseEvent&) override;

private:
  enum class Kind { none, point, handle };
  struct Hit {
    Kind kind = Kind::none;
    int index = -1;
  };

  juce::Rectangle<float> curveRect() const;  // largest inscribed square
  juce::Point<float> toUI(const WarpCurve::Node& n) const;
  WarpCurve::Node fromUI(juce::Point<float> p) const;
  Hit hitTest(juce::Point<float> p) const;
  float distToSegmentCurve(juce::Point<float> p, int s) const;
  void notify();
  void timerCallback() override;  // catch external edits (state/preset load)

  WarpCurve& curve_;
  std::function<void()> onChange_;
  Kind grab_ = Kind::none;
  int grabIndex_ = -1;
  juce::Point<float> grabOffset_;
  float lockedHandleX_ = 0.5f;  // Shift-drag vertical lock
  Hit hover_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WarpCurveEditor)
};

}  // namespace audio_plugin