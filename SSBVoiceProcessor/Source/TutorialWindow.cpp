#include "TutorialWindow.h"
#include "UiStyle.h"

namespace
{
//==============================================================================
// 绘图小工具（文字统一走 fromUTF8，避免窄字符串按本地编码解释导致乱码）
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

void drawBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
              juce::Colour fill, juce::Colour textColour, float fontHeight)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, 7.0f);

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (fontHeight, juce::Font::bold)));
    g.drawFittedText (text, r.toNearestInt().reduced (5, 0),
                      juce::Justification::centred, 1);
}

void drawArrow (juce::Graphics& g, float x1, float x2, float y, juce::Colour colour)
{
    g.setColour (colour);
    g.drawLine (x1, y, x2 - 7.0f, y, 1.8f);

    juce::Path head;
    head.startNewSubPath (x2, y);
    head.lineTo (x2 - 8.0f, y - 5.0f);
    head.lineTo (x2 - 8.0f, y + 5.0f);
    head.closeSubPath();
    g.fillPath (head);
}

// 高亮圈 + 指示文字（默认写在下方，也可以写在左侧）
void drawCallout (juce::Graphics& g, juce::Rectangle<float> target,
                  const juce::String& text, juce::Colour colour, bool labelBelow = true)
{
    g.setColour (colour);
    g.drawRoundedRectangle (target.expanded (6.0f), 10.0f, 2.2f);

    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.setColour (colour);

    if (labelBelow)
    {
        auto label = target.withY (target.getBottom() + 10.0f).withHeight (22.0f);
        g.drawFittedText (text, label.toNearestInt(), juce::Justification::centred, 1);
    }
    else
    {
        auto label = juce::Rectangle<float> (target.getX() - 210.0f,
                                             target.getCentreY() - 12.0f, 196.0f, 24.0f);
        g.drawFittedText (text, label.toNearestInt(), juce::Justification::centredRight, 1);
    }
}

//==============================================================================
struct TutorialPage
{
    juce::String title;
    juce::StringArray lines;
};

juce::Array<TutorialPage> buildPages()
{
    juce::Array<TutorialPage> pages;

    {
        TutorialPage p;
        p.title = u8 (u8"欢迎使用 SSB 语音处理器");
        p.lines.add (u8 (u8"把麦克风声音实时处理后送给电台，专门为 SSB 通联优化。"));
        p.lines.add (u8 (u8"信号从左到右依次经过下面这些模块："));
        pages.add (p);
    }
    {
        TutorialPage p;
        p.title = u8 (u8"第一步：选择音频设备");
        p.lines.add (u8 (u8"打开「设置」选择麦克风与输出设备，然后点「应用」。"));
        p.lines.add (u8 (u8"推荐 48 kHz、缓冲 256～512：越小延迟越低，越考验 CPU。"));
        pages.add (p);
    }
    {
        TutorialPage p;
        p.title = u8 (u8"第二步：自适应调参（建议先做一次）");
        p.lines.add (u8 (u8"点左上角「自适应调参」，程序录 8 秒后自动设好全部参数。"));
        pages.add (p);
    }
    {
        TutorialPage p;
        p.title = u8 (u8"日常操作");
        p.lines.add (u8 (u8"最常用的按钮，以及托盘图标的右键菜单："));
        pages.add (p);
    }

    return pages;
}

//==============================================================================
class TutorialComponent : public juce::Component
{
public:
    TutorialComponent() : pages (buildPages())
    {
        addAndMakeVisible (backBtn);
        addAndMakeVisible (nextBtn);
        addAndMakeVisible (skipBtn);

        backBtn.setButtonText (u8 (u8"上一步"));
        skipBtn.setButtonText (u8 (u8"跳过"));

        for (auto* b : { &backBtn, &nextBtn, &skipBtn })
        {
            b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        backBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3142));
        skipBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3142));

        backBtn.onClick = [this] { goToPage (page - 1); };
        skipBtn.onClick = [this] { if (onClosed) onClosed(); };
        nextBtn.onClick = [this]
        {
            if (page + 1 >= pages.size()) { if (onClosed) onClosed(); }
            else goToPage (page + 1);
        };

        goToPage (0);
    }

    void goToPage (int newPage)
    {
        page = juce::jlimit (0, pages.size() - 1, newPage);
        backBtn.setEnabled (page > 0);

        const bool last = page + 1 >= pages.size();
        nextBtn.setButtonText (last ? u8 (u8"开始使用") : u8 (u8"下一步"));
        nextBtn.setColour (juce::TextButton::buttonColourId, UiStyle::accentGlow);
        repaint();
    }

    std::function<void()> onClosed;

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (UiStyle::bgBottom);
        g.fillRoundedRectangle (bounds, 10.0f);

        auto content = getLocalBounds().reduced (44, 30);
        content.removeFromBottom (60 + 18);   // 按钮行 + 进度点

        g.setColour (UiStyle::accentGlow);
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
        g.drawFittedText (pages[page].title, content.removeFromTop (44),
                          juce::Justification::centredLeft, 1);
        content.removeFromTop (12);

        g.setColour (UiStyle::text);
        g.setFont (juce::Font (juce::FontOptions (19.0f)));
        for (const auto& line : pages[page].lines)
            g.drawFittedText (line, content.removeFromTop (32),
                              juce::Justification::topLeft, 2);

        content.removeFromTop (18);

        switch (page)
        {
            case 0: drawChainDiagram (g, content);      break;
            case 1: drawDeviceDiagram (g, content);     break;
            case 2: drawAutoTuneDiagram (g, content);   break;
            case 3: drawDailyUseDiagram (g, content);   break;
            default: break;
        }

        drawPageDots (g);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (44, 30);
        auto buttons = r.removeFromBottom (60);

        backBtn.setBounds (buttons.removeFromLeft (150));
        skipBtn.setBounds (buttons.removeFromRight (150));
        buttons.removeFromRight (14);
        nextBtn.setBounds (buttons.removeFromRight (200));
    }

private:
    //==========================================================================
    // 第 1 页：处理链
    void drawChainDiagram (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const char* row1[] = { "输入增益", "高通", "噪声门", "频谱降噪", "EQ", "多段压缩" };
        const char* row2[] = { "主压缩", "饱和", "去齿音", "SSB 带限", "自动电平", "限幅", "输出增益" };

        const float boxH = 64.0f;
        const float gap = 30.0f;

        auto drawRow = [&] (float y, const char* const* names, int count, juce::Colour fill)
        {
            const float boxW = (area.getWidth() - gap * (float) (count - 1)) / (float) count;
            float x = (float) area.getX();

            for (int i = 0; i < count; ++i)
            {
                juce::Rectangle<float> box (x, y, boxW, boxH);
                drawBox (g, box, u8 (names[i]), fill, juce::Colours::white, 16.0f);

                if (i + 1 < count)
                    drawArrow (g, box.getRight() + 6.0f, box.getRight() + gap - 3.0f,
                               box.getCentreY(), UiStyle::cardBorder.brighter (0.5f));

                x += boxW + gap;
            }
        };

        // 让两组方框在剩余空间里垂直居中
        const float blockH = boxH * 2.0f + 26.0f + 46.0f;
        float y = (float) area.getY() + juce::jmax (0.0f, ((float) area.getHeight() - blockH) * 0.5f);

        drawRow (y, row1, 6, juce::Colour (0xff243044));
        drawRow (y + boxH + 26.0f, row2, 7, juce::Colour (0xff1f3b52));

        g.setColour (UiStyle::textFaint);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText (u8 (u8"末端「输出增益」用来匹配电台的输入电平；最终峰值被限制在 −1 dBFS 以内，"
                              u8"留出余量避免削顶。"),
                          juce::Rectangle<int> (area.getX(),
                                                (int) (y + boxH * 2.0f + 40.0f),
                                                area.getWidth(), 40),
                          juce::Justification::topLeft, 2);
    }

    //==========================================================================
    // 第 2 页：设置面板
    void drawDeviceDiagram (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const int panelW = juce::jmin (460, area.getWidth() - 300);
        const int panelH = juce::jmin (area.getHeight(), 356);

        auto panel = juce::Rectangle<int> (area.getX(),
                                           area.getY() + (area.getHeight() - panelH) / 2,
                                           panelW, panelH).toFloat();

        g.setColour (juce::Colour (0xff141a24));
        g.fillRoundedRectangle (panel, 10.0f);
        g.setColour (UiStyle::cardBorder.withAlpha (0.8f));
        g.drawRoundedRectangle (panel, 10.0f, 1.2f);

        g.setColour (UiStyle::accentGlow);
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.drawFittedText (u8 (u8"设置 · 音频设备"),
                          panel.toNearestInt().removeFromTop (42).reduced (16, 0),
                          juce::Justification::centredLeft, 1);

        const char* labels[] = { "驱动类型", "输入设备", "输出设备", "采样率", "缓冲大小" };
        const char* values[] = { "", "", "", "48000 Hz", "512" };

        float y = panel.getY() + 46.0f;
        for (int i = 0; i < 5; ++i)
        {
            auto row = juce::Rectangle<float> (panel.getX() + 16.0f, y,
                                               panel.getWidth() - 32.0f, 38.0f);

            g.setColour (UiStyle::text);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText (u8 (labels[i]), row.withWidth (96.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1);

            auto field = row.withTrimmedLeft (104.0f);
            g.setColour (juce::Colour (0xff1d2735));
            g.fillRoundedRectangle (field, 5.0f);

            g.setColour (i >= 3 ? UiStyle::text : UiStyle::textFaint);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText (values[i][0] != 0 ? juce::String (values[i]) : u8 (u8"自动选择"),
                              field.toNearestInt().reduced (10, 0),
                              juce::Justification::centredLeft, 1);

            y += 46.0f;
        }

        auto applyBtn = juce::Rectangle<float> (panel.getRight() - 130.0f,
                                                panel.getBottom() - 54.0f, 106.0f, 38.0f);
        drawBox (g, applyBtn, u8 (u8"应用"), UiStyle::accentGlow, juce::Colours::white, 16.0f);
        drawCallout (g, applyBtn, u8 (u8"别忘了点「应用」"), juce::Colour (0xffffc857), false);

        auto tips = juce::Rectangle<int> ((int) panel.getRight() + 34, (int) panel.getY(),
                                          area.getRight() - (int) panel.getRight() - 34,
                                          panelH);
        g.setColour (UiStyle::textFaint);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawFittedText (u8 (u8"· 名字里带 USB 的通常是外置声卡\n"
                              u8"· 输入输出用同一块声卡，延迟最低\n"
                              u8"· 设置会保存，下次启动自动恢复"),
                          tips, juce::Justification::topLeft, 6);
    }

    //==========================================================================
    // 第 3 页：8 秒时间轴
    void drawAutoTuneDiagram (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const float barH = 110.0f;
        const float blockH = 40.0f + barH + 40.0f + 58.0f + 30.0f; // 麦克风 + 条 + 间距 + 按钮 + 说明

        float top = (float) area.getY() + juce::jmax (0.0f, ((float) area.getHeight() - blockH) * 0.5f);

        // 麦克风图标（在时间轴上方，留足空隙）
        {
            const float cx = (float) area.getX() + 24.0f;
            const float cy = top + 20.0f;
            g.setColour (UiStyle::text);
            juce::Path mic;
            mic.addRoundedRectangle (cx - 6.0f, cy - 14.0f, 12.0f, 20.0f, 6.0f);
            mic.startNewSubPath (cx - 14.0f, cy - 2.0f);
            mic.quadraticTo (cx - 14.0f, cy + 14.0f, cx, cy + 14.0f);
            mic.quadraticTo (cx + 14.0f, cy + 14.0f, cx + 14.0f, cy - 2.0f);
            g.strokePath (mic, juce::PathStrokeType (2.0f));
            g.drawLine (cx, cy + 14.0f, cx, cy + 20.0f, 2.0f);

            g.setColour (UiStyle::textFaint);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText (u8 (u8"麦克风输入"),
                              juce::Rectangle<int> ((int) cx + 26, (int) cy - 14, 200, 28),
                              juce::Justification::centredLeft, 1);
        }

        auto bar = juce::Rectangle<float> ((float) area.getX(), top + 40.0f,
                                           (float) area.getWidth(), barH);

        const float quietW = bar.getWidth() * 0.25f;
        auto quiet = bar.withWidth (quietW);
        auto talk  = bar.withTrimmedLeft (quietW);

        g.setColour (juce::Colour (0xff2b3648));
        g.fillRoundedRectangle (quiet, 10.0f);
        g.setColour (UiStyle::accentGlow.withAlpha (0.9f));
        g.fillRoundedRectangle (talk, 10.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.drawFittedText (u8 (u8"保持安静 · 2 秒"),
                          quiet.toNearestInt().reduced (6, 0), juce::Justification::centred, 2);
        g.drawFittedText (u8 (u8"正常说话 · 6 秒"),
                          talk.toNearestInt().reduced (6, 0), juce::Justification::centred, 1);

        auto tuneBtn = juce::Rectangle<float> ((float) area.getX(), bar.getBottom() + 42.0f,
                                               230.0f, 48.0f);
        drawBox (g, tuneBtn, u8 (u8"自适应调参"), UiStyle::accentGlow, juce::Colours::white, 17.0f);
        drawCallout (g, tuneBtn, u8 (u8"点这里开始"), juce::Colour (0xffffc857));

        auto note = juce::Rectangle<int> ((int) tuneBtn.getRight() + 44, (int) tuneBtn.getY() - 6,
                                          area.getRight() - (int) tuneBtn.getRight() - 44, 90);
        g.setColour (UiStyle::textFaint);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawFittedText (u8 (u8"完成后自动设好全部参数，\n"
                              u8"不满意可以点「撤销调参」回退。"),
                          note, juce::Justification::topLeft, 4);
    }

    //==========================================================================
    // 第 4 页：常用按钮 + 托盘菜单
    void drawDailyUseDiagram (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const int colW = (area.getWidth() - 40) / 2;
        auto left  = juce::Rectangle<int> (area.getX(), area.getY(), colW, area.getHeight());
        auto right = juce::Rectangle<int> (area.getX() + colW + 40, area.getY(), colW, area.getHeight());

        // 左：主界面按钮（两列三行）
        g.setColour (UiStyle::textFaint);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText (u8 (u8"主界面按钮"), left.removeFromTop (28),
                          juce::Justification::centredLeft, 1);

        struct Cell { const char* text; juce::Colour fill; };
        const Cell cells[] = {
            { "静音", juce::Colour (0xffd64545) },
            { "原声", juce::Colour (0xff2a3142) },
            { "预设", juce::Colour (0xff2a3142) },
            { "重置", juce::Colour (0xff2a3142) },
            { "简单 / 高级", juce::Colour (0xff2a3142) },
            { "设置", juce::Colour (0xff2a3142) },
        };

        const float bw = (left.getWidth() - 14.0f) * 0.5f;
        const float bh = 50.0f;

        for (int i = 0; i < 6; ++i)
        {
            const int col = i % 2, row = i / 2;
            juce::Rectangle<float> b ((float) left.getX() + (float) col * (bw + 14.0f),
                                      (float) left.getY() + 6.0f + (float) row * (bh + 14.0f),
                                      bw, bh);
            drawBox (g, b, u8 (cells[i].text), cells[i].fill, juce::Colours::white, 16.0f);
        }

        // 右：托盘右键菜单
        g.setColour (UiStyle::textFaint);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText (u8 (u8"托盘图标右键"), right.removeFromTop (28),
                          juce::Justification::centredLeft, 1);

        auto menu = right.withWidth (juce::jmin (right.getWidth(), 300))
                         .withHeight (juce::jmin (right.getHeight(), 250)).toFloat();

        g.setColour (juce::Colour (0xff10151d));
        g.fillRoundedRectangle (menu, 10.0f);
        g.setColour (UiStyle::cardBorder);
        g.drawRoundedRectangle (menu, 10.0f, 1.2f);

        const char* items[] = { "显示主界面", "静音（切断输出）", "原声（总旁路）", "退出" };
        float iy = menu.getY() + 12.0f;

        for (int i = 0; i < 4; ++i)
        {
            if (i == 3)
            {
                g.setColour (UiStyle::cardBorder.withAlpha (0.7f));
                g.drawLine (menu.getX() + 12.0f, iy + 2.0f, menu.getRight() - 12.0f, iy + 2.0f, 1.0f);
                iy += 12.0f;
            }

            g.setColour (i == 2 ? UiStyle::textFaint : UiStyle::text);
            g.setFont (juce::Font (juce::FontOptions (16.0f)));
            g.drawFittedText (u8 (items[i]),
                              juce::Rectangle<int> ((int) menu.getX() + 18, (int) iy,
                                                    (int) menu.getWidth() - 36, 44),
                              juce::Justification::centredLeft, 1);
            iy += 50.0f;
        }
    }

    //==========================================================================
    void drawPageDots (juce::Graphics& g)
    {
        const int count = pages.size();
        const float gap = 22.0f;
        const float r = 5.0f;
        const float totalW = (float) count * gap;
        float x = (float) getWidth() * 0.5f - totalW * 0.5f + gap * 0.5f;
        const float y = (float) (getHeight() - 30 - 60 - 22);

        for (int i = 0; i < count; ++i, x += gap)
        {
            g.setColour (i == page ? UiStyle::accentGlow : UiStyle::cardBorder);
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }
    }

    juce::Array<TutorialPage> pages;
    int page = 0;
    juce::TextButton backBtn, nextBtn, skipBtn;
};
} // namespace

//==============================================================================
std::unique_ptr<juce::Component> createTutorialComponent (int startPage)
{
    auto c = std::make_unique<TutorialComponent>();
    c->goToPage (startPage);
    return c;
}

void showFirstRunTutorial (juce::Component* owner)
{
    juce::ignoreUnused (owner);

    auto* content = new TutorialComponent();
    content->setSize (980, 680);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = juce::String::fromUTF8 (u8"快速上手");
    opts.dialogBackgroundColour = UiStyle::bgBottom;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = owner;

    auto* dialog = opts.launchAsync();
    if (dialog == nullptr) return;

    juce::Component::SafePointer<juce::DialogWindow> safeDialog (dialog);
    content->onClosed = [safeDialog]()
    {
        if (safeDialog != nullptr) safeDialog->exitModalState (0);
    };
}
