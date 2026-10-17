#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ModuleSelector : public juce::Component
{
public:
    ModuleSelector();

    void setModules(const juce::StringArray& nodeNames);
    void setSelected(int index);
    void setNodeBypassed(int index, bool bypassed);
    bool isNodeBypassed(int i) const { return i >= 0 && i < (int)items.size() && !items[(size_t)i].on; }
    int  getSelected() const { return selected; }

    std::function<void(int)> onSelectionChange;
    std::function<void(int, bool)> onToggleBypass;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    struct Item
    {
        juce::String nodeName;
        juce::String displayKey;
        juce::Rectangle<int> bounds;
        bool on = true;
    };
    std::vector<Item> items;
    int selected = 0;
    int hovered = -1;

    void drawNodeIcon(juce::Graphics&,
                      juce::Rectangle<float>,
                      const juce::String& nodeName,
                      juce::Colour col);
};
