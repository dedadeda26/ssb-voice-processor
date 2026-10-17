#include "ModuleSelector.h"
#include "Localization.h"
#include "UiStyle.h"
#include <cmath>

namespace {
    juce::String getNodeDisplayKey(const juce::String& internalName)
    {
        if (internalName == "Input Gain")  return "node.inputGain";
        if (internalName == "High-Pass")   return "node.hpf";
        if (internalName == "Noise Gate")  return "node.gate";
        if (internalName == "Spectral NR") return "node.spectralNr";
        if (internalName == "EQ")          return "node.eq";
        if (internalName == "Multi-Band")  return "node.mbComp";
        if (internalName == "Compressor")  return "node.comp";
        if (internalName == "Saturation")  return "node.sat";
        if (internalName == "De-Esser")    return "node.deEsser";
        if (internalName == "Limiter")     return "node.limiter";
        if (internalName == "Auto Level")  return "node.autoLevel";
        if (internalName == "Band-Limit")  return "node.bandLimit";
        if (internalName == "Output Gain") return "node.outputGain";
        return internalName;
    }
}

ModuleSelector::ModuleSelector()
{
    setInterceptsMouseClicks(true, false);
}

void ModuleSelector::setModules(const juce::StringArray& nodeNames)
{
    items.clear();
    for (auto& n : nodeNames)
    {
        Item it;
        it.nodeName = n;
        it.displayKey = getNodeDisplayKey(n);
        items.push_back(it);
    }
    resized();
    repaint();
}

void ModuleSelector::setSelected(int index)
{
    if (index < 0 || index >= (int)items.size()) return;
    selected = index;
    repaint();
}

void ModuleSelector::setNodeBypassed(int index, bool bypassed)
{
    if (index < 0 || index >= (int)items.size()) return;
    items[(size_t)index].on = !bypassed;
    repaint();
}

void ModuleSelector::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0x78000000));
    g.fillRoundedRectangle(r, 12.0f);
    g.setColour(UiStyle::cardBorder.withAlpha(0.45f));
    g.drawRoundedRectangle(r, 12.0f, 1.0f);

    // ============================================================
    // 1) 先画模块之间的"流程箭头"
    //    先画箭头，后画图标 —— 图标压在上层，层次干净
    // ============================================================
    for (int i = 0; i + 1 < (int)items.size(); ++i)
    {
        auto& a = items[(size_t)i];
        auto& b = items[(size_t)(i + 1)];

        float x1 = (float) a.bounds.getRight() + 2.0f;
        float x2 = (float) b.bounds.getX()     - 2.0f;
        float y  = ((float) a.bounds.getCentreY() +
                    (float) b.bounds.getCentreY()) * 0.5f;
        if (x2 <= x1 + 4.0f) continue;

        // 两端都启用时高亮，任一被 bypass 则变灰暗
        const bool active = a.on && b.on;

        juce::Colour arrowCol = active ? UiStyle::accentGlow
                                       : UiStyle::textFaint;
        float alpha = active ? 1.0f : 0.55f;

        // ---- 横线：暗 → 亮，表示信号流向 ----
        juce::ColourGradient grad(
            arrowCol.withAlpha(0.20f * alpha), x1, y,
            arrowCol.withAlpha(0.85f * alpha), x2, y,
            false);
        g.setGradientFill(grad);
        g.drawLine(x1, y, x2 - 4.5f, y, 1.3f);

        // ---- 箭头三角 ----
        juce::Path arrow;
        arrow.startNewSubPath(x2, y);
        arrow.lineTo(x2 - 5.0f, y - 3.2f);
        arrow.lineTo(x2 - 5.0f, y + 3.2f);
        arrow.closeSubPath();

        g.setColour(arrowCol.withAlpha(0.95f * alpha));
        g.fillPath(arrow);
    }

    // ============================================================
    // 2) 再画各个模块的图标 + 标签
    // ============================================================
    for (int i = 0; i < (int)items.size(); ++i)
    {
        auto& it = items[(size_t)i];
        bool isSel = (i == selected);
        bool isHov = (i == hovered);
        bool on = it.on;

        auto ib = it.bounds.toFloat();
        juce::Colour cat = UiStyle::categoryColour(it.nodeName);

        // 选中：纯色块，无渐变
        if (isSel)
        {
            g.setColour(cat.withAlpha(0.20f));
            g.fillRoundedRectangle(ib, 8.0f);
            g.setColour(cat.withAlpha(0.72f));
            g.drawRoundedRectangle(ib.reduced(0.5f), 8.0f, 1.2f);
        }
        else if (isHov)
        {
            g.setColour(cat.withAlpha(0.12f));
            g.fillRoundedRectangle(ib, 8.0f);
        }

        auto iconBox = ib.withTrimmedBottom(ib.getHeight() * 0.34f);
        float iconSize = juce::jmin(iconBox.getWidth() * 0.72f,
                                    iconBox.getHeight() * 0.86f);
        if (iconSize < 24.0f) iconSize = 24.0f;
        auto iconArea = juce::Rectangle<float>(
            iconBox.getCentreX() - iconSize * 0.5f,
            iconBox.getCentreY() - iconSize * 0.5f + 4.0f,
            iconSize, iconSize);

        juce::Colour ic = (isSel || isHov) ? cat : UiStyle::textDim;
        if (!on)
            ic = ic.withAlpha(0.35f);

        drawNodeIcon(g, iconArea, it.nodeName, ic);

        juce::String label = Loc::t(it.displayKey);
        g.setColour(isSel ? UiStyle::text
                          : (on ? UiStyle::textDim : UiStyle::textFaint.withAlpha(0.5f)));
        g.setFont(juce::Font(juce::FontOptions(12.0f,
                                               isSel ? juce::Font::bold
                                                     : juce::Font::plain)));
        auto textArea = ib.removeFromBottom(ib.getHeight() * 0.30f);
        g.drawFittedText(label, textArea.toNearestInt(),
                         juce::Justification::centred, 2, 0.65f);
    }
}

void ModuleSelector::resized()
{
    if (items.empty()) return;
    auto r = getLocalBounds().reduced(8);

    const int n = (int) items.size();

    // 模块多了（现在是 13 个）以后 52px 的最小宽度会让最后一个模块被裁到卡片外，
    // 于是"输出增益"点不到。箭头缝与最小宽度都随模块数收紧，保证全部放得下。
    const int arrowGap = n > 10 ? 12 : 20;
    const int totalArrowGap = arrowGap * juce::jmax(0, n - 1);

    // 可用宽度减掉所有箭头缝，剩下的分给每个模块
    int available = r.getWidth() - totalArrowGap;
    int itemW = juce::jlimit(40, 96, available / juce::jmax(1, n));

    int totalW = itemW * n + totalArrowGap;
    int startX = r.getX() + juce::jmax(0, (r.getWidth() - totalW) / 2);

    for (int i = 0; i < n; ++i)
    {
        items[(size_t)i].bounds = juce::Rectangle<int>(
            startX + i * (itemW + arrowGap),
            r.getY() + 8,
            itemW,
            r.getHeight() - 8);
    }
}

void ModuleSelector::mouseMove(const juce::MouseEvent& e)
{
    int oldHover = hovered;
    hovered = -1;
    for (int i = 0; i < (int)items.size(); ++i)
    {
        if (items[(size_t)i].bounds.contains(e.getPosition()))
        {
            hovered = i;
            break;
        }
    }
    if (hovered != oldHover) repaint();
}

void ModuleSelector::mouseExit(const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}

void ModuleSelector::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < (int)items.size(); ++i)
    {
        if (items[(size_t)i].bounds.contains(e.getPosition()))
        {
            if (e.mods.isRightButtonDown())
            {
                items[(size_t)i].on = !items[(size_t)i].on;
                if (onToggleBypass) onToggleBypass(i, items[(size_t)i].on);
                repaint();
            }
            else
            {
                setSelected(i);
                if (onSelectionChange) onSelectionChange(i);
            }
            break;
        }
    }
}

void ModuleSelector::drawNodeIcon(juce::Graphics& g,
                                  juce::Rectangle<float> r,
                                  const juce::String& name,
                                  juce::Colour col)
{
    auto c = r.getCentre();
    float h = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
    juce::PathStrokeType stroke(1.6f,
                                juce::PathStrokeType::curved,
                                juce::PathStrokeType::rounded);
    g.setColour(col);

    if (name == "Input Gain")
    {
        juce::Path p;
        p.addRoundedRectangle(c.x - h * 0.22f, c.y - h * 0.85f,
                              h * 0.44f, h * 0.95f, h * 0.22f);
        p.startNewSubPath(c.x - h * 0.55f, c.y - h * 0.05f);
        p.quadraticTo(c.x - h * 0.55f, c.y + h * 0.55f, c.x, c.y + h * 0.55f);
        p.quadraticTo(c.x + h * 0.55f, c.y + h * 0.55f, c.x + h * 0.55f, c.y - h * 0.05f);
        p.startNewSubPath(c.x, c.y + h * 0.55f);
        p.lineTo(c.x, c.y + h * 0.85f);
        g.strokePath(p, stroke);
    }
    else if (name == "Output Gain")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.85f, c.y - h * 0.20f);
        p.lineTo(c.x - h * 0.45f, c.y - h * 0.20f);
        p.lineTo(c.x - h * 0.05f, c.y - h * 0.60f);
        p.lineTo(c.x - h * 0.05f, c.y + h * 0.60f);
        p.lineTo(c.x - h * 0.45f, c.y + h * 0.20f);
        p.lineTo(c.x - h * 0.85f, c.y + h * 0.20f);
        p.closeSubPath();
        p.startNewSubPath(c.x + h * 0.10f, c.y - h * 0.35f);
        p.quadraticTo(c.x + h * 0.55f, c.y, c.x + h * 0.10f, c.y + h * 0.35f);
        g.strokePath(p, stroke);
    }
    else if (name == "Spectral NR")
    {
        // Spectral bars shrinking to the right: noise reduced per band.
        juce::Path p;
        constexpr int bars = 7;
        for (int i = 0; i < bars; ++i)
        {
            const float t = (float) i / (float) (bars - 1);
            const float x = c.x - h * 0.8f + t * h * 1.6f;
            const float amp = x < c.x ? h * (i % 2 == 0 ? 0.62f : 0.34f) : h * 0.14f;
            p.startNewSubPath(x, c.y - amp);
            p.lineTo(x, c.y + amp);
        }
        g.strokePath(p, stroke);
    }
    else if (name == "High-Pass")
    {
        juce::Path p;
        for (int i = 0; i <= 24; ++i)
        {
            float t = (float) i / 24.0f;
            float x = r.getX() + t * r.getWidth();
            float y = r.getBottom() - (r.getHeight() * 0.75f) *
                      (1.0f - std::exp(-t * 4.5f));
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        g.strokePath(p, stroke);
    }
    else if (name == "Noise Gate")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.85f, c.y + h * 0.40f);
        p.lineTo(c.x - h * 0.45f, c.y + h * 0.40f);
        p.lineTo(c.x - h * 0.45f, c.y - h * 0.55f);
        p.lineTo(c.x + h * 0.25f, c.y - h * 0.55f);
        p.lineTo(c.x + h * 0.25f, c.y + h * 0.40f);
        p.lineTo(c.x + h * 0.85f, c.y + h * 0.40f);
        g.strokePath(p, stroke);
    }
    else if (name == "EQ")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.95f, c.y + h * 0.30f);
        p.quadraticTo(c.x - h * 0.55f, c.y - h * 0.30f,
                      c.x - h * 0.15f, c.y - h * 0.15f);
        p.quadraticTo(c.x + h * 0.10f, c.y - h * 0.65f,
                      c.x + h * 0.35f, c.y - h * 0.05f);
        p.quadraticTo(c.x + h * 0.60f, c.y + h * 0.45f,
                      c.x + h * 0.95f, c.y + h * 0.20f);
        g.strokePath(p, stroke);
    }
    else if (name == "Multi-Band")
    {
        for (int i = 0; i < 3; ++i)
        {
            float bx = c.x - h * 0.85f + i * h * 0.62f;
            float bh = h * (0.5f + i * 0.28f);
            juce::Rectangle<float> bar(bx, c.y + h * 0.75f - bh, h * 0.42f, bh);
            g.drawRoundedRectangle(bar, 2.0f, 1.2f);
        }
    }
    else if (name == "Compressor")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.90f, c.y + h * 0.85f);
        p.lineTo(c.x - h * 0.25f, c.y - h * 0.20f);
        p.lineTo(c.x + h * 0.90f, c.y + h * 0.20f);
        g.strokePath(p, stroke);
    }
    else if (name == "Saturation")
    {
        juce::Path p;
        for (int i = 0; i <= 32; ++i)
        {
            float t = (float) i / 32.0f;
            float x = c.x - h * 0.90f + t * h * 1.80f;
            float phase = (t - 0.5f) * juce::MathConstants<float>::twoPi * 1.5f;
            float y = c.y - std::tanh(phase * 1.6f) * h * 0.65f;
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        g.strokePath(p, stroke);
    }
    else if (name == "De-Esser")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.85f, c.y + h * 0.30f);
        p.quadraticTo(c.x - h * 0.25f, c.y + h * 0.30f,
                      c.x - h * 0.10f, c.y);
        p.quadraticTo(c.x + h * 0.05f, c.y - h * 0.30f,
                      c.x + h * 0.60f, c.y - h * 0.30f);
        g.strokePath(p, stroke);

        juce::Path ar;
        ar.startNewSubPath(c.x + h * 0.55f, c.y + h * 0.10f);
        ar.lineTo(c.x + h * 0.55f, c.y + h * 0.70f);
        ar.startNewSubPath(c.x + h * 0.30f, c.y + h * 0.45f);
        ar.lineTo(c.x + h * 0.55f, c.y + h * 0.70f);
        ar.lineTo(c.x + h * 0.80f, c.y + h * 0.45f);
        g.strokePath(ar, stroke);
    }
    else if (name == "Limiter")
    {
        juce::Path p;
        for (int i = 0; i <= 32; ++i)
        {
            float t = (float) i / 32.0f;
            float x = c.x - h * 0.90f + t * h * 1.80f;
            float phase = (t - 0.5f) * juce::MathConstants<float>::twoPi * 1.4f;
            float raw = std::sin(phase) * 1.4f;
            float clipped = juce::jlimit(-1.0f, 1.0f, raw);
            float y = c.y - clipped * h * 0.75f;
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        g.strokePath(p, stroke);
    }
    else if (name == "Band-Limit")
    {
        juce::Path p;
        p.startNewSubPath(c.x - h * 0.70f, c.y + h * 0.70f);
        p.lineTo(c.x - h * 0.70f, c.y - h * 0.40f);
        p.quadraticTo(c.x, c.y - h * 0.95f,
                      c.x + h * 0.70f, c.y - h * 0.40f);
        p.lineTo(c.x + h * 0.70f, c.y + h * 0.70f);
        g.strokePath(p, stroke);
    }
    else
    {
        juce::Path p;
        p.addEllipse(c.x - h * 0.6f, c.y - h * 0.6f, h * 1.2f, h * 1.2f);
        g.strokePath(p, stroke);
    }
}
