#include "LevelMeter.h"
#include "UiStyle.h"

LevelMeter::LevelMeter(const juce::String& label,
                       std::function<float()> levelGetter,
                       bool vertical)
    : labelText(label), getter(std::move(levelGetter)), vertical(vertical)
{
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void LevelMeter::timerCallback()
{
    float target = getter ? getter() : 0.0f;
    float cur = smoothValue;
    if (target > cur) smoothValue = cur * 0.35f + target * 0.65f;
    else              smoothValue = cur * 0.82f + target * 0.18f;
    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    juce::Rectangle<float> labelArea, barArea, scaleArea;
    if (vertical)
    {
        labelArea = r.removeFromTop(18.0f);
        scaleArea = r.removeFromRight(18.0f);
        barArea = r;
    }
    else
    {
        labelArea = r.removeFromLeft(36.0f);
        barArea = r;
    }

    // 标签
    g.setColour(UiStyle::textFaint);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(labelText, labelArea.toNearestInt(),
               juce::Justification::centred, false);

    // 背景
    g.setColour(UiStyle::meterBg);
    g.fillRoundedRectangle(barArea, 4.0f);

    auto inner = barArea.reduced(2.0f);

    float db = juce::Decibels::gainToDecibels(smoothValue, -60.0f);
    float v = juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f));

    if (vertical)
    {
        float fullH = inner.getHeight();
        float bottom = inner.getBottom();

        // 底纹：三条区域刻度线
        g.setColour(UiStyle::cardBorder.withAlpha(0.35f));
        float greenTopY = bottom - fullH * 0.70f;
        float yellowTopY = bottom - fullH * 0.90f;
        g.drawLine(barArea.getX() + 2.0f, greenTopY, barArea.getX() + 5.0f, greenTopY, 0.8f);
        g.drawLine(barArea.getX() + 2.0f, yellowTopY, barArea.getX() + 5.0f, yellowTopY, 0.8f);

        // 绿段 0-70%
        if (v > 0.005f)
        {
            float gH = fullH * juce::jmin(v, 0.70f);
            if (gH > 0.3f)
            {
                g.setColour(juce::Colour(0xff3ddc84));
                g.fillRect(inner.getX(), bottom - gH, inner.getWidth(), gH);
            }
        }
        // 黄段 70-90%
        if (v > 0.70f)
        {
            float yH = fullH * (juce::jmin(v, 0.90f) - 0.70f);
            if (yH > 0.3f)
            {
                g.setColour(juce::Colour(0xfff4c542));
                g.fillRect(inner.getX(), bottom - fullH * 0.70f - yH,
                           inner.getWidth(), yH);
            }
        }
        // 红段 90-100%（必须贴着当前电平，不能从顶端往下画）
        if (v > 0.90f)
        {
            float rH = fullH * (v - 0.90f);
            if (rH > 0.3f)
            {
                g.setColour(juce::Colour(0xffef4444));
                g.fillRect(inner.getX(), bottom - fullH * v, inner.getWidth(), rH);
            }
        }
    }
    else
    {
        float fullW = inner.getWidth();
        float left = inner.getX();

        // 底纹刻度
        g.setColour(UiStyle::cardBorder.withAlpha(0.35f));
        float greenRightX = left + fullW * 0.70f;
        float yellowRightX = left + fullW * 0.90f;
        g.drawLine(greenRightX, barArea.getY() + 2.0f, greenRightX, barArea.getY() + 5.0f, 0.8f);
        g.drawLine(yellowRightX, barArea.getY() + 2.0f, yellowRightX, barArea.getY() + 5.0f, 0.8f);

        if (v > 0.005f)
        {
            float gW = fullW * juce::jmin(v, 0.70f);
            if (gW > 0.3f)
            {
                g.setColour(juce::Colour(0xff3ddc84));
                g.fillRect(left, inner.getY(), gW, inner.getHeight());
            }
            if (v > 0.70f)
            {
                float yW = fullW * (juce::jmin(v, 0.90f) - 0.70f);
                if (yW > 0.3f)
                {
                    g.setColour(juce::Colour(0xfff4c542));
                    g.fillRect(left + fullW * 0.70f, inner.getY(),
                               yW, inner.getHeight());
                }
            }
            if (v > 0.90f)
            {
                float rW = fullW * (v - 0.90f);
                if (rW > 0.3f)
                {
                    g.setColour(juce::Colour(0xffef4444));
                    g.fillRect(left + fullW * 0.90f, inner.getY(),
                               rW, inner.getHeight());
                }
            }
        }
    }

    // 边框
    g.setColour(UiStyle::cardBorder.withAlpha(0.8f));
    g.drawRoundedRectangle(barArea, 4.0f, 1.0f);

    // 右侧刻度（只竖版）
    if (vertical && scaleArea.getWidth() > 4.0f)
    {
        const float dbs[] = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
        auto scaleRect = scaleArea.reduced(1.0f, 2.0f);

        for (float d : dbs)
        {
            float t = juce::jlimit(0.0f, 1.0f, juce::jmap(d, -60.0f, 0.0f, 0.0f, 1.0f));
            float y = scaleRect.getBottom() - scaleRect.getHeight() * t;

            g.setColour(UiStyle::textFaint.withAlpha(0.7f));
            g.drawLine(scaleRect.getX(), y,
                       scaleRect.getX() + 3.0f, y, 0.8f);

            if (d == 0.0f || d == -12.0f || d == -36.0f || d == -60.0f)
            {
                g.setColour(UiStyle::textFaint.withAlpha(0.9f));
                g.setFont(juce::Font(juce::FontOptions(8.0f)));
                juce::String label = (d == 0.0f) ? "0" : juce::String((int) d);
                g.drawText(label,
                           juce::Rectangle<int>((int) scaleRect.getX() + 4,
                                                (int) y - 5, 13, 10),
                           juce::Justification::centredLeft, false);
            }
        }
    }
}
