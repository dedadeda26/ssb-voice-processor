#include "SplashComponent.h"
#include "icon_data.h"
#include <cmath>

namespace {

    inline float easeOutCubic(float t)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return 1.0f - std::pow(1.0f - t, 3.0f);
    }

    inline float progress(double elapsed, double start, double dur)
    {
        if (dur <= 0.0) return 1.0f;
        return juce::jlimit(0.0f, 1.0f, (float)((elapsed - start) / dur));
    }

} // namespace

SplashComponent::SplashComponent()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);

    iconImage = juce::ImageFileFormat::loadFrom(
        IconData::icon_png, (size_t) IconData::icon_png_size);

    // 组件尺寸 = 卡片尺寸 + 四周外发光预留
    const int W = kCardW + kGlowPad * 2;
    const int H = kCardH + kGlowPad * 2;
    setSize(W, H);

    // No OS drop shadow: the splash already draws its own glow, and the peer's
    // shadow showed up as a translucent block around the card.
    addToDesktop(juce::ComponentPeer::windowIsTemporary
               | juce::ComponentPeer::windowIgnoresKeyPresses);

    setAlwaysOnTop(true);
    centreWithSize(W, H);
    setVisible(true);
    toFront(true);

    startTimeMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

SplashComponent::~SplashComponent()
{
    stopTimer();
}

void SplashComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    elapsedSec = (now - startTimeMs) / 1000.0;

    ringRotation += 0.012f;
    if (ringRotation > juce::MathConstants<float>::twoPi * 4.0f)
        ringRotation -= juce::MathConstants<float>::twoPi * 4.0f;

    pulsePhase += 0.035f;
    if (pulsePhase > juce::MathConstants<float>::twoPi)
        pulsePhase -= juce::MathConstants<float>::twoPi;

    if (elapsedSec >= kTotalDuration)
    {
        stopTimer();

        juce::Component::SafePointer<SplashComponent> safe(this);
        juce::MessageManager::callAsync([safe]() {
            if (safe == nullptr) return;
            if (safe->onFinished) safe->onFinished();
        });
        return;
    }

    repaint();
}

void SplashComponent::paint(juce::Graphics& g)
{
    // 卡片区域（去掉四周 kGlowPad 的外发光预留）
    auto cardBounds = getLocalBounds().toFloat()
                        .reduced((float) kGlowPad);

    // ---------- 全局淡入 / 淡出 ----------
    float masterAlpha = 1.0f;
    if (elapsedSec < 0.4)
        masterAlpha = (float)(elapsedSec / 0.4);
    else if (elapsedSec > kFadeOutStart)
        masterAlpha = 1.0f - (float)((elapsedSec - kFadeOutStart) /
                                     (kTotalDuration - kFadeOutStart));
    masterAlpha = juce::jlimit(0.0f, 1.0f, masterAlpha);

    // ============ 外边缘发光（在卡片外围绘制） ============
    // 多层圆角矩形描边，逐层放大、透明度递减，形成柔和的外发光
    if (masterAlpha > 0.01f)
    {
        const int   glowLayers = 14;
        const float cornerR    = 16.0f;
        const juce::Colour glowCol(0xff5eb0ff);

        for (int i = 1; i <= glowLayers; ++i)
        {
            const float expand = (float) i * 2.0f;   // 每层向外扩 2px
            // 透明度：内层强、外层弱，二次衰减
            const float t     = (float) i / (float) glowLayers;
            const float alpha = 0.30f * std::pow(1.0f - t, 1.8f);

            if (alpha < 0.003f) continue;

            auto r = cardBounds.expanded(expand);
            g.setColour(glowCol.withAlpha(alpha * masterAlpha));
            g.drawRoundedRectangle(r, cornerR + expand * 0.5f, 2.0f);
        }
    }

    // ============ 背景（平整柔和的深蓝渐变） ============
    {
        juce::ColourGradient bg(
            juce::Colour(0xff1a2130), cardBounds.getCentreX(), cardBounds.getY(),
            juce::Colour(0xff0c1119), cardBounds.getCentreX(), cardBounds.getBottom(),
            false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(cardBounds, 16.0f);
    }

    // ============ 底部边缘发光（在卡片内部） ============
    if (masterAlpha > 0.01f)
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(cardBounds, 16.0f);
        g.reduceClipRegion(clip);

        const float glowH = 70.0f;
        juce::ColourGradient botGlow(
            juce::Colour(0xff3a7fd0).withAlpha(0.0f),
            cardBounds.getCentreX(), cardBounds.getBottom() - glowH,
            juce::Colour(0xff4a90e8).withAlpha(0.20f * masterAlpha),
            cardBounds.getCentreX(), cardBounds.getBottom(),
            false);
        g.setGradientFill(botGlow);
        g.fillRect(juce::Rectangle<float>(cardBounds.getX(),
                                          cardBounds.getBottom() - glowH,
                                          cardBounds.getWidth(),
                                          glowH));

        const float lineH  = 1.5f;
        const float lineY  = cardBounds.getBottom() - lineH - 1.0f;
        const float halfW  = 200.0f;
        const juce::Colour coreCol = juce::Colour(0xffa8ddff);

        {
            juce::Rectangle<float> rL(
                cardBounds.getCentreX() - halfW, lineY, halfW, lineH);
            juce::ColourGradient lineL(
                juce::Colour(0xff7eccff).withAlpha(0.0f),
                rL.getX(), rL.getCentreY(),
                coreCol.withAlpha(0.65f * masterAlpha),
                rL.getRight(), rL.getCentreY(),
                false);
            g.setGradientFill(lineL);
            g.fillRect(rL);
        }
        {
            juce::Rectangle<float> rR(
                cardBounds.getCentreX(), lineY, halfW, lineH);
            juce::ColourGradient lineR(
                coreCol.withAlpha(0.65f * masterAlpha),
                rR.getX(), rR.getCentreY(),
                juce::Colour(0xff7eccff).withAlpha(0.0f),
                rR.getRight(), rR.getCentreY(),
                false);
            g.setGradientFill(lineR);
            g.fillRect(rR);
        }
    }

    // ============ 卡片边框（更柔和的细描边） ============
    {
        g.setColour(juce::Colour(0xff7eccff).withAlpha(0.08f * masterAlpha));
        g.drawRoundedRectangle(cardBounds.reduced(0.5f), 16.0f, 0.8f);
    }

    // ============ 各元素入场进度 ============
    const float tIconIn   = easeOutCubic(progress(elapsedSec, 0.15, 0.65));
    const float tTitleIn  = easeOutCubic(progress(elapsedSec, 0.55, 0.55));
    const float tSubIn    = easeOutCubic(progress(elapsedSec, 0.75, 0.55));
    const float tAuthorIn = easeOutCubic(progress(elapsedSec, 1.00, 0.55));

    // ---------- 几何参数 ----------
    const float cx      = cardBounds.getCentreX();
    const float iconCy  = cardBounds.getY() + 122.0f;
    const float baseIconSize = 106.0f;

    const float iconInScale = 0.65f + 0.35f * tIconIn;
    const float pulse       = 1.0f + 0.008f * std::sin(pulsePhase);
    const float iconSize    = baseIconSize * iconInScale * pulse;

    // ============ 单层细极光弧 ============
    if (tIconIn > 0.05f)
    {
        const float r      = baseIconSize * 0.5f + 22.0f;
        const float rot    = ringRotation * 0.5f;
        const float arcLen = juce::MathConstants<float>::pi * 0.85f;

        juce::Path p;
        p.addCentredArc(cx, iconCy, r, r, 0.0f, rot, rot + arcLen, true);

        g.setColour(juce::Colour(0xff7eccff).withAlpha(0.32f * tIconIn * masterAlpha));
        g.strokePath(p, juce::PathStrokeType(
            1.8f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // ============ 图标本体 ============
    if (iconImage.isValid() && tIconIn > 0.01f)
    {
        const juce::Rectangle<float> iconRect(
            cx - iconSize * 0.5f, iconCy - iconSize * 0.5f,
            iconSize, iconSize);

        juce::Graphics::ScopedSaveState save(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.setOpacity(tIconIn * masterAlpha);
        g.drawImage(iconImage, iconRect,
                    juce::RectanglePlacement::stretchToFit, false);
    }

    // ============ 主标题（38pt） ============
    {
        const float offsetY = (1.0f - tTitleIn) * 10.0f;
        const juce::Rectangle<int> titleRect(
            0, (int)(iconCy + 78.0f + offsetY),
            getWidth(), 50);

        const float a = tTitleIn * masterAlpha;
        g.setColour(juce::Colour(0xffeef3fb).withAlpha(a));
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 38.0f, juce::Font::bold)));
        g.drawText("SSB Voice Processor", titleRect,
                   juce::Justification::centred, false);
    }

    // ============ 副标题（20pt） ============
    {
        const float offsetY = (1.0f - tSubIn) * 8.0f;
        const juce::Rectangle<int> subRect(
            0, (int)(iconCy + 140.0f + offsetY),
            getWidth(), 32);

        const float a = tSubIn * masterAlpha * 0.85f;
        g.setColour(juce::Colour(0xff95a4bc).withAlpha(a));
        g.setFont(juce::Font(juce::FontOptions("Microsoft YaHei", 20.0f,
                                               juce::Font::plain)));
        g.drawText(juce::String::fromUTF8(u8"SSB 语音处理器 · 正在启动"),
                   subRect, juce::Justification::centred, false);
    }

    // ============ 底部分隔线 + 署名（19pt） ============
    {
        const float lineY   = cardBounds.getBottom() - 56.0f;
        const float lineW   = 200.0f;
        const float lineX   = cardBounds.getCentreX() - lineW * 0.5f;
        const float aAuthor = tAuthorIn * masterAlpha;

        g.setColour(juce::Colour(0xff3c4459).withAlpha(0.55f * aAuthor));
        g.drawLine(lineX, lineY, lineX + lineW, lineY, 0.8f);

        const float offsetY = (1.0f - tAuthorIn) * 6.0f;
        const juce::Rectangle<int> authorRect(
            0, (int)(lineY + 12.0f + offsetY),
            getWidth(), 32);

        g.setColour(juce::Colour(0xff7eccff).withAlpha(aAuthor));
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 19.0f, juce::Font::bold)));
        g.drawText("Made by BI4AGE", authorRect,
                   juce::Justification::centred, false);
    }
}
