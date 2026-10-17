#include "CalibrationOverlay.h"
#include "UiStyle.h"
#include <cmath>

CalibrationOverlay::CalibrationOverlay()
{
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    cancelButton.setButtonText(juce::String::fromUTF8(u8"取消 / Cancel (Esc)"));
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(cancelButton);
    startTimerHz(60);

    auroras = {
        { -0.60f, 0.16f, 1.8f, 0.0f, 0.55f, 0.45f },
        { -0.38f, 0.20f, 2.3f, 1.1f, 0.70f, 0.55f },
        { -0.15f, 0.22f, 1.6f, 2.2f, 0.45f, 0.65f },
        {  0.10f, 0.24f, 2.7f, 3.3f, 0.80f, 0.60f },
        {  0.32f, 0.20f, 2.0f, 4.4f, 0.60f, 0.50f },
        {  0.55f, 0.14f, 2.5f, 5.5f, 0.75f, 0.40f },
        { -0.50f, 0.26f, 1.4f, 0.7f, 0.40f, 0.35f },
        {  0.20f, 0.28f, 1.2f, 2.9f, 0.50f, 0.42f },
        { -0.28f, 0.24f, 3.0f, 4.0f, 0.85f, 0.38f },
        {  0.00f, 0.30f, 1.7f, 1.6f, 0.35f, 0.48f },
    };
}

void CalibrationOverlay::resized()
{
    cancelButton.setBounds(getWidth() / 2 - 100, getHeight() - 62, 200, 36);
}

bool CalibrationOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && cancelButton.isEnabled())
    {
        if (onCancel) onCancel();
        return true;
    }
    return false;
}

void CalibrationOverlay::timerCallback()
{
    t1 += 0.06f;
    t2 += 0.03f;
    if (t1 > juce::MathConstants<float>::twoPi * 4.0f) t1 -= juce::MathConstants<float>::twoPi * 4.0f;
    if (t2 > juce::MathConstants<float>::twoPi) t2 -= juce::MathConstants<float>::twoPi;

    // 平滑音频电平（快升慢降）
    if (levelGetter)
    {
        float target = juce::jlimit(0.0f, 1.0f, levelGetter());
        if (target > smoothLevel)
            smoothLevel = smoothLevel * 0.35f + target * 0.65f;
        else
            smoothLevel = smoothLevel * 0.94f + target * 0.06f;
    }

    repaint();
}

void CalibrationOverlay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // 背景
    juce::ColourGradient bg(juce::Colour(0xff1d2942),
                            r.getCentreX(), r.getY(),
                            juce::Colour(0xff131c30),
                            r.getCentreX(), r.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRect(r);

    // 球体中心 / 半径
    const auto content = r.withTrimmedBottom(100.0f);
    auto centre = juce::Point<float>(content.getCentreX(), content.getCentreY() - 30.0f);
    float sphereR = juce::jmin(content.getWidth(), content.getHeight()) * 0.17f;
    if (sphereR < 100.0f) sphereR = 100.0f;

    // 音频驱动的发光强度
    float lvl = smoothLevel;
    float pulse = 1.0f + lvl * 0.35f;

    // 外围光晕（随音频脉动）
    {
        for (int i = 0; i < 7; ++i)
        {
            float rr = sphereR * (1.05f + i * 0.20f) * pulse;
            float a = 0.075f - i * 0.009f;
            if (a <= 0.0f) break;
            a *= (0.7f + lvl * 0.8f);
            juce::ColourGradient halo(
                juce::Colour(0xff7ea8d8).withAlpha(a),
                centre.x, centre.y,
                juce::Colours::transparentBlack,
                centre.x + rr, centre.y, true);
            g.setGradientFill(halo);
            g.fillEllipse(centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f);
        }
    }

    // 球体本体
    {
        juce::ColourGradient grad(
            juce::Colour(0xff243350),
            centre.x, centre.y - sphereR * 0.3f,
            juce::Colour(0xff0a1220),
            centre.x, centre.y + sphereR * 1.05f, true);
        g.setGradientFill(grad);
        g.fillEllipse(centre.x - sphereR, centre.y - sphereR,
                      sphereR * 2.0f, sphereR * 2.0f);
    }

    // 内部极光带
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addEllipse(centre.x - sphereR, centre.y - sphereR,
                        sphereR * 2.0f, sphereR * 2.0f);
        g.reduceClipRegion(clip);

        // 每层四条，模拟柔和光晕
        struct Layer { float w; float a; };
        static const Layer layers[] = {
            { 9.0f, 0.035f },
            { 5.0f, 0.090f },
            { 2.5f, 0.22f },
            { 1.0f, 0.55f },
        };

        for (auto& a : auroras)
        {
            // 每帧重新计算带路径
            juce::Path band;
            const int N = 72;
            for (int i = 0; i <= N; ++i)
            {
                float t = (float)i / (float)N;
                float nx = (t - 0.5f) * 2.0f;
                float circleHalf = std::sqrt(juce::jmax(0.0f, 1.0f - nx * nx));

                // 音频加大振幅 + 相位流动明显
                float ampMod = a.amp * (1.0f + lvl * 1.2f);
                float phaseShift = a.phase + t1 * a.speed;
                float wave = std::sin(nx * a.freq * juce::MathConstants<float>::pi
                                       + phaseShift) * ampMod;
                float ny = (a.baseY + wave) * circleHalf;

                float x = centre.x + nx * sphereR;
                float y = centre.y + ny * sphereR;
                if (i == 0) band.startNewSubPath(x, y);
                else        band.lineTo(x, y);
            }

            // 音频驱动的亮度
            float alphaMod = a.baseAlpha * (0.65f + lvl * 0.9f);

            // 多层叠加，外层大宽度低透明度 → 柔和
            for (auto& L : layers)
            {
                g.setColour(juce::Colour(0xffc8e0ff).withAlpha(alphaMod * L.a));
                g.strokePath(band, juce::PathStrokeType(
                    L.w, juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            }

            // 最亮核心用更白
            g.setColour(juce::Colour(0xfff0f6ff).withAlpha(alphaMod * 0.8f));
            g.strokePath(band, juce::PathStrokeType(
                0.5f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // 中心柔亮
        juce::ColourGradient inner(
            juce::Colour(0xffb8d4f5).withAlpha(0.18f + lvl * 0.15f),
            centre.x, centre.y,
            juce::Colours::transparentWhite,
            centre.x + sphereR * 0.55f, centre.y, true);
        g.setGradientFill(inner);
        g.fillEllipse(centre.x - sphereR, centre.y - sphereR,
                      sphereR * 2.0f, sphereR * 2.0f);
    }

    // 边缘柔和亮环
    {
        juce::ColourGradient rim(
            juce::Colours::transparentBlack,
            centre.x, centre.y,
            juce::Colour(0xffa8c8f0).withAlpha(0.22f + lvl * 0.20f),
            centre.x + sphereR, centre.y, true);
        g.setGradientFill(rim);
        g.drawEllipse(centre.x - sphereR, centre.y - sphereR,
                      sphereR * 2.0f, sphereR * 2.0f, 1.2f);
    }

    // 标题
    {
        auto titleArea = juce::Rectangle<float>(r.getX(), 40.0f, r.getWidth(), 50.0f);
        g.setColour(juce::Colour(0xffe8eef8));
        g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::plain)));
        g.drawText("AUTO CALIBRATING", titleArea.toNearestInt(),
                   juce::Justification::centred, false);
    }

    // 步骤文字
    float textTop = juce::jmin(centre.y + sphereR + 50.0f, r.getBottom() - 270.0f);
    {
        auto stageArea = juce::Rectangle<float>(
            r.getX(), textTop, r.getWidth(), 32.0f);
        g.setColour(juce::Colour(0xffd0dcf0));
        g.setFont(juce::Font(juce::FontOptions(17.0f)));
        juce::String txt = stageText.isNotEmpty() ? stageText : juce::String("...");
        g.drawText(txt, stageArea.toNearestInt(),
                   juce::Justification::centred, false);
    }

    // 进度条
    {
        float barW = juce::jmin(620.0f, r.getWidth() * 0.62f);
        float barH = 6.0f;
        float bx = r.getCentreX() - barW * 0.5f;
        float by = textTop + 52.0f;

        juce::Rectangle<float> bg2(bx, by, barW, barH);
        g.setColour(juce::Colour(0xff283248));
        g.fillRoundedRectangle(bg2, barH * 0.5f);

        if (progress > 0.005f)
        {
            auto fill = bg2.withWidth(barW * progress);
            juce::ColourGradient grad(
                juce::Colour(0xff90b8e8), bx, by,
                juce::Colour(0xffd8e8ff), bx + barW, by, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fill, barH * 0.5f);

            float xEnd = bx + barW * progress;
            juce::ColourGradient dot(
                juce::Colour(0xffe0ecff),
                xEnd - 4.0f, by + barH * 0.5f,
                juce::Colours::transparentWhite,
                xEnd + 6.0f, by + barH * 0.5f, true);
            g.setGradientFill(dot);
            g.fillEllipse(xEnd - 6.0f, by - 2.0f, 12.0f, 12.0f);
        }
    }

    // 百分比
    {
        auto pctArea = juce::Rectangle<float>(
            r.getX(), textTop + 82.0f, r.getWidth(), 60.0f);
        g.setColour(juce::Colour(0xffe8eef8));
        g.setFont(juce::Font(juce::FontOptions(34.0f, juce::Font::plain)));
        juce::String pct = juce::String((int)std::round(progress * 100.0f)) + "%";
        g.drawText(pct, pctArea.toNearestInt(),
                   juce::Justification::centred, false);
    }

    // 底部提示
    {
        auto tipArea = juce::Rectangle<float>(
            r.getX(), r.getBottom() - 100.0f, r.getWidth(), 30.0f);
        g.setColour(juce::Colour(0xff8896ac));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(juce::String::fromUTF8(u8"先保持安静，再按提示正常说话；可随时取消"),
                   tipArea.toNearestInt(),
                   juce::Justification::centred, false);
    }
}
