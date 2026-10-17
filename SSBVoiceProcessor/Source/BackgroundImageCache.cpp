#include "BackgroundImageCache.h"
#include "AppSettings.h"
#include <cmath>

BackgroundImageCache& BackgroundImageCache::get()
{
    static BackgroundImageCache instance;
    return instance;
}

void BackgroundImageCache::setImageFile(const juce::File& f)
{
    imageFile = f;
    original = juce::Image{};
    processed = juce::Image{};

    if (f.existsAsFile())
        original = juce::ImageFileFormat::loadFrom(f);

    cachedW = cachedH = 0;
    cachedBlur = cachedDim = -1.0f;
    saveToSettings();
    bumpVersion();
}

void BackgroundImageCache::clearImage()
{
    imageFile = juce::File{};
    original = juce::Image{};
    processed = juce::Image{};
    cachedW = cachedH = 0;
    cachedBlur = cachedDim = -1.0f;
    saveToSettings();
    bumpVersion();
}

void BackgroundImageCache::setOpacity(float o)
{
    o = juce::jlimit(0.0f, 1.0f, o);
    if (std::abs(o - opacity) > 0.001f)
    {
        opacity = o;
        saveToSettings();
        bumpVersion();
    }
}
void BackgroundImageCache::setBlur(float b)
{
    b = juce::jlimit(0.0f, 40.0f, b);
    if (std::abs(b - blur) > 0.01f) { blur = b; cachedBlur = -1.0f; saveToSettings(); bumpVersion(); }
}
void BackgroundImageCache::setDim(float d)
{
    d = juce::jlimit(0.0f, 1.0f, d);
    if (std::abs(d - dim) > 0.01f) { dim = d; cachedDim = -1.0f; saveToSettings(); bumpVersion(); }
}

const juce::Image& BackgroundImageCache::getProcessed(int targetW, int targetH)
{
    if (!original.isValid() || targetW <= 0 || targetH <= 0)
        return processed;

    bool needRebuild =
        (processed.getWidth()  != targetW) ||
        (processed.getHeight() != targetH) ||
        std::abs(cachedBlur - blur) > 0.01f ||
        std::abs(cachedDim  - dim)  > 0.01f;

    if (!needRebuild && processed.isValid())
        return processed;

    // 缩放（cover）
    float srcAR = (float) original.getWidth() / (float) original.getHeight();
    float dstAR = (float) targetW / (float) targetH;
    int drawW, drawH;
    if (srcAR > dstAR) { drawH = targetH; drawW = (int) std::round(drawH * srcAR); }
    else                { drawW = targetW; drawH = (int) std::round(drawW / srcAR); }

    juce::Image canvas(juce::Image::ARGB, targetW, targetH, true);
    {
        juce::Graphics g(canvas);
        int x = (targetW - drawW) / 2;
        int y = (targetH - drawH) / 2;
        g.drawImage(original, x, y, drawW, drawH,
                    0, 0, original.getWidth(), original.getHeight());
    }

    // 模糊
    if (blur > 0.5f)
        processed = boxBlur(canvas, (int) std::round(blur / 2.0f), 3);
    else
        processed = canvas;

    // 全局暗度（纯黑叠加，不改色相）
    if (dim > 0.01f)
    {
        juce::Graphics g(processed);
        g.setColour(juce::Colours::black.withAlpha(dim * 0.6f));
        g.fillRect(processed.getBounds());
    }

    cachedW = targetW;
    cachedH = targetH;
    cachedBlur = blur;
    cachedDim  = dim;

    return processed;
}

void BackgroundImageCache::loadFromSettings()
{
    if (auto* s = AppSettings::get())
    {
        juce::String path = s->getValue("bgImagePath", "");
        if (path.isNotEmpty())
        {
            juce::File f(path);
            if (f.existsAsFile())
            {
                imageFile = f;
                original = juce::ImageFileFormat::loadFrom(f);
            }
        }
        opacity = juce::jlimit(0.0f, 1.0f, (float) s->getDoubleValue("bgImageOpacity", 0.45));
        blur    = juce::jlimit(0.0f, 40.0f, (float) s->getDoubleValue("bgImageBlur", 20.0));
        dim     = juce::jlimit(0.0f, 1.0f, (float) s->getDoubleValue("bgImageDim", 0.10));
        cachedBlur = cachedDim = -1.0f;
    }
}

void BackgroundImageCache::saveToSettings()
{
    if (auto* s = AppSettings::get())
    {
        s->setValue("bgImagePath", imageFile.getFullPathName());
        s->setValue("bgImageOpacity", (double) opacity);
        s->setValue("bgImageBlur", (double) blur);
        s->setValue("bgImageDim", (double) dim);
        // 清掉旧版本残留的 tint
        if (s->containsKey("bgImageTint"))
            s->removeValue("bgImageTint");
        s->saveIfNeeded();
    }
}

// ============ 三遍 box blur ============
//
// 修复说明：
//   原实现用 juce::Colour(sa, sr, sg, sb) 构造像素，
//   而 Colour 的构造顺序是 (red, green, blue, alpha)，
//   导致所有通道错位 -> 模糊后产生明显色差。
//
//   现改为直接操作 juce::PixelARGB：
//     - setARGB(a, r, g, b) 明确顺序，不再依赖 Colour 构造
//     - 直接读写像素指针，避免每像素构造 Colour 对象的开销
//     - 进入前把图转换为 ARGB，保证像素格式一致
//
juce::Image BackgroundImageCache::boxBlur(const juce::Image& src, int radius, int passes)
{
    if (radius < 1 || !src.isValid()) return src;

    const int W = src.getWidth();
    const int H = src.getHeight();
    if (W <= 0 || H <= 0) return src;

    // 保证像素格式为 ARGB，才能用 PixelARGB 指针访问
    juce::Image a = src.convertedToFormat(juce::Image::ARGB);
    juce::Image b(juce::Image::ARGB, W, H, true);

    for (int p = 0; p < passes; ++p)
    {
        // ---------- 水平 pass ----------
        {
            juce::Image::BitmapData sBD(a, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dBD(b, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < H; ++y)
            {
                const auto* sLine = reinterpret_cast<const juce::PixelARGB*>(sBD.getLinePointer(y));
                      auto* dLine = reinterpret_cast<juce::PixelARGB*>(dBD.getLinePointer(y));

                int sr = 0, sg = 0, sb = 0, sa = 0;
                int xr = juce::jmin(radius, W - 1);
                for (int k = 0; k <= xr; ++k)
                {
                    const auto& px = sLine[k];
                    sa += px.getAlpha();
                    sr += px.getRed();
                    sg += px.getGreen();
                    sb += px.getBlue();
                }
                int n = xr + 1;

                for (int x = 0; x < W; ++x)
                {
                    // 关键修复：PixelARGB::setARGB 的顺序是 (a, r, g, b)
                    dLine[x].setARGB((juce::uint8)(sa / n),
                                     (juce::uint8)(sr / n),
                                     (juce::uint8)(sg / n),
                                     (juce::uint8)(sb / n));

                    int outIdx = x - radius;
                    int inIdx  = x + radius + 1;
                    if (outIdx >= 0)
                    {
                        const auto& px = sLine[outIdx];
                        sa -= px.getAlpha(); sr -= px.getRed();
                        sg -= px.getGreen(); sb -= px.getBlue();
                        --n;
                    }
                    if (inIdx < W)
                    {
                        const auto& px = sLine[inIdx];
                        sa += px.getAlpha(); sr += px.getRed();
                        sg += px.getGreen(); sb += px.getBlue();
                        ++n;
                    }
                }
            }
        }

        // ---------- 垂直 pass ----------
        {
            juce::Image tmp = b.createCopy();
            juce::Image::BitmapData sBD(tmp, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dBD(b,   juce::Image::BitmapData::writeOnly);

            for (int x = 0; x < W; ++x)
            {
                int sr = 0, sg = 0, sb = 0, sa = 0;
                int yb = juce::jmin(radius, H - 1);
                for (int k = 0; k <= yb; ++k)
                {
                    const auto* line = reinterpret_cast<const juce::PixelARGB*>(sBD.getLinePointer(k));
                    const auto& px = line[x];
                    sa += px.getAlpha(); sr += px.getRed();
                    sg += px.getGreen(); sb += px.getBlue();
                }
                int n = yb + 1;

                for (int y = 0; y < H; ++y)
                {
                    auto* dLine = reinterpret_cast<juce::PixelARGB*>(dBD.getLinePointer(y));
                    dLine[x].setARGB((juce::uint8)(sa / n),
                                     (juce::uint8)(sr / n),
                                     (juce::uint8)(sg / n),
                                     (juce::uint8)(sb / n));

                    int outIdx = y - radius;
                    int inIdx  = y + radius + 1;
                    if (outIdx >= 0)
                    {
                        const auto* line = reinterpret_cast<const juce::PixelARGB*>(sBD.getLinePointer(outIdx));
                        const auto& px = line[x];
                        sa -= px.getAlpha(); sr -= px.getRed();
                        sg -= px.getGreen(); sb -= px.getBlue();
                        --n;
                    }
                    if (inIdx < H)
                    {
                        const auto* line = reinterpret_cast<const juce::PixelARGB*>(sBD.getLinePointer(inIdx));
                        const auto& px = line[x];
                        sa += px.getAlpha(); sr += px.getRed();
                        sg += px.getGreen(); sb += px.getBlue();
                        ++n;
                    }
                }
            }
        }

        if (p < passes - 1) a = b.createCopy();
    }
    return b;
}
