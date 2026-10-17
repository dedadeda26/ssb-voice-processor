#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "AppSettings.h"
#include "SplashComponent.h"
#include "icon_data.h"

// ============ 生成应用图标（托盘 + 窗口 + 任务栏） ============
// 从编译进 exe 的 PNG 里加载，缩放到指定尺寸
static juce::Image createAppIcon(int size)
{
    auto img = juce::ImageFileFormat::loadFrom(
        IconData::icon_png, (size_t) IconData::icon_png_size);

    juce::Image scaled(juce::Image::ARGB, size, size, true);
    juce::Graphics g(scaled);

    if (img.isValid() && img.getWidth() > 0 && img.getHeight() > 0)
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(img, juce::Rectangle<float>(0.0f, 0.0f,
                                                (float) size, (float) size));
        return scaled;
    }

    // The embedded PNG failed to decode. Returning a valid-but-fully-transparent
    // image made JUCE create an invisible tray icon (it only checks isValid()),
    // which is why the tray looked empty. Draw something opaque instead.
    const float w = (float) size;
    g.setColour(juce::Colour(0xff1b2536));
    g.fillRoundedRectangle(0.5f, 0.5f, w - 1.0f, w - 1.0f, w * 0.22f);
    g.setColour(juce::Colour(0xff5eb8ff));
    g.fillRoundedRectangle(w * 0.31f, w * 0.16f, w * 0.38f, w * 0.40f, w * 0.19f);
    g.drawLine(w * 0.50f, w * 0.56f, w * 0.50f, w * 0.78f, juce::jmax(1.0f, w * 0.08f));
    g.drawLine(w * 0.30f, w * 0.78f, w * 0.70f, w * 0.78f, juce::jmax(1.0f, w * 0.08f));
    return scaled;
}

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name, juce::Colour(0xff0a0d13), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, false);
        setResizeLimits(1000, 640, 2800, 1600);

        auto* s = AppSettings::get();
        int w = 1360, h = 820, x = -1, y = -1;
        if (s != nullptr)
        {
            w = s->getIntValue("windowW", 1360);
            h = s->getIntValue("windowH", 820);
            x = s->getIntValue("windowX", -1);
            y = s->getIntValue("windowY", -1);
        }
        w = juce::jlimit(1000, 2800, w);
        h = juce::jlimit(640, 1600, h);

        bool placed = false;
        if (x >= 0 && y >= 0)
        {
            juce::Rectangle<int> saved(x, y, w, h);
            bool onScreen = false;
            for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            {
                if (d.userArea.intersects(saved)) { onScreen = true; break; }
            }
            if (onScreen) { setBounds(saved); placed = true; }
        }
        if (!placed) centreWithSize(w, h);

        setAlwaysOnTop(true);
        setVisible(true);
        setAlwaysOnTop(false);
        toFront(true);
    }

    ~MainWindow() override {}

    MainComponent* getMainComponent() const
    {
        return dynamic_cast<MainComponent*> (getContentComponent());
    }

    void saveWindowBounds()
    {
        if (auto* s = AppSettings::get())
        {
            s->setValue("windowX", getX());
            s->setValue("windowY", getY());
            s->setValue("windowW", getWidth());
            s->setValue("windowH", getHeight());
            s->saveIfNeeded();
        }
    }

    void closeButtonPressed() override
    {
        saveWindowBounds();
        int behavior = 2;
        if (auto* s = AppSettings::get())
            behavior = s->getIntValue("closeBehaviour", 2);

        if (behavior == 2)
            setVisible(false);          // 真正隐藏到状态栏
        else
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    void minimiseButtonPressed() override
    {
        int behavior = 1;
        if (auto* s = AppSettings::get())
            behavior = s->getIntValue("minimiseBehaviour", 1);

        if (behavior == 2)
            setVisible(false);
        else
            DocumentWindow::minimiseButtonPressed();
    }
};

class TrayIcon : public juce::SystemTrayIconComponent
{
public:
    TrayIcon(MainWindow* w) : window(w)
    {
        // 基类已经把组件加到了桌面（有自己的窗口），图标在 setIconImage 里注册。
        setSize(1, 1);
        auto colourImg = createAppIcon(32);
        setIconImage(colourImg, colourImg.createCopy());
        setIconTooltip("SSB Voice Processor");
    }

    void showMainWindow()
    {
        if (window != nullptr)
        {
            window->setVisible(true);
            window->setAlwaysOnTop(true);
            window->toFront(true);
            window->setAlwaysOnTop(false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! e.mods.isRightButtonDown())
        {
            showMainWindow();
            return;
        }

        auto* main = window != nullptr ? window->getMainComponent() : nullptr;
        const bool muted = main != nullptr && main->isMuted();
        const bool bypassed = main != nullptr && main->isBypass();

        juce::PopupMenu m;
        m.addItem(1, juce::String::fromUTF8(u8"显示主界面"));
        m.addSeparator();
        m.addItem(2, juce::String::fromUTF8(u8"静音（切断输出）"), true, muted);
        m.addItem(3, juce::String::fromUTF8(u8"原声（总旁路）"), true, bypassed);
        m.addSeparator();
        m.addItem(4, juce::String::fromUTF8(u8"退出"));

        juce::Component::SafePointer<TrayIcon> safe(this);
        m.showMenuAsync({}, [safe](int result)
        {
            if (safe == nullptr) return;
            auto* main = safe->window != nullptr ? safe->window->getMainComponent() : nullptr;

            if (result == 1) safe->showMainWindow();
            else if (result == 2 && main != nullptr) main->setMuted(! main->isMuted());
            else if (result == 3 && main != nullptr) main->setBypass(! main->isBypass());
            else if (result == 4)
            {
                if (safe->window != nullptr) safe->window->saveWindowBounds();
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
        });
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        showMainWindow();
    }

private:
    MainWindow* window = nullptr;
};

class SSBVoiceApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "SSB Voice Processor"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override
    {
        AppSettings::get();

        // 1) 先显示启动动画
        splash = std::make_unique<SplashComponent>();
        splash->onFinished = [this]() {
            // 隐藏 Splash
            if (splash != nullptr)
                splash->setVisible(false);

            // 2) 创建主窗口 + 托盘
            mainWindow.reset(new MainWindow(getApplicationName()));
            trayIcon.reset(new TrayIcon(mainWindow.get()));

            // 3) 稍微延迟一下再销毁 Splash
            juce::Component::SafePointer<SplashComponent> safeSplash(splash.get());
            juce::MessageManager::callAsync([this, safeSplash]() {
                if (safeSplash != nullptr)
                    splash.reset();
            });
        };
    }

    void shutdown() override
    {
        if (mainWindow != nullptr) mainWindow->saveWindowBounds();
        trayIcon.reset();
        mainWindow = nullptr;
        splash.reset();
    }

    void systemRequestedQuit() override { quit(); }

    // Single-instance: a second launch hands its command line to this process.
    // Without this override the user gets no feedback at all when the window is
    // hidden in the tray and it looks like the app failed to start.
    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible(true);
            mainWindow->setAlwaysOnTop(true);
            mainWindow->toFront(true);
            mainWindow->setAlwaysOnTop(false);
        }
    }

private:
    std::unique_ptr<SplashComponent> splash;
    std::unique_ptr<MainWindow>       mainWindow;
    std::unique_ptr<TrayIcon>         trayIcon;
};

START_JUCE_APPLICATION(SSBVoiceApplication)