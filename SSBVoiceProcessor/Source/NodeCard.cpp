#include "NodeCard.h"
#include "SimpleMacro.h"
#include "Localization.h"
#include "AppSettings.h"
#include "UiStyle.h"
#include "FreqResponse.h"
#include <map>
#include <cmath>
#include <vector>
#include <atomic>

namespace {

// 全局瀑布图增益，输入/输出共享（非匿名，供 MainComponent 存取以便退出时落盘）
} // namespace

std::atomic<float> gWaterfallGain { 1.0f };
std::atomic<bool>  gWaterfallGainLoaded { false };

float getSharedWaterfallGain() { return gWaterfallGain.load(); }
void setSharedWaterfallGain(float value)
{
    gWaterfallGain.store(juce::jlimit(0.3f, 3.0f, value));
    gWaterfallGainLoaded.store(true); // do not let a later load overwrite it
}

namespace {

void ensureWaterfallGainLoaded()
{
    bool expected = false;
    if (!gWaterfallGainLoaded.compare_exchange_strong(expected, true))
        return;
    if (auto* s = AppSettings::get())
    {
        double v = s->getDoubleValue("waterfallGain", 1.0);
        gWaterfallGain.store((float) juce::jlimit(0.3, 3.0, v));
    }
}

juce::String getNodeDisplayKey(const juce::String& n)
{
    if (n == "Input Gain")  return "node.inputGain";
    if (n == "High-Pass")   return "node.hpf";
    if (n == "Noise Gate")  return "node.gate";
    if (n == "Spectral NR") return "node.spectralNr";
    if (n == "EQ")          return "node.eq";
    if (n == "Multi-Band")  return "node.mbComp";
    if (n == "Compressor")  return "node.comp";
    if (n == "Saturation")  return "node.sat";
    if (n == "De-Esser")    return "node.deEsser";
    if (n == "Limiter")     return "node.limiter";
    if (n == "Auto Level")  return "node.autoLevel";
    if (n == "Band-Limit")  return "node.bandLimit";
    if (n == "Output Gain") return "node.outputGain";
    return n;
}

int getMacroIdFor(const juce::String& nodeName, const juce::String& paramId)
{
    if (nodeName == "EQ")
        if (paramId == "lowGain" || paramId == "midGain" || paramId == "highGain") return 0;
    if (nodeName == "Multi-Band")
        if (paramId == "lowAmount" || paramId == "midAmount" || paramId == "highAmount") return 0;
    if (nodeName == "Compressor")
        if (paramId == "threshold" || paramId == "ratio" || paramId == "makeup") return 0;
    if (nodeName == "Saturation")
        if (paramId == "drive" || paramId == "mix") return 0;
    if (nodeName == "De-Esser")
        if (paramId == "deEssAmount" || paramId == "deEssThresh") return 0;
    if (nodeName == "Noise Gate")
        if (paramId == "gateAmount" || paramId == "gateThreshold") return 0;
    return -1;
}

juce::String getMacroNameKey(const juce::String& nodeName)
{
    if (nodeName == "EQ")          return "macro.tone";
    if (nodeName == "Multi-Band")  return "macro.force";
    if (nodeName == "Compressor")  return "macro.punch";
    if (nodeName == "Saturation")  return "macro.warmth";
    if (nodeName == "De-Esser")    return "macro.sibilance";
    if (nodeName == "Noise Gate")  return "macro.gate";
    return "macro.tone";
}

// 帮助 key
juce::String getHelpKey(const juce::String& n)
{
    if (n == "Input Gain")  return "help.inputGain";
    if (n == "High-Pass")   return "help.hpf";
    if (n == "Noise Gate")  return "help.gate";
    if (n == "EQ")          return "help.eq";
    if (n == "Multi-Band")  return "help.mbComp";
    if (n == "Compressor")  return "help.comp";
    if (n == "Saturation")  return "help.sat";
    if (n == "De-Esser")    return "help.deEsser";
    if (n == "Limiter")     return "help.limiter";
    if (n == "Band-Limit")  return "help.bandLimit";
    if (n == "Output Gain") return "help.outputGain";
    return {};
}

bool hasVisualization(const juce::String& nodeName)
{
    return nodeName == "Input Gain" ||
           nodeName == "Output Gain" ||
           nodeName == "High-Pass" ||
           nodeName == "Noise Gate" ||
           nodeName == "Spectral NR" ||
           nodeName == "EQ" ||
           nodeName == "Multi-Band" ||
           nodeName == "Compressor" ||
           nodeName == "Saturation" ||
           nodeName == "De-Esser" ||
           nodeName == "Limiter" ||
           nodeName == "Band-Limit";
}

inline float freqToX(float freq, juce::Rectangle<float> area)
{
    freq = juce::jlimit(40.0f, 20000.0f, freq);
    float t = std::log(freq / 40.0f) / std::log(20000.0f / 40.0f);
    return area.getX() + t * area.getWidth();
}

inline float dbToY(float db, juce::Rectangle<float> area, float dbRange)
{
    float t = juce::jlimit(-1.0f, 1.0f, db / dbRange);
    return area.getCentreY() - t * area.getHeight() * 0.45f;
}

// 瀑布配色（v 已经是映射好的最终值，0-1）
inline juce::Colour waterfallColour(float v)
{
    v = juce::jlimit(0.0f, 1.0f, v);

    struct Stop { float t; juce::Colour c; };
    static const Stop stops[] = {
        { 0.00f, juce::Colour(0xff000828) },
        { 0.12f, juce::Colour(0xff001a5a) },
        { 0.26f, juce::Colour(0xff0038b8) },
        { 0.40f, juce::Colour(0xff0060e0) },
        { 0.52f, juce::Colour(0xff40a8f0) },
        { 0.62f, juce::Colour(0xffc0e8ff) },
        { 0.70f, juce::Colour(0xfffff8d0) },
        { 0.78f, juce::Colour(0xffffe020) },
        { 0.87f, juce::Colour(0xffff8010) },
        { 0.95f, juce::Colour(0xffe01010) },
        { 1.00f, juce::Colour(0xff800808) },
    };
    const int N = sizeof(stops) / sizeof(stops[0]);
    for (int i = 0; i + 1 < N; ++i)
    {
        if (v <= stops[i+1].t)
        {
            float t = (v - stops[i].t) / (stops[i+1].t - stops[i].t);
            return stops[i].c.interpolatedWith(stops[i+1].c, t);
        }
    }
    return stops[N-1].c;
}

// 3D 瀑布
void drawWaterfall3D(juce::Graphics& g, AudioEngine& engine,
                     juce::Rectangle<float> area)
{
    const int bands = engine.getSpectrumBands();
    const int hist  = engine.getSpectrumHistorySize();
    if (bands < 2 || hist < 2) return;

    const int wp = engine.getSpectrumWritePos();

    const float totalY = area.getHeight() * 0.94f;
    const float minW = 0.38f;
    const float maxW = 1.00f;
    const float energyH = area.getHeight() * 0.24f;
    const float centerX = area.getCentreX();
    const float colourGain = juce::jlimit(0.3f, 3.0f, gWaterfallGain.load());

    // Two layers per history frame, drawn at the native 96 bands: that keeps the
    // smooth look the 3x-interpolated version had while reading 64 points/frame
    // directly instead of 286 bounds-checked lookups.
    constexpr int rowsPerFrame = 2;
    const int frames = (hist - 1) * rowsPerFrame + 1;
    const float framesF = (float) juce::jmax(1, frames - 1);

    for (int t = 0; t < frames; ++t)
    {
        const int t0 = t / rowsPerFrame;
        const int t1 = juce::jmin(hist - 1, t0 + 1);
        const float frac = (float) (t % rowsPerFrame) / (float) rowsPerFrame;
        const int row0 = (wp - hist + t0 + hist * 2) % hist;
        const int row1 = (wp - hist + t1 + hist * 2) % hist;
        const auto& a = engine.getSpectrumHistoryRow(row0);
        const auto& b = engine.getSpectrumHistoryRow(row1);

        const float tn = (float) t / framesF;
        const float baseY = area.getY() + (area.getHeight() - totalY) * 0.5f + tn * totalY;
        const float widthScale = minW + (maxW - minW) * tn;
        const float w = area.getWidth() * widthScale;
        const float left = centerX - w * 0.5f;
        const float dx = w / (float) (bands - 1);

        juce::Path p;
        p.startNewSubPath(left, baseY);
        float peakCol = 0.0f;
        for (int i = 0; i < bands; ++i)
        {
            const float va = a[(size_t) i];
            const float v = va + (b[(size_t) i] - va) * frac;
            p.lineTo(left + dx * (float) i, baseY - v * energyH);
            peakCol = juce::jmax(peakCol, v * colourGain);
        }
        p.lineTo(left + w, baseY);
        p.closeSubPath();

        juce::Colour base = waterfallColour(juce::jlimit(0.0f, 1.0f, peakCol));
        base = base.withMultipliedBrightness(0.32f + 0.68f * tn);
        g.setColour(base.withAlpha(0.46f));
        g.fillPath(p);
    }
}

void drawWaveformBarsSlow(juce::Graphics& g, AudioEngine& engine,
                          juce::Rectangle<float> area, juce::Colour colour)
{
    float midY = area.getCentreY();
    float amp = area.getHeight() * 0.5f - 2.0f;

    g.setColour(UiStyle::meterBg.withAlpha(0.85f));
    g.fillRoundedRectangle(area, 3.0f);

    g.setColour(UiStyle::cardBorder.withAlpha(0.18f));
    for (int i = 1; i < 4; ++i)
    {
        float y = area.getY() + (float)i / 4.0f * area.getHeight();
        g.drawLine(area.getX(), y, area.getRight(), y, 0.5f);
    }

    g.setColour(UiStyle::cardBorder.withAlpha(0.5f));
    g.drawLine(area.getX(), midY, area.getRight(), midY, 0.7f);

    const int windowSize = 96000;
    int wp = engine.getWaveWritePos();

    // Reused across repaints: this used to allocate 384 KB thirty times a second.
    static std::vector<float> raw;
    raw.resize((size_t) windowSize);
    float peak = 0.0f;
    for (int i = 0; i < windowSize; ++i)
    {
        int idx = (wp - windowSize + i + AudioEngine::kWaveSize * 4) % AudioEngine::kWaveSize;
        float v = engine.getSmoothedWaveSample(idx);
        raw[(size_t) i] = v;
        peak = juce::jmax(peak, std::abs(v));
    }

    float autoGain = 1.0f;
    if (peak > 0.02f) autoGain = juce::jmin(4.0f, 0.85f / peak);
    for (int i = 0; i < windowSize; ++i) raw[(size_t) i] *= autoGain;

    int numBlocks = juce::jlimit(400, 1400, (int)(area.getWidth() * 1.5f));
    int samplesPerBlock = juce::jmax(1, windowSize / numBlocks);
    numBlocks = (windowSize + samplesPerBlock - 1) / samplesPerBlock;
    float blockW = area.getWidth() / (float) numBlocks;

    g.setColour(colour);
    for (int b = 0; b < numBlocks; ++b)
    {
        int start = b * samplesPerBlock;
        int end = juce::jmin(windowSize, start + samplesPerBlock);
        float minV = 0.0f, maxV = 0.0f;
        for (int i = start; i < end; ++i)
        {
            float v = raw[(size_t) i];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        float y1 = midY - maxV * amp;
        float y2 = midY - minV * amp;
        float x = area.getX() + b * blockW;
        g.fillRect(x, y1, juce::jmax(0.6f, blockW), y2 - y1);
    }

    g.setColour(UiStyle::cardBorder.withAlpha(0.4f));
    g.drawRoundedRectangle(area, 3.0f, 0.7f);
}

void drawFreqGrid(juce::Graphics& g, juce::Rectangle<float> area, float dbRange)
{
    const float freqs[] = { 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f };
    for (float f : freqs)
    {
        float x = freqToX(f, area);
        g.setColour(UiStyle::cardBorder.withAlpha(0.22f));
        g.drawLine(x, area.getY(), x, area.getBottom(), 0.6f);
        g.setColour(UiStyle::textFaint.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(8.5f)));
        juce::String label = (f >= 1000.0f)
            ? juce::String((int)(f / 1000.0f)) + "k"
            : juce::String((int) f);
        g.drawText(label,
                   juce::Rectangle<int>((int) x - 16, (int) area.getBottom() - 11, 32, 10),
                   juce::Justification::centred, false);
    }

    const float dbs[] = { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f };
    for (float db : dbs)
    {
        float y = dbToY(db, area, dbRange);
        bool isZero = std::abs(db) < 0.1f;
        g.setColour(UiStyle::cardBorder.withAlpha(isZero ? 0.5f : 0.18f));
        g.drawLine(area.getX(), y, area.getRight(), y, isZero ? 0.9f : 0.5f);
        if (isZero || std::abs(db) > 11.0f)
        {
            g.setColour(UiStyle::textFaint.withAlpha(0.55f));
            g.setFont(juce::Font(juce::FontOptions(8.5f)));
            juce::String label = (db > 0 ? "+" : "") + juce::String((int) db);
            g.drawText(label,
                       juce::Rectangle<int>((int) area.getX() + 2,
                                            (int) y - 5, 22, 10),
                       juce::Justification::centredLeft, false);
        }
    }
}

void smoothDbArray(std::vector<float>& arr, int iterations)
{
    int n = (int)arr.size();
    if (n < 3) return;
    std::vector<float> tmp((size_t) n);
    for (int it = 0; it < iterations; ++it)
    {
        for (int i = 0; i < n; ++i)
        {
            int a = juce::jmax(0, i - 1);
            int b = i;
            int c = juce::jmin(n - 1, i + 1);
            tmp[(size_t)i] = (arr[(size_t)a] + 2.0f * arr[(size_t)b] + arr[(size_t)c]) * 0.25f;
        }
        arr.swap(tmp);
    }
}

void drawFreqResponse(juce::Graphics& g, juce::Rectangle<float> area,
                      std::function<float(float)> responseDbFn,
                      juce::Colour curveColour,
                      float dbRange = 24.0f)
{
    drawFreqGrid(g, area, dbRange);

    const int steps = 400;
    std::vector<float> db((size_t)(steps + 1));
    std::vector<float> xs((size_t)(steps + 1));
    std::vector<float> ys((size_t)(steps + 1));

    for (int i = 0; i <= steps; ++i)
    {
        float t = (float) i / (float) steps;
        float freq = 40.0f * std::pow(20000.0f / 40.0f, t);
        db[(size_t)i] = responseDbFn(freq);
        xs[(size_t)i] = area.getX() + t * area.getWidth();
    }
    smoothDbArray(db, 6);
    for (int i = 0; i <= steps; ++i)
        ys[(size_t)i] = dbToY(db[(size_t)i], area, dbRange);

    juce::Path curve;
    curve.startNewSubPath(xs[0], ys[0]);
    for (int i = 1; i <= steps; ++i)
        curve.lineTo(xs[(size_t)i], ys[(size_t)i]);

    {
        juce::Path fillPath = curve;
        fillPath.lineTo(area.getRight(), area.getBottom());
        fillPath.lineTo(area.getX(), area.getBottom());
        fillPath.closeSubPath();
        juce::ColourGradient grad(
            curveColour.withAlpha(0.25f),
            area.getCentreX(), area.getY(),
            curveColour.withAlpha(0.0f),
            area.getCentreX(), area.getBottom(),
            false);
        g.setGradientFill(grad);
        g.fillPath(fillPath);
    }

    g.setColour(curveColour.withAlpha(0.28f));
    g.strokePath(curve, juce::PathStrokeType(4.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(curveColour);
    g.strokePath(curve, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void drawDynamicEQ(juce::Graphics& g, AudioEngine& engine,
                   juce::Rectangle<float> area,
                   std::function<float(float)> responseDbFn,
                   juce::Colour curveColour,
                   const std::vector<juce::Point<float>>& handles,
                   float dbRange = 18.0f)
{
    drawFreqGrid(g, area, dbRange);

    int n = engine.getSpectrumBands();
    if (n >= 2)
    {
        juce::Path spec;
        spec.startNewSubPath(area.getX(), area.getBottom());
        for (int i = 0; i < n; ++i)
        {
            float t = (float)i / (float)(n - 1);
            float v = engine.getSpectrumBand(i);
            float x = area.getX() + t * area.getWidth();
            float y = area.getBottom() - v * area.getHeight() * 0.9f;
            spec.lineTo(x, y);
        }
        spec.lineTo(area.getRight(), area.getBottom());
        spec.closeSubPath();

        juce::ColourGradient grad(
            juce::Colour(0xff3080e0).withAlpha(0.45f),
            area.getCentreX(), area.getY(),
            juce::Colour(0xff0a0e18).withAlpha(0.10f),
            area.getCentreX(), area.getBottom(),
            false);
        grad.addColour(0.5, juce::Colour(0xff1a4a90).withAlpha(0.25f));
        g.setGradientFill(grad);
        g.fillPath(spec);

        juce::Path outline;
        bool first = true;
        for (int i = 0; i < n; ++i)
        {
            float t = (float)i / (float)(n - 1);
            float v = engine.getSpectrumBand(i);
            float x = area.getX() + t * area.getWidth();
            float y = area.getBottom() - v * area.getHeight() * 0.9f;
            if (first) { outline.startNewSubPath(x, y); first = false; }
            else       outline.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xffa0d8ff).withAlpha(0.7f));
        g.strokePath(outline, juce::PathStrokeType(1.3f));
    }

    const int steps = 400;
    std::vector<float> db((size_t)(steps + 1));
    std::vector<float> xs((size_t)(steps + 1));
    std::vector<float> ys((size_t)(steps + 1));

    for (int i = 0; i <= steps; ++i)
    {
        float t = (float)i / (float)steps;
        float freq = 40.0f * std::pow(20000.0f / 40.0f, t);
        db[(size_t)i] = responseDbFn(freq);
        xs[(size_t)i] = area.getX() + t * area.getWidth();
    }
    smoothDbArray(db, 8);
    for (int i = 0; i <= steps; ++i)
        ys[(size_t)i] = dbToY(db[(size_t)i], area, dbRange);

    juce::Path curve;
    curve.startNewSubPath(xs[0], ys[0]);
    for (int i = 1; i <= steps; ++i)
        curve.lineTo(xs[(size_t)i], ys[(size_t)i]);

    {
        juce::Path fillPath = curve;
        fillPath.lineTo(area.getRight(), area.getBottom());
        fillPath.lineTo(area.getX(), area.getBottom());
        fillPath.closeSubPath();
        juce::ColourGradient grad(
            curveColour.withAlpha(0.20f),
            area.getCentreX(), area.getY(),
            curveColour.withAlpha(0.0f),
            area.getCentreX(), area.getBottom(),
            false);
        g.setGradientFill(grad);
        g.fillPath(fillPath);
    }

    g.setColour(juce::Colours::black.withAlpha(0.88f));
    g.strokePath(curve, juce::PathStrokeType(5.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(curveColour.withAlpha(0.55f));
    g.strokePath(curve, juce::PathStrokeType(3.2f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(curveColour);
    g.strokePath(curve, juce::PathStrokeType(1.8f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    for (auto& hp : handles)
    {
        float x = area.getX() + hp.x * area.getWidth();
        float y = area.getY() + hp.y * area.getHeight();

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(x - 7.0f, y - 5.0f, 14.0f, 14.0f);

        g.setColour(curveColour);
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);

        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}

void drawVerticalMeterWithZones(juce::Graphics& g, juce::Rectangle<float> bar,
                                float value01, bool isGR)
{
    g.setColour(juce::Colour(0xff15181e));
    g.fillRoundedRectangle(bar, 2.0f);

    auto inner = bar.reduced(1.0f);

    if (isGR)
    {
        float v = juce::jlimit(0.0f, 1.0f, value01);
        if (v > 0.005f)
        {
            float h = inner.getHeight() * v;
            auto r = inner.withHeight(h);
            g.setColour(juce::Colour(0xffef4444));
            g.fillRect(r);
        }
        g.setColour(UiStyle::cardBorder.withAlpha(0.55f));
        g.drawRoundedRectangle(bar, 2.0f, 0.8f);
        return;
    }

    float v = juce::jlimit(0.0f, 1.0f, value01);
    float fullH = inner.getHeight();
    float bottom = inner.getBottom();
    float top = inner.getY();

    const float greenThreshold = 0.60f;
    const float yellowThreshold = 0.82f;

    float greenTopY = bottom - fullH * greenThreshold;
    float yellowTopY = bottom - fullH * yellowThreshold;

    g.setColour(UiStyle::cardBorder.withAlpha(0.45f));
    g.drawLine(bar.getX() + 1.0f, greenTopY, bar.getX() + 3.0f, greenTopY, 0.6f);
    g.drawLine(bar.getX() + 1.0f, yellowTopY, bar.getX() + 3.0f, yellowTopY, 0.6f);

    if (v > 0.005f)
    {
        float gH = fullH * juce::jmin(v, greenThreshold);
        if (gH > 0.3f)
        {
            g.setColour(juce::Colour(0xff3ddc84));
            g.fillRect(inner.getX(), bottom - gH, inner.getWidth(), gH);
        }
        if (v > greenThreshold)
        {
            float yH = fullH * (juce::jmin(v, yellowThreshold) - greenThreshold);
            if (yH > 0.3f)
            {
                g.setColour(juce::Colour(0xfff4c542));
                g.fillRect(inner.getX(), bottom - fullH * greenThreshold - yH,
                           inner.getWidth(), yH);
            }
        }
        if (v > yellowThreshold)
        {
            float rH = fullH * (v - yellowThreshold);
            if (rH > 0.3f)
            {
                g.setColour(juce::Colour(0xffef4444));
                g.fillRect(inner.getX(), top, inner.getWidth(), rH);
            }
        }
    }

    g.setColour(UiStyle::cardBorder.withAlpha(0.55f));
    g.drawRoundedRectangle(bar, 2.0f, 0.8f);
}

void drawHorizontalMeterWithZones(juce::Graphics& g, juce::Rectangle<float> bar,
                                  float value01)
{
    g.setColour(juce::Colour(0xff15181e));
    g.fillRoundedRectangle(bar, 3.0f);

    auto inner = bar.reduced(1.0f);
    float fullW = inner.getWidth();

    float greenRightX = inner.getX() + fullW * 0.70f;
    float yellowRightX = inner.getX() + fullW * 0.90f;

    g.setColour(UiStyle::cardBorder.withAlpha(0.45f));
    g.drawLine(greenRightX, bar.getY() + 2.0f, greenRightX, bar.getBottom() - 2.0f, 0.6f);
    g.drawLine(yellowRightX, bar.getY() + 2.0f, yellowRightX, bar.getBottom() - 2.0f, 0.6f);

    float v = juce::jlimit(0.0f, 1.0f, value01);
    if (v > 0.005f)
    {
        float gW = fullW * juce::jmin(v, 0.70f);
        if (gW > 0.3f)
        {
            g.setColour(juce::Colour(0xff3ddc84));
            g.fillRect(inner.getX(), inner.getY(), gW, inner.getHeight());
        }
        if (v > 0.70f)
        {
            float yW = fullW * (juce::jmin(v, 0.90f) - 0.70f);
            if (yW > 0.3f)
            {
                g.setColour(juce::Colour(0xfff4c542));
                g.fillRect(inner.getX() + fullW * 0.70f, inner.getY(),
                           yW, inner.getHeight());
            }
        }
        if (v > 0.90f)
        {
            float rW = fullW * (v - 0.90f);
            if (rW > 0.3f)
            {
                g.setColour(juce::Colour(0xffef4444));
                g.fillRect(inner.getX() + fullW * 0.90f, inner.getY(),
                           rW, inner.getHeight());
            }
        }
    }

    g.setColour(UiStyle::cardBorder.withAlpha(0.6f));
    g.drawRoundedRectangle(bar, 3.0f, 0.9f);
}

} // namespace

NodeCard::NodeCard(DspNode* n, AudioEngine& e, bool simpleMode)
    : node(n), engine(e), simple(simpleMode)
{
    configurationVersion = engine.getConfigurationVersion();
    nodeKey = getNodeDisplayKey(node->getName());
    displayTitle = simple ? Loc::t(nodeKey) : node->getName().toUpperCase();
    onState = !node->isBypassed();

    fullCardWaterfall = (node->getName() == "Input Gain" ||
                         node->getName() == "Output Gain");

    // 创建瀑布图增益滑块（仅 Input/Output Gain）
    if (fullCardWaterfall)
    {
        // 左侧标签
        waterfallGainLabel = std::make_unique<juce::Label>();
        waterfallGainLabel->setText(juce::String::fromUTF8(u8"光谱增益"),
                                    juce::dontSendNotification);
        waterfallGainLabel->setColour(juce::Label::textColourId,
                                      juce::Colour(0xffb8c4d8));
        waterfallGainLabel->setFont(juce::Font(juce::FontOptions(12.0f,
                                                                 juce::Font::bold)));
        waterfallGainLabel->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*waterfallGainLabel);

        // 滑块
        waterfallGainSlider = std::make_unique<juce::Slider>();
        waterfallGainSlider->setSliderStyle(juce::Slider::LinearHorizontal);
        // 右侧显示数值
        waterfallGainSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
        waterfallGainSlider->setRange(0.3, 3.0, 0.05);
        ensureWaterfallGainLoaded();
        waterfallGainSlider->setValue(gWaterfallGain.load(), juce::dontSendNotification);

        waterfallGainSlider->setColour(juce::Slider::backgroundColourId,
                                       juce::Colour(0x80000000));
        waterfallGainSlider->setColour(juce::Slider::trackColourId,
                                       juce::Colour(0xff7eccff));
        waterfallGainSlider->setColour(juce::Slider::thumbColourId,
                                       juce::Colours::white);
        waterfallGainSlider->setColour(juce::Slider::textBoxTextColourId,
                                       juce::Colour(0xffe0e8f8));
        waterfallGainSlider->setColour(juce::Slider::textBoxBackgroundColourId,
                                       juce::Colour(0x40000000));
        waterfallGainSlider->setColour(juce::Slider::textBoxOutlineColourId,
                                       juce::Colour(0x00000000));

        waterfallGainSlider->onValueChange = [this] {
            float v = (float)waterfallGainSlider->getValue();
            gWaterfallGain.store(v);
            if (auto* s = AppSettings::get())
            {
                s->setValue("waterfallGain", (double) v);
                s->save(); // force: saveIfNeeded could skip the write
            }
        };

        addAndMakeVisible(*waterfallGainSlider);
    }

    if (simple) buildSimpleMacros();
    else        buildControls();

    startTimerHz(30);
}

void NodeCard::timerCallback()
{
    engine.consumeVisualization();
    if (configurationVersion != engine.getConfigurationVersion())
    {
        configurationVersion = engine.getConfigurationVersion();
        if (simple) buildSimpleMacros(); else buildControls();
        resized();
    }
    if (helpHovered)
    {
        ++helpHoverFrames;
        if (helpHoverFrames > 30 && !showHelp)
        {
            showHelp = true;
            repaint();
        }
    }
    else
    {
        if (showHelp) { showHelp = false; repaint(); }
        helpHoverFrames = 0;
    }

    // 同步共享的瀑布增益
    if (waterfallGainSlider != nullptr)
    {
        float cur = gWaterfallGain.load();
        if (std::abs((float)waterfallGainSlider->getValue() - cur) > 0.001f)
            waterfallGainSlider->setValue(cur, juce::dontSendNotification);
    }

    // Only the graph needs 30 Hz updates; repainting the whole card also forced
    // every knob and label to re-render, which is what made switching to the 3D
    // page feel heavy.
    if (hasVisualization(node->getName()))
        repaint(visArea.isEmpty() ? getLocalBounds() : visArea);
}

void NodeCard::refreshFromParams()
{
    for (auto& refresh : refreshers) refresh();
}

void NodeCard::buildSimpleMacros()
{
    knobs.clear();
    macroKnobs.clear();
    refreshers.clear();

    auto params = node->getParameters();
    std::map<int, std::vector<DspNode::ParamDescriptor>> groups;
    std::vector<DspNode::ParamDescriptor> ungrouped;

    for (auto& p : params)
    {
        if (p.param == nullptr) continue;
        int mid = getMacroIdFor(node->getName(), p.id);
        if (mid >= 0) groups[mid].push_back(p);
        else if (p.showInSimple) ungrouped.push_back(p);
    }

    for (auto& kv : groups)
    {
        auto& grp = kv.second;
        if (grp.empty()) continue;
        auto* knob = new Knob();
        knob->setKnobName(Loc::t(getMacroNameKey(node->getName())));
        knob->setRange(0.0, 100.0, 0.01);
        knob->setNumDecimalPlacesToDisplay(0);
        knob->setTextValueSuffix(" %");
        knob->setDoubleClickReturnValue(true, 50.0);
        knob->setAccentColour(UiStyle::categoryColour(node->getName()));

        auto macro = std::make_shared<SimpleMacro>(*node);
        knob->setValue(macro->initialValue(), juce::dontSendNotification);
        knob->onValueChange = [this, knob, macro] { macro->apply((float)knob->getValue()); if (onParametersChanged) onParametersChanged(); };
        addAndMakeVisible(knob);
        macroKnobs.add(knob);
        refreshers.push_back([knob, macro] { knob->setValue(macro->initialValue(), juce::dontSendNotification); });
    }

    for (auto& p : ungrouped)
    {
        if (p.param == nullptr) continue;
        auto* knob = new Knob();
        knob->setKnobName(Loc::t(p.nameKey));
        knob->setRange(p.minValue, p.maxValue);
        knob->setNumDecimalPlacesToDisplay(p.numDecimals);
        knob->setTextValueSuffix(p.unit.isNotEmpty() ? " " + p.unit : juce::String());
        knob->setDoubleClickReturnValue(true, p.defaultValue);
        knob->setValue(p.param->get(), juce::dontSendNotification);
        knob->setAccentColour(UiStyle::categoryColour(node->getName()));

        Param* pPtr = p.param;
        knob->onValueChange = [this, knob, pPtr] { pPtr->set((float) knob->getValue()); if (onParametersChanged) onParametersChanged(); };
        addAndMakeVisible(knob);
        knobs.add(knob);
        refreshers.push_back([knob, pPtr] { knob->setValue(pPtr->get(), juce::dontSendNotification); });
    }
    numKnobs = knobs.size();
    rows = 1;
}

void NodeCard::buildControls()
{
    knobs.clear();
    macroKnobs.clear();
    refreshers.clear();
    auto params = node->getParameters();
    for (auto& p : params)
    {
        if (p.param == nullptr) continue;
        auto* knob = new Knob();
        knob->setKnobName(Loc::t(p.nameKey));
        knob->setRange(p.minValue, p.maxValue);
        knob->setNumDecimalPlacesToDisplay(p.numDecimals);
        knob->setTextValueSuffix(p.unit.isNotEmpty() ? " " + p.unit : juce::String());
        knob->setDoubleClickReturnValue(true, p.defaultValue);
        knob->setValue(p.param->get(), juce::dontSendNotification);
        knob->setAccentColour(UiStyle::categoryColour(node->getName()));

        Param* pPtr = p.param;
        knob->onValueChange = [this, knob, pPtr] { pPtr->set((float) knob->getValue()); if (onParametersChanged) onParametersChanged(); };
        addAndMakeVisible(knob);
        knobs.add(knob);
        refreshers.push_back([knob, pPtr] { knob->setValue(pPtr->get(), juce::dontSendNotification); });
    }
    numKnobs = knobs.size();
    rows = numKnobs == 0 ? 1 : (numKnobs + 3) / 4;
}

void NodeCard::mouseMove(const juce::MouseEvent& e)
{
    bool inHelp = helpArea.contains(e.getPosition());
    if (inHelp != helpHovered) { helpHovered = inHelp; repaint(); }
}

void NodeCard::mouseExit(const juce::MouseEvent&)
{
    if (helpHovered) { helpHovered = false; repaint(); }
}

void NodeCard::drawHelpIcon(juce::Graphics& g, juce::Rectangle<float> r)
{
    float d = juce::jmin(r.getWidth(), r.getHeight());
    float radius = d * 0.42f;
    auto c = r.getCentre();
    g.setColour(UiStyle::textFaint.withAlpha(helpHovered ? 0.9f : 0.55f));
    g.drawEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
    g.setFont(juce::Font(juce::FontOptions(d * 0.68f, juce::Font::bold)));
    g.drawText("?", r.toNearestInt(), juce::Justification::centred, false);
}

void NodeCard::drawVisualization(juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::String name = node->getName();
    juce::Colour cat = UiStyle::categoryColour(name);

    if (fullCardWaterfall) return;

    g.setColour(UiStyle::meterBg.withAlpha(0.55f));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(UiStyle::cardBorder.withAlpha(0.4f));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);

    auto inner = r.reduced(8.0f);
    if (inner.getWidth() < 20.0f || inner.getHeight() < 10.0f) return;

    // One descriptor query per paint instead of one per lookup (each query built
    // a fresh vector of juce::Strings).
    const auto descriptors = node->getParameters();
    auto paramById = [&](const juce::String& id) -> float {
        for (const auto& p : descriptors)
            if (p.id == id && p.param != nullptr) return p.param->get();
        return 0.0f;
    };

    if (name == "High-Pass")
    {
        float cutoff = paramById("cutoff");
        const double rate = engine.getCurrentSampleRate();
        const auto coeffs = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(rate, juce::jmax(10.0f, cutoff), 0.707f);
        drawFreqResponse(g, inner, [coeffs, rate](float f) { return biquadMagnitudeDb(coeffs, rate, f); },
                        cat, 24.0f);
        float x = freqToX(cutoff, inner);
        g.setColour(cat.withAlpha(0.7f));
        g.drawLine(x, inner.getY(), x, inner.getBottom(), 1.2f);
    }
    else if (name == "Noise Gate")
    {
        float thresh = paramById("gateThreshold");
        // The gate threshold range is -80..-20 dB, so the meter must cover the
        // same span; flooring the input at -60 dB made "OPEN" permanent for any
        // threshold below -60 and the marker pinned to the left edge.
        float inDb = juce::Decibels::gainToDecibels(engine.getInputLevel(), -80.0f);
        float inputNorm = juce::jlimit(0.0f, 1.0f, (inDb + 80.0f) / 80.0f);
        float threshNorm = juce::jlimit(0.0f, 1.0f, (thresh + 80.0f) / 80.0f);

        auto meterArea = inner.removeFromTop(inner.getHeight() * 0.55f);
        inner.removeFromTop(8.0f);
        auto statusArea = inner;

        drawHorizontalMeterWithZones(g, meterArea, inputNorm);

        float threshX = meterArea.getX() + meterArea.getWidth() * threshNorm;
        g.setColour(juce::Colour(0xffff4d4d));
        g.drawLine(threshX, meterArea.getY() - 2.0f,
                   threshX, meterArea.getBottom() + 2.0f, 2.0f);

        juce::Path tri;
        tri.startNewSubPath(threshX, meterArea.getY() - 3.0f);
        tri.lineTo(threshX - 4.0f, meterArea.getY() - 8.0f);
        tri.lineTo(threshX + 4.0f, meterArea.getY() - 8.0f);
        tri.closeSubPath();
        g.fillPath(tri);

        bool open = inDb > thresh;
        g.setColour(open ? UiStyle::onGreen : UiStyle::textFaint);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(open ? "OPEN" : "CLOSED",
                   statusArea.toNearestInt(),
                   juce::Justification::centred, false);
    }
    else if (name == "Spectral NR")
    {
        const float floorDb = paramById("nrMaxReduction");
        const float amount = paramById("nrAmount");
        // Live spectrum with the attenuation floor drawn as a flat line: the real
        // per-bin reduction follows the signal, so a single floor is honest here.
        auto responseFn = [floorDb](float) { return floorDb; };
        auto normX = [](float freq) {
            return std::log(freq / 40.0f) / std::log(20000.0f / 40.0f);
        };
        auto dbNormY = [](float db, float dbRange) { return 0.5f - (db / dbRange) * 0.45f; };

        const float dbRange = 30.0f;
        std::vector<juce::Point<float>> handles;
        handles.push_back({ (float) normX(1200.0f), dbNormY(floorDb, dbRange) });
        drawDynamicEQ(g, engine, inner, responseFn, cat, handles, dbRange);

        g.setColour(UiStyle::text);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(juce::String((int) amount) + "%",
                   inner.toNearestInt(), juce::Justification::topLeft, false);
    }
    else if (name == "EQ")
    {
        float lowFreq  = paramById("lowFreq");
        float lowGain  = paramById("lowGain");
        float midFreq  = paramById("midFreq");
        float midGain  = paramById("midGain");
        float midQ     = paramById("midQ");
        float highFreq = paramById("highFreq");
        float highGain = paramById("highGain");

        // Draw the real cascade (low shelf + peak with its Q + high shelf) using
        // the same coefficient factories as EqNode, so midQ visibly does something
        // and the corner levels are not 3 dB off.
        const double rate = engine.getCurrentSampleRate();
        const auto cLow  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf(
            rate, juce::jmax(10.0f, lowFreq), 0.707f, juce::Decibels::decibelsToGain(lowGain));
        const auto cMid  = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
            rate, juce::jmax(10.0f, midFreq), juce::jlimit(0.1f, 10.0f, midQ),
            juce::Decibels::decibelsToGain(midGain));
        const auto cHigh = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf(
            rate, juce::jmax(10.0f, highFreq), 0.707f, juce::Decibels::decibelsToGain(highGain));

        auto responseFn = [cLow, cMid, cHigh, rate](float f) {
            return biquadMagnitudeDb(cLow, rate, f) + biquadMagnitudeDb(cMid, rate, f)
                 + biquadMagnitudeDb(cHigh, rate, f);
        };

        auto normX = [](float freq) {
            return std::log(freq / 40.0f) / std::log(20000.0f / 40.0f);
        };
        auto dbNormY = [=](float db, float dbRange) {
            return 0.5f - (db / dbRange) * 0.45f;
        };

        float dbRange = 18.0f;
        std::vector<juce::Point<float>> handles;
        handles.push_back({ (float)normX(lowFreq),  dbNormY(responseFn(lowFreq),  dbRange) });
        handles.push_back({ (float)normX(midFreq),  dbNormY(responseFn(midFreq),  dbRange) });
        handles.push_back({ (float)normX(highFreq), dbNormY(responseFn(highFreq), dbRange) });

        drawDynamicEQ(g, engine, inner, responseFn, cat, handles, dbRange);
    }
    else if (name == "Multi-Band")
    {
        juce::StringArray labels { "LOW", "MID", "HIGH" };
        const float bandGap = 40.0f;
        float bandW = (inner.getWidth() - bandGap * 2) / 3.0f;
        const float colW = 52.0f;
        const float colGap = 14.0f;
        const float metersTotalW = colW * 3 + colGap * 2;

        for (int band = 0; band < 3; ++band)
        {
            float bx = inner.getX() + band * (bandW + bandGap);
            float metersX = bx + (bandW - metersTotalW) * 0.5f;

            auto labelArea = juce::Rectangle<float>(bx, inner.getY(), bandW, 20.0f);
            g.setColour(UiStyle::textFaint);
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.drawText(labels[band], labelArea.toNearestInt(),
                       juce::Justification::centred, false);

            auto meterTop = inner.getY() + 24.0f;
            auto meterH = inner.getHeight() - 24.0f - 16.0f;

            auto inBar = juce::Rectangle<float>(metersX, meterTop, colW, meterH);
            auto outBar = juce::Rectangle<float>(metersX + colW + colGap, meterTop,
                                                 colW, meterH);
            auto grBar = juce::Rectangle<float>(metersX + (colW + colGap) * 2, meterTop,
                                                colW, meterH);

            float inV  = node->getMeterValue(band * 3 + 0);
            float outV = node->getMeterValue(band * 3 + 1);
            float grV  = node->getMeterValue(band * 3 + 2);

            drawVerticalMeterWithZones(g, inBar, inV, false);
            drawVerticalMeterWithZones(g, outBar, outV, false);
            drawVerticalMeterWithZones(g, grBar, grV, true);

            g.setColour(UiStyle::textFaint);
            g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

            auto lIn  = juce::Rectangle<int>((int)inBar.getX(), (int)inBar.getBottom() + 2,
                                             (int)inBar.getWidth(), 13);
            auto lOut = juce::Rectangle<int>((int)outBar.getX(), (int)outBar.getBottom() + 2,
                                             (int)outBar.getWidth(), 13);
            auto lGr  = juce::Rectangle<int>((int)grBar.getX(), (int)grBar.getBottom() + 2,
                                             (int)grBar.getWidth(), 13);

            g.setColour(UiStyle::textFaint);
            g.drawText("IN",  lIn,  juce::Justification::centred, false);
            g.drawText("OUT", lOut, juce::Justification::centred, false);
            g.setColour(juce::Colour(0xffff6b6b));
            g.drawText("GR",  lGr,  juce::Justification::centred, false);
        }
    }
    else if (name == "Compressor")
    {
        float inV  = node->getMeterValue(0);
        float outV = node->getMeterValue(1);
        float gr   = node->getMeterValue(2);
        float thresh = paramById("threshold");
        float ratio  = paramById("ratio");
        float knee   = paramById("knee");

        auto curveArea = inner.removeFromLeft(inner.getWidth() * 0.55f);
        inner.removeFromLeft(14.0f);
        auto barArea = inner;

        g.setColour(UiStyle::cardBorder.withAlpha(0.25f));
        for (int i = 1; i < 4; ++i)
        {
            float t = (float) i / 4.0f;
            g.drawLine(curveArea.getX() + t * curveArea.getWidth(),
                       curveArea.getY(),
                       curveArea.getX() + t * curveArea.getWidth(),
                       curveArea.getBottom(), 0.4f);
            g.drawLine(curveArea.getX(),
                       curveArea.getY() + t * curveArea.getHeight(),
                       curveArea.getRight(),
                       curveArea.getY() + t * curveArea.getHeight(), 0.4f);
        }

        g.setColour(UiStyle::cardBorder.withAlpha(0.35f));
        g.drawLine(curveArea.getX(), curveArea.getBottom(),
                   curveArea.getRight(), curveArea.getY(), 0.7f);

        juce::Path curve;
        int steps = 120;
        float tNorm = juce::jlimit(0.0f, 1.0f, (thresh + 60.0f) / 60.0f);
        for (int i = 0; i <= steps; ++i)
        {
            float t = (float) i / steps;
            float inputDb = -60.0f + t * 60.0f;
            float outputDb;
            const float over = inputDb - thresh;
            if (over <= 0.0f) outputDb = inputDb;
            else
            {
                // Same soft-knee curve as CompressorNode (smoothstep inside the knee).
                float reduced;
                const float k = 1.0f - 1.0f / juce::jmax(1.0f, ratio);
                if (knee > 0.0f && over < knee)
                {
                    const float u = over / knee;
                    reduced = over * k * u * u * (3.0f - 2.0f * u);
                }
                else reduced = over * k;
                outputDb = inputDb - reduced;
            }
            float x = curveArea.getX() + t * curveArea.getWidth();
            float y = curveArea.getBottom() -
                      (outputDb + 60.0f) / 60.0f * curveArea.getHeight();
            if (i == 0) curve.startNewSubPath(x, y);
            else        curve.lineTo(x, y);
        }
        g.setColour(cat.withAlpha(0.25f));
        g.strokePath(curve, juce::PathStrokeType(3.0f));
        g.setColour(cat);
        g.strokePath(curve, juce::PathStrokeType(1.6f));

        float tx = curveArea.getX() + tNorm * curveArea.getWidth();
        g.setColour(UiStyle::meterHigh.withAlpha(0.7f));
        g.drawLine(tx, curveArea.getY(), tx, curveArea.getBottom(), 0.8f);

        const float colW = 52.0f;
        const float colGap = 14.0f;
        const float totalW = colW * 3 + colGap * 2;
        float bx = barArea.getX() + (barArea.getWidth() - totalW) * 0.5f;

        auto inBar = juce::Rectangle<float>(bx, barArea.getY(), colW, barArea.getHeight());
        auto outBar = juce::Rectangle<float>(bx + colW + colGap, barArea.getY(),
                                             colW, barArea.getHeight());
        auto grBar = juce::Rectangle<float>(bx + (colW + colGap) * 2, barArea.getY(),
                                            colW, barArea.getHeight());

        drawVerticalMeterWithZones(g, inBar, inV, false);
        drawVerticalMeterWithZones(g, outBar, outV, false);
        drawVerticalMeterWithZones(g, grBar, gr, true);

        g.setColour(UiStyle::textFaint);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("IN",  inBar.withY(inBar.getBottom() + 2.0f).withHeight(13.0f).toNearestInt(),
                   juce::Justification::centred, false);
        g.drawText("OUT", outBar.withY(outBar.getBottom() + 2.0f).withHeight(13.0f).toNearestInt(),
                   juce::Justification::centred, false);
        g.setColour(juce::Colour(0xffff6b6b));
        g.drawText("GR",  grBar.withY(grBar.getBottom() + 2.0f).withHeight(13.0f).toNearestInt(),
                   juce::Justification::centred, false);
    }
    else if (name == "Saturation")
    {
        auto area = inner;
        g.setColour(UiStyle::cardBorder.withAlpha(0.22f));
        g.drawLine(area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 0.5f);
        g.drawLine(area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom(), 0.5f);
        g.setColour(UiStyle::cardBorder.withAlpha(0.35f));
        g.drawLine(area.getX(), area.getBottom(), area.getRight(), area.getY(), 0.7f);

        float drive = juce::Decibels::decibelsToGain(paramById("drive"));
        float comp = 1.0f / std::sqrt(drive);
        juce::Path p;
        int steps = 200;
        for (int i = 0; i <= steps; ++i)
        {
            float t = (float) i / steps;
            float x = (t - 0.5f) * 2.0f;
            float y = std::tanh(x * drive) * comp;
            float px = area.getX() + t * area.getWidth();
            float py = area.getCentreY() - y * area.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath(px, py);
            else        p.lineTo(px, py);
        }
        g.setColour(cat.withAlpha(0.28f));
        g.strokePath(p, juce::PathStrokeType(4.0f));
        g.setColour(cat);
        g.strokePath(p, juce::PathStrokeType(1.8f));

        auto centre = area.getCentre();
        g.setColour(UiStyle::meterLow.withAlpha(0.9f));
        g.fillEllipse(centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    }
    else if (name == "De-Esser")
    {
        float sibFreq = paramById("deEssFreq");
        float amount = paramById("deEssAmount");

        int bands = engine.getSpectrumBands();
        float t = std::log(sibFreq / 40.0f) / std::log(20000.0f / 40.0f);
        int band = juce::jlimit(0, bands - 1, (int)(t * bands));
        float sibEnergy = engine.getSpectrumBand(band);

        static float smoothedSib = 0.0f;
        smoothedSib = smoothedSib * 0.85f + sibEnergy * 0.15f;

        float notchDepth = (amount / 100.0f) * smoothedSib * 14.0f;

        auto responseFn = [=](float f) {
            float lf = std::log(f / sibFreq);
            float sigma = 0.45f;
            float g = std::exp(-(lf * lf) / (2.0f * sigma * sigma));
            return -g * notchDepth;
        };

        auto normX = [](float freq) {
            return std::log(freq / 40.0f) / std::log(20000.0f / 40.0f);
        };
        auto dbNormY = [=](float db, float dbRange) {
            return 0.5f - (db / dbRange) * 0.45f;
        };

        float dbRange = 18.0f;
        std::vector<juce::Point<float>> handles;
        float notchDb = -notchDepth;
        handles.push_back({ (float)normX(sibFreq), dbNormY(notchDb, dbRange) });

        drawDynamicEQ(g, engine, inner, responseFn, cat, handles, dbRange);

        g.setColour(UiStyle::text);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(juce::String((int) sibFreq) + " Hz",
                   inner.toNearestInt(), juce::Justification::topLeft, false);
    }
    else if (name == "Limiter")
    {
        auto waveArea = inner.removeFromTop(inner.getHeight() * 0.72f);
        inner.removeFromTop(6.0f);
        auto grArea = inner.removeFromTop(8.0f);

        drawWaveformBarsSlow(g, engine, waveArea, cat);

        g.setColour(juce::Colour(0xffff4d4d).withAlpha(0.85f));
        float lineY1 = waveArea.getY() + 3.0f;
        float lineY2 = waveArea.getBottom() - 3.0f;
        for (float x = waveArea.getX(); x < waveArea.getRight(); x += 8.0f)
        {
            g.drawLine(x, lineY1, juce::jmin(x + 4.0f, waveArea.getRight()), lineY1, 1.2f);
            g.drawLine(x, lineY2, juce::jmin(x + 4.0f, waveArea.getRight()), lineY2, 1.2f);
        }

        float gr = node->getMeterValue(0);
        drawHorizontalMeterWithZones(g, grArea, gr);
    }
    else if (name == "Band-Limit")
    {
        float lowHz = paramById("bandLow");
        float highHz = paramById("bandHigh");
        if (lowHz <= 0.0f)  lowHz = 90.0f;
        if (highHz <= 0.0f) highHz = 3100.0f;

        drawFreqResponse(g, inner, [lowHz, highHz](float f) {
            float hp = (f < lowHz) ? -24.0f * (1.0f - f / lowHz) : 0.0f;
            float lp = (f > highHz) ? -24.0f * juce::jmin(1.0f, (f - highHz) / highHz) : 0.0f;
            return hp + lp;
        }, cat, 24.0f);

        auto markAt = [&](float f, juce::Colour col) {
            float x = freqToX(f, inner);
            g.setColour(col.withAlpha(0.75f));
            g.drawLine(x, inner.getY(), x, inner.getBottom(), 1.2f);
        };
        markAt(lowHz, juce::Colour(0xff3ddc84));
        markAt(highHz, juce::Colour(0xff3ddc84));

        g.setColour(UiStyle::textFaint);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(juce::String((int) lowHz),
                   inner.toNearestInt(), juce::Justification::bottomLeft, false);
        g.drawText(juce::String((int) highHz),
                   inner.toNearestInt(), juce::Justification::bottomRight, false);
    }
}

void NodeCard::paint(juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat().reduced(1.0f);

    if (!largePanel)
    {
        juce::Path shadow;
        shadow.addRoundedRectangle(outer.getX() + 1.0f, outer.getY() + 3.0f,
                                   outer.getWidth(), outer.getHeight(), 12.0f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillPath(shadow);
    }

    {
        // 卡片轻微透明，让背景图在空白区域略微显现
        juce::ColourGradient grad(UiStyle::cardTop.withAlpha(0.85f),
                                  outer.getCentreX(), outer.getY(),
                                  UiStyle::cardBottom.withAlpha(0.88f),
                                  outer.getCentreX(), outer.getBottom(),
                                  false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(outer, 12.0f);
    }

    if (fullCardWaterfall && onState)
    {
        juce::Graphics::ScopedSaveState save(g);

        juce::Path clip;
        clip.addRoundedRectangle(outer, 12.0f);
        g.reduceClipRegion(clip);

        g.setColour(juce::Colour(0x60000000));
        g.fillRect(outer);

        drawWaterfall3D(g, engine, outer);

        juce::ColourGradient topFade(
            juce::Colour(0x80000000),
            outer.getCentreX(), outer.getY(),
            juce::Colours::transparentBlack,
            outer.getCentreX(), outer.getY() + 90.0f,
            false);
        g.setGradientFill(topFade);
        g.fillRect(outer);

        juce::ColourGradient botFade(
            juce::Colours::transparentBlack,
            outer.getCentreX(), outer.getBottom() - 120.0f,
            juce::Colour(0x50000000),
            outer.getCentreX(), outer.getBottom(),
            false);
        g.setGradientFill(botFade);
        g.fillRect(outer);
    }

    juce::Colour cat = UiStyle::categoryColour(node->getName());
    g.setColour(cat.withAlpha(onState ? 0.55f : 0.15f));
    g.drawRoundedRectangle(outer, 12.0f, 1.2f);

    if (!onState)
    {
        g.setColour(juce::Colour(0x99000000));
        g.fillRoundedRectangle(outer, 12.0f);
    }

    {
        juce::Rectangle<float> bar(headerArea.getX(),
                                   headerArea.getY() + 2.0f,
                                   3.0f,
                                   headerArea.getHeight() - 4.0f);
        g.setColour(cat);
        g.fillRoundedRectangle(bar, 1.5f);
    }

    {
        g.setColour(onState ? UiStyle::text : UiStyle::textDim);
        g.setFont(juce::Font(juce::FontOptions(largePanel ? 20.0f : 12.5f,
                                               juce::Font::bold)));
        auto titleArea = headerArea;
        titleArea.removeFromLeft(8);
        g.drawText(displayTitle, titleArea,
                   juce::Justification::centredLeft, false);
    }

    if (!helpArea.isEmpty())
        drawHelpIcon(g, helpArea.toFloat());

    if (!visArea.isEmpty() && hasVisualization(node->getName()) && !fullCardWaterfall)
        drawVisualization(g, visArea.toFloat());

    if (showHelp && !helpArea.isEmpty())
    {
        juce::String help = Loc::t(getHelpKey(node->getName()));
        if (help.isNotEmpty())
        {
            // 用微软雅黑，避免某些默认字体中文瘦高
            juce::Font font(juce::FontOptions("Microsoft YaHei", 17.0f, juce::Font::plain));
            juce::GlyphArrangement ga;
            ga.addFittedText(font, help, 0, 0, 300.0f, 200.0f,
                             juce::Justification::topLeft, 6);
            auto textBounds = ga.getBoundingBox(0, -1, true).toNearestInt();

            int w = juce::jmin(360, juce::jmax(220, textBounds.getWidth() + 28));
            int h = textBounds.getHeight() + 24;

            int x = getWidth() - w - 10;
            int y = helpArea.getBottom() + 8;
            juce::Rectangle<int> bubble(x, y, w, h);

            g.setColour(juce::Colour(0xf0141820));
            g.fillRoundedRectangle(bubble.toFloat(), 8.0f);
            g.setColour(cat.withAlpha(0.7f));
            g.drawRoundedRectangle(bubble.toFloat(), 8.0f, 1.0f);

            g.setColour(UiStyle::text);
            g.setFont(font);
            g.drawFittedText(help, bubble.reduced(14, 12),
                             juce::Justification::topLeft, 6);
        }
    }
}

void NodeCard::resized()
{
    int pad = largePanel ? 32 : 14;
    auto r = getLocalBounds().reduced(pad);

    int headerH = largePanel ? 40 : 22;

    headerArea = r.removeFromTop(headerH);
    helpArea = headerArea.removeFromRight(24);
    headerArea.removeFromRight(6);

    // 瀑布增益滑块放在帮助图标左侧
    if (waterfallGainSlider != nullptr && waterfallGainLabel != nullptr)
    {
        auto sliderArea = headerArea.removeFromRight(260);
        // 左侧标签（60px）
        waterfallGainLabel->setBounds(sliderArea.removeFromLeft(60).reduced(2, 4));
        // 滑块（含右侧数值框）
        waterfallGainSlider->setBounds(sliderArea.reduced(2, 8));
    }

    if (fullCardWaterfall)
    {
        visArea = {};
        auto knobArea = r;
        knobArea.removeFromTop(knobArea.getHeight() * 0.25f);
        knobArea.removeFromBottom(largePanel ? 20 : 10);
        r = knobArea;
    }
    else
    {
        int visH = 0;
        if (hasVisualization(node->getName()))
        {
            // The graph must not starve the knobs: on a short card give the
            // controls priority instead of drawing a squeezed 3D view.
            visH = largePanel ? juce::jlimit(70, 150, (int) ((float) r.getHeight() * 0.30f)) : 56;
        }

        if (visH > 0)
        {
            r.removeFromTop(largePanel ? 20 : 10);
            visArea = r.removeFromTop(visH);
        }
        else
        {
            visArea = {};
        }

        r.removeFromTop(largePanel ? 30 : 12);
    }

    if (simple)
    {
        std::vector<Knob*> all;
        for (auto* k : macroKnobs) all.push_back(k);
        for (auto* k : knobs)      all.push_back(k);
        if (all.empty()) return;

        int total = (int)all.size();
        int slotW = r.getWidth() / total;
        int slotH = largePanel ? juce::jmin(r.getHeight(), 320) : r.getHeight();
        int top = largePanel ? r.getY() + juce::jmax(0, (r.getHeight() - slotH) / 2)
                             : r.getY();

        for (int i = 0; i < total; ++i)
            all[(size_t)i]->setBounds(r.getX() + i * slotW, top, slotW, slotH);
    }
    else
    {
        if (numKnobs == 0) return;
        // Choose the number of rows from the height we actually have, so a module
        // with many parameters (Multi-Band has 9) gets fewer, much larger knobs
        // instead of a 3x3 grid of tiny ones.
        const int targetRowH = largePanel ? 124 : 64;
        const int maxRows = juce::jmax(1, (r.getHeight() - (largePanel ? 20 : 0)) / juce::jmax(1, targetRowH));
        int rowsActual = juce::jmax(1, juce::jmin((numKnobs + 2) / 3, maxRows));
        int perRow = (numKnobs + rowsActual - 1) / rowsActual;
        perRow = juce::jmax(1, perRow);

        int slotW = r.getWidth() / juce::jmax(1, perRow);
        int slotH = largePanel
                    ? juce::jmin((r.getHeight() - (rowsActual - 1) * 20) / rowsActual, 240)
                    : (r.getHeight() / rowsActual);
        int totalH = rowsActual * slotH + (rowsActual - 1) * (largePanel ? 20 : 0);
        int top = r.getY() + (largePanel ? juce::jmax(0, (r.getHeight() - totalH) / 2) : 0);

        const int usedRows = (numKnobs + perRow - 1) / perRow;
        for (int i = 0; i < numKnobs; ++i)
        {
            const int row = i / perRow;
            const int col = i % perRow;
            // Centre the last row when it is not full.
            const int inRow = juce::jmin(perRow, numKnobs - row * perRow);
            const int rowWidth = inRow * slotW;
            const int rowX = r.getX() + juce::jmax(0, (r.getWidth() - rowWidth) / 2);
            knobs[i]->setBounds(rowX + col * slotW,
                                top + row * (slotH + (largePanel ? 20 : 0)),
                                slotW, slotH);
        }
        juce::ignoreUnused(usedRows);
    }
}
