// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace spStyle {
inline const juce::Colour silver{0xffd1d8d3}, shell{0xff1b2224},
    panel{0xff252d2f}, well{0xff151d20}, ivory{0xffe9ece3}, ink{0xff172023},
    muted{0xffa8b4af}, orange{0xffff713b}, red{0xffe34a32}, green{0xffa5d6ac};
inline juce::Font body(float size) {
  return juce::Font(
      juce::FontOptions("Helvetica Neue", size, juce::Font::plain));
}
inline juce::Font heading(float size) {
  return juce::Font(
      juce::FontOptions("Helvetica Neue", "Condensed Bold", size));
}
inline juce::Font display(float size) {
  return juce::Font(juce::FontOptions("Menlo", size, juce::Font::plain));
}
class LookAndFeel : public juce::LookAndFeel_V4 {
public:
  LookAndFeel() {
    using namespace juce;
    setDefaultSansSerifTypefaceName("Helvetica Neue");
    setColour(TextButton::buttonColourId, ivory);
    setColour(TextButton::buttonOnColourId, orange);
    setColour(TextButton::textColourOffId, ink);
    setColour(TextButton::textColourOnId, ink);
    setColour(ComboBox::backgroundColourId, ivory);
    setColour(ComboBox::textColourId, ink);
    setColour(ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(ComboBox::arrowColourId, ink);
    setColour(ComboBox::focusedOutlineColourId, orange);
    setColour(TextEditor::backgroundColourId, ivory);
    setColour(TextEditor::textColourId, ink);
    setColour(TextEditor::outlineColourId, ink);
    setColour(TextEditor::focusedOutlineColourId, orange);
    setColour(TextEditor::highlightColourId, orange);
    setColour(TextEditor::highlightedTextColourId, ink);
    setColour(CaretComponent::caretColourId, ink);
    setColour(Label::textColourId, ivory);
    setColour(Slider::backgroundColourId, muted.withMultipliedBrightness(.55f));
    setColour(Slider::trackColourId, muted.withMultipliedBrightness(.55f));
    setColour(Slider::textBoxBackgroundColourId, ivory);
    setColour(Slider::textBoxTextColourId, ink);
    setColour(Slider::textBoxHighlightColourId, orange);
    setColour(Slider::textBoxOutlineColourId, ink);
    setColour(TooltipWindow::backgroundColourId, silver);
    setColour(TooltipWindow::textColourId, ink);
    setColour(TooltipWindow::outlineColourId, ink);
    setColour(PopupMenu::backgroundColourId, ivory);
    setColour(PopupMenu::textColourId, ink);
    setColour(PopupMenu::highlightedBackgroundColourId, orange);
    setColour(PopupMenu::highlightedTextColourId, ink);
    setColour(ScrollBar::thumbColourId, muted.withAlpha(.65f));
    setColour(AlertWindow::backgroundColourId, panel);
    setColour(AlertWindow::textColourId, ivory);
    setColour(AlertWindow::outlineColourId, silver);
  }
  juce::TextLayout tooltipLayout(const juce::String &text) const {
    juce::AttributedString content;
    content.setJustification(juce::Justification::topLeft);
    content.setWordWrap(juce::AttributedString::WordWrap::byWord);
    content.append(text, body(13).boldened(),
                   findColour(juce::TooltipWindow::textColourId));
    juce::TextLayout layout;
    layout.createLayout(content, 400);
    return layout;
  }
  juce::Rectangle<int>
  getTooltipBounds(const juce::String &text, juce::Point<int> position,
                   juce::Rectangle<int> parentArea) override {
    const auto layout = tooltipLayout(text);
    const int width = juce::roundToInt(std::ceil(layout.getWidth())) + 24;
    const int height = juce::roundToInt(std::ceil(layout.getHeight())) + 20;
    return juce::Rectangle<int>(
               position.x > parentArea.getCentreX() ? position.x - width - 12
                                                    : position.x + 24,
               position.y > parentArea.getCentreY() ? position.y - height - 6
                                                    : position.y + 6,
               width, height)
        .constrainedWithin(parentArea);
  }
  void drawTooltip(juce::Graphics &g, const juce::String &text, int width,
                   int height) override {
    const juce::Rectangle<float> bounds(0, 0, float(width), float(height));
    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(bounds, 5);
    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(.5f), 5, 1);
    tooltipLayout(text).draw(g, bounds.reduced(12, 10));
  }
  juce::Font getTextButtonFont(juce::TextButton &, int) override {
    return heading(16);
  }
  juce::Font getComboBoxFont(juce::ComboBox &) override { return body(14); }
  juce::Label *createSliderTextBox(juce::Slider &slider) override {
    auto *label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textWhenEditingColourId, ink);
    label->setColour(juce::Label::backgroundWhenEditingColourId, ivory);
    label->setColour(juce::Label::outlineWhenEditingColourId, orange);
    label->setColour(juce::TextEditor::highlightedTextColourId, ink);
    return label;
  }
  static juce::Rectangle<float> buttonFace(juce::Button &button) {
    return button.getLocalBounds().toFloat().reduced(3, 1);
  }
  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &, bool over,
                            bool down) override {
    const auto area = buttonFace(button);
    auto face = button.getToggleState() ? orange : ivory;
    if (over)
      face = face.brighter(.1f);
    if (down)
      face = face.darker(.15f);
    if (!button.isEnabled())
      face = face.withAlpha(.32f);
    g.setColour(face);
    g.fillRoundedRectangle(area, 2);
    if (button.hasKeyboardFocus(true)) {
      g.setColour(orange);
      g.drawRoundedRectangle(area.reduced(1), 2, 2);
    }
  }
  void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool,
                      bool) override {
    const auto face = buttonFace(button);
    juce::GlyphArrangement text;
    text.addLineOfText(heading(16), button.getButtonText(), 0, 0);
    juce::Path outlines;
    text.createPath(outlines);
    const auto glyphBounds = outlines.getBounds();
    if (glyphBounds.isEmpty())
      return;
    // Centre visible glyphs within the flat face, with equal vertical padding.
    const float scale =
        std::min({1.f, (face.getWidth() - 12) / glyphBounds.getWidth(),
                  (face.getHeight() - 10) / glyphBounds.getHeight()});
    g.setColour(button.isEnabled() ? ink : silver.withAlpha(.55f));
    text.draw(g, juce::AffineTransform::scale(scale).translated(
                     face.getCentreX() - glyphBounds.getCentreX() * scale,
                     face.getCentreY() - glyphBounds.getCentreY() * scale));
  }
  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        bool over, bool) override {
    auto box =
        juce::Rectangle<float>(3, (button.getHeight() - 17.f) / 2, 17, 17);
    const float alpha = button.isEnabled() ? 1.f : .4f;
    g.setColour(ink.withAlpha(alpha));
    g.fillRoundedRectangle(box, 2);
    g.setColour((button.getToggleState() ? red : ivory).withAlpha(alpha));
    g.fillRoundedRectangle(box.reduced(2), 1);
    if (button.getToggleState()) {
      juce::Path tick;
      tick.startNewSubPath(box.getX() + 4, box.getY() + 8);
      tick.lineTo(box.getX() + 7, box.getY() + 12);
      tick.lineTo(box.getX() + 13, box.getY() + 5);
      g.setColour(ivory.withAlpha(alpha));
      g.strokePath(tick, juce::PathStrokeType(2));
    }
    if (over || button.hasKeyboardFocus(true)) {
      g.setColour(red.withAlpha(alpha));
      g.drawRoundedRectangle(box.expanded(1), 2, 1);
    }
    g.setColour(ivory.withAlpha(alpha));
    g.setFont(heading(16));
    g.drawText(button.getButtonText(), 26, 0, button.getWidth() - 26,
               button.getHeight(), juce::Justification::centredLeft);
  }
};
} // namespace spStyle
