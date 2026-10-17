#include "ChainCanvas.h"
#include "UiStyle.h"

void ChainCanvas::paint(juce::Graphics& g)
{
    int n = getNumChildComponents();
    if (n < 2) return;

    for (int i = 0; i < n - 1; ++i)
    {
        auto* a = getChildComponent(i);
        auto* b = getChildComponent(i + 1);
        if (a == nullptr || b == nullptr) continue;

        auto ra = a->getBounds().toFloat();
        auto rb = b->getBounds().toFloat();

        // 同一行才画
        bool sameRow = (ra.getY() < rb.getBottom()) && (rb.getY() < ra.getBottom());
        if (!sameRow) continue;

        float x1 = ra.getRight() + 1.0f;
        float x2 = rb.getX() - 1.0f;
        if (x2 <= x1) continue;

        float y = (ra.getCentreY() + rb.getCentreY()) * 0.5f;

        // 底部有阴影的线
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawLine(x1, y + 1.0f, x2, y + 1.0f, 1.5f);

        // 主线
        g.setColour(UiStyle::cardBorder.withAlpha(0.75f));
        g.drawLine(x1, y, x2, y, 1.5f);

        // 箭头
        float ax = x2 - 1.0f;
        juce::Path arrow;
        arrow.startNewSubPath(ax, y);
        arrow.lineTo(ax - 5.0f, y - 3.0f);
        arrow.lineTo(ax - 5.0f, y + 3.0f);
        arrow.closeSubPath();
        g.setColour(UiStyle::textDim);
        g.fillPath(arrow);
    }
}
