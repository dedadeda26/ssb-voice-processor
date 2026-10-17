#include "Knob.h"
#include "UiStyle.h"
#include <cmath>

Knob::Knob(const juce::String& name)
    : accentColour(UiStyle::accentGlow)
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setPopupDisplayEnabled(false, false, nullptr);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                        juce::MathConstants<float>::pi * 2.75f, true);
    setVelocityBasedMode(false);
    setMouseDragSensitivity(180);
    juce::Component::setName(name);
}

void Knob::mouseEnter(const juce::MouseEvent&) { hovered = true; repaint(); }
void Knob::mouseExit(const juce::MouseEvent&) { hovered = false; repaint(); }
void Knob::mouseDown(const juce::MouseEvent& e)
{
    dragging = true;
    juce::Slider::mouseDown(e);
    repaint();
}
void Knob::mouseUp(const juce::MouseEvent& e)
{
    dragging = false;
    juce::Slider::mouseUp(e);
    repaint();
}

void Knob::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto nameArea  = bounds.removeFromBottom(18.0f).toNearestInt();
    auto valueArea = bounds.removeFromBottom(20.0f).toNearestInt();
    auto knobArea  = bounds.reduced(2.0f);

    if (knobArea.getWidth() < 20.0f || knobArea.getHeight() < 20.0f)
    {
        g.setColour(UiStyle::textDim);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(getTextFromValue(getValue()),
                   valueArea, juce::Justification::centred, false);
        return;
    }

    float d = juce::jmin(knobArea.getWidth(), knobArea.getHeight());
    if (d < 26.0f) d = 26.0f;

    float outerR = d * 0.5f;
    float ringR  = outerR - 2.0f;
    float discR  = outerR - 13.0f;
    if (discR < 7.0f) discR = 7.0f;

    auto centre = knobArea.getCentre();

    bool active = hovered || dragging;
    juce::Colour ac = accentColour;

    double minV = getMinimum();
    double maxV = getMaximum();
    double range = juce::jmax(1e-9, maxV - minV);
    double ratio = juce::jlimit(0.0, 1.0, (getValue() - minV) / range);

    const float startDeg = -135.0f;
    const float endDeg   =  135.0f;
    const float valDeg   = startDeg + (float)ratio * 270.0f;

    // ===== 环形边缘渗光（从圆盘外缘向外扩散） =====
    // 这部分在圆盘之前绘制，圆盘会覆盖内侧，只留圆盘外的环形光
    {
        const int layers = 14;
        float startR = discR - 1.0f;      // 稍微压到圆盘边缘一点
        for (int i = 0; i < layers; ++i)
        {
            float r = startR + i * 2.4f;
            // 透明度：最内圈最高，向外快速衰减
            float t = (float)i / (float)layers;
            float a = (active ? 0.28f : 0.20f) * std::pow(1.0f - t, 1.8f);
            if (a < 0.002f) break;

            juce::ColourGradient glow(
                ac.withAlpha(a),
                centre.x, centre.y,
                ac.withAlpha(0.0f),
                centre.x + r, centre.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        }
    }

    // 底弧
    {
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, ringR, ringR, 0.0f,
                        juce::degreesToRadians(startDeg),
                        juce::degreesToRadians(endDeg), true);
        g.setColour(UiStyle::knobRing.withAlpha(0.55f));
        g.strokePath(p, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // 进度弧
    if (ratio > 0.002f)
    {
        juce::Path p;
        p.addCentredArc(centre.x, centre.y, ringR, ringR, 0.0f,
                        juce::degreesToRadians(startDeg),
                        juce::degreesToRadians(valDeg), true);
        g.setColour(ac.withAlpha(active ? 0.35f : 0.24f));
        g.strokePath(p, juce::PathStrokeType(5.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
        g.setColour(ac);
        g.strokePath(p, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    // 圆盘阴影
    {
        juce::Path sh;
        sh.addEllipse(centre.x - discR, centre.y - discR + 1.5f,
                      discR * 2.0f, discR * 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillPath(sh);
    }

    // 圆盘本体
    {
        g.setColour(UiStyle::knobMid);
        g.fillEllipse(centre.x - discR, centre.y - discR,
                      discR * 2.0f, discR * 2.0f);
    }

    // 圆盘边缘光（与渗光呼应）
    {
        g.setColour(ac.withAlpha(active ? 0.45f : 0.28f));
        g.drawEllipse(centre.x - discR, centre.y - discR,
                      discR * 2.0f, discR * 2.0f, 1.4f);
    }
    {
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.drawEllipse(centre.x - discR + 1.0f, centre.y - discR + 1.0f,
                      discR * 2.0f - 2.0f, discR * 2.0f - 2.0f, 0.8f);
    }

    // 指针
    {
        float angle = juce::degreesToRadians(valDeg);
        float r1 = discR * 0.42f;
        float r2 = discR * 0.86f;
        float x1 = centre.x + std::sin(angle) * r1;
        float y1 = centre.y - std::cos(angle) * r1;
        float x2 = centre.x + std::sin(angle) * r2;
        float y2 = centre.y - std::cos(angle) * r2;
        g.setColour(UiStyle::knobPtr.withAlpha(0.95f));
        g.drawLine(x1, y1, x2, y2, 2.2f);
    }

    // 数值
    {
        g.setColour(active ? UiStyle::text : UiStyle::textDim);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(getTextFromValue(getValue()),
                   valueArea, juce::Justification::centred, false);
    }

    // 名称
    {
        g.setColour(active ? UiStyle::text : UiStyle::textDim);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText(getKnobName(), nameArea, juce::Justification::centred, false);
    }
}
