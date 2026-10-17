#pragma once
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>
#include <atomic>

class BackgroundImageCache
{
public:
    static BackgroundImageCache& get();

    void setImageFile(const juce::File& f);
    juce::File getImageFile() const { return imageFile; }
    void clearImage();

    void setOpacity(float o);
    float getOpacity() const { return opacity; }

    void setBlur(float b);
    float getBlur() const { return blur; }

    void setDim(float d);
    float getDim() const { return dim; }

    const juce::Image& getProcessed(int targetW, int targetH);

    int getVersion() const { return version.load(); }

    void loadFromSettings();
    void saveToSettings();

private:
    BackgroundImageCache() = default;

    juce::File imageFile;
    juce::Image original;
    juce::Image processed;

    float opacity = 0.45f;
    float blur    = 20.0f;
    float dim     = 0.10f;

    int   cachedW = 0;
    int   cachedH = 0;
    float cachedBlur = -1.0f;
    float cachedDim  = -1.0f;

    std::atomic<int> version { 0 };

    void bumpVersion() { version.fetch_add(1); }

    static juce::Image boxBlur(const juce::Image& src, int radius, int passes);
};
