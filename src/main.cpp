#include "QHotkey/qhotkey.h"
#include "nvapiwrapper/nvapidrssession.h"
#include "nvapiwrapper/nvapiwrapper.h"
#include "nvapiwrapper/utils.h"
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QCursor>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <thread>
#include <windows.h>
#include <QDesktopServices>
#include <QUrl>

const QString DEFAULT_KEYBINDING_OFF                 = "Ctrl+Alt+P";
const QString DEFAULT_KEYBINDING_FULLSCREEN          = "Ctrl+Alt+O";
const QString DEFAULT_KEYBINDING_FULLSCREEN_WINDOWED = "Ctrl+Alt+L";
const QString DEFAULT_KEYBINDING_TOGGLE              = "Ctrl+Alt+K";

const QString DEFAULT_COLOR_OFF                 = "#181a1b";
const QString DEFAULT_COLOR_FULLSCREEN          = "#e8e6e3";
const QString DEFAULT_COLOR_FULLSCREEN_WINDOWED = "#76b900";

class KeyBindingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KeyBindingDialog(const QString& title, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setFixedSize(300, 100);

        auto* layout = new QVBoxLayout(this);

        m_keyEdit = new QLineEdit(this);
        m_keyEdit->setReadOnly(true);
        m_keyEdit->setPlaceholderText("Press keys to set binding...");
        layout->addWidget(m_keyEdit);

        auto* buttonBox    = new QHBoxLayout();
        auto* okButton     = new QPushButton("OK", this);
        auto* cancelButton = new QPushButton("Cancel", this);
        buttonBox->addWidget(okButton);
        buttonBox->addWidget(cancelButton);
        layout->addLayout(buttonBox);

        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

        m_keyEdit->installEventFilter(this);
    }

    QString getKeyBinding() const
    {
        return m_keyBinding;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == m_keyEdit && event->type() == QEvent::KeyPress)
        {
            auto* keyEvent = dynamic_cast<QKeyEvent*>(event);
            if (keyEvent)
            {
                QKeySequence sequence(keyEvent->key() | keyEvent->modifiers());
                m_keyBinding = sequence.toString();
                m_keyEdit->setText(m_keyBinding);
                return true;
            }
        }
        return QDialog::eventFilter(obj, event);
    }

private:
    QLineEdit* m_keyEdit;
    QString    m_keyBinding;
};

class GSyncTrayIcon : public QSystemTrayIcon
{
    Q_OBJECT
public:
    explicit GSyncTrayIcon(QObject* parent = nullptr)
        : QSystemTrayIcon(parent)
    {
        setupMenu();
        setupIcon();
        updateIconColor();

        connect(this, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger)
            {
                QPoint pos = QCursor::pos();
                contextMenu()->popup(pos);
            }
        });

        try
        {
            NvApiWrapper       nvapi;
            NvApiDrsSession    drs_session;
            NvDRSProfileHandle drs_profile{nullptr};
            NVDRS_SETTING      drs_setting{};

            drs_setting.version = NVDRS_SETTING_VER;
            assertSuccess(nvapi.DRS_LoadSettings(drs_session), "Failed to load session settings!");
            assertSuccess(nvapi.DRS_GetBaseProfile(drs_session, &drs_profile), "Failed to get base profile!");

            NvU32      currentValue{VRR_MODE_DEFAULT};
            const auto status{nvapi.DRS_GetSetting(drs_session, drs_profile, VRR_MODE_ID, &drs_setting)};
            if (status != NVAPI_SETTING_NOT_FOUND)
            {
                assertSuccess(status, "Failed to get VRR setting!");
                currentValue = drs_setting.u32CurrentValue;
            }

            updateTooltip(currentValue);
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(nullptr, "Error", error.what());
        }
    }

    ~GSyncTrayIcon()
    {
        for (auto* hotkey : m_hotkeys)
        {
            delete hotkey;
        }
        m_hotkeys.clear();
    }

private slots:
    void onGSyncModeChanged(int mode)
    {
        try
        {
            NvApiWrapper       nvapi;
            NvApiDrsSession    drs_session;
            NvDRSProfileHandle drs_profile{nullptr};
            NVDRS_SETTING      drs_setting{};

            drs_setting.version = NVDRS_SETTING_VER;
            assertSuccess(nvapi.DRS_LoadSettings(drs_session), "Failed to load session settings!");
            assertSuccess(nvapi.DRS_GetBaseProfile(drs_session, &drs_profile), "Failed to get base profile!");

            if (mode == -1)
            {
                NvU32      currentValue{VRR_MODE_DEFAULT};
                const auto status{nvapi.DRS_GetSetting(drs_session, drs_profile, VRR_MODE_ID, &drs_setting)};
                if (status != NVAPI_SETTING_NOT_FOUND)
                {
                    assertSuccess(status, "Failed to get VRR setting!");
                    currentValue = drs_setting.u32CurrentValue;
                }

                if (currentValue == 1 || currentValue == 2)
                {
                    mode = 0;
                }
                else
                {
                    mode = m_settings.value("last_gsync_mode", 2).toInt();
                }
            }

            drs_setting.settingId       = VRR_MODE_ID;
            drs_setting.settingType     = NVDRS_DWORD_TYPE;
            drs_setting.settingLocation = NVDRS_CURRENT_PROFILE_LOCATION;
            drs_setting.u32CurrentValue = mode;

            assertSuccess(nvapi.DRS_SetSetting(drs_session, drs_profile, &drs_setting), "Failed to set VRR setting!");
            assertSuccess(nvapi.DRS_SaveSettings(drs_session), "Failed to save session settings!");

            if (mode != -1 && mode != 0)
            {
                m_settings.setValue("last_gsync_mode", mode);
            }

            updateIconColor();
            updateMenuCheckmarks(mode);
            updateTooltip(mode);
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(nullptr, "Error", error.what());
        }
    }

    void onStartupToggled(bool checked)
    {
        QSettings startupSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  QSettings::NativeFormat);
        if (checked)
        {
            startupSettings.setValue("GSyncToggle", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        }
        else
        {
            startupSettings.remove("GSyncToggle");
        }
    }

    void onColorChanged(int mode, const QColor& color)
    {
        m_settings.setValue(QString("color_mode_%1").arg(mode), color.name());
        updateIconColor();
    }

    void onKeyBindingChanged(const QString& action, const QString& binding)
    {
        m_settings.setValue(QString("keybinding_%1").arg(action), binding);
        setupKeyBindings();
        updateKeybindingMenuText(action, binding);
    }

    void updateKeybindingMenuText(const QString& action, const QString& binding)
    {
        auto* menu = contextMenu();
        if (!menu)
            return;

        for (auto* menuAction : menu->actions())
        {
            if (menuAction->text() == "Settings" && menuAction->menu())
            {
                auto* settingsMenu = menuAction->menu();

                for (auto* settingsAction : settingsMenu->actions())
                {
                    QString actionText;
                    if (action == "off")
                        actionText = "G-Sync off";
                    else if (action == "fullscreen")
                        actionText = "G-Sync fullscreen only";
                    else if (action == "fullscreen_windowed")
                        actionText = "G-Sync fullscreen and windowed";
                    else if (action == "toggle")
                        actionText = "G-Sync toggle last state/off";

                    if (settingsAction->text().startsWith(actionText))
                    {
                        settingsAction->setText(QString("%1 (%2)").arg(actionText, binding));
                        break;
                    }
                }
                break;
            }
        }
    }

    void onKeyBindingDialog(const QString& action, const QString& title)
    {
        auto* dialog = new KeyBindingDialog(title, nullptr);
        setDialogIcon(dialog);
        if (dialog->exec() == QDialog::Accepted)
        {
            QString newBinding = dialog->getKeyBinding();
            if (!newBinding.isEmpty())
            {
                onKeyBindingChanged(action, newBinding);
            }
        }
        dialog->deleteLater();
    }

    void updateTooltip(int mode)
    {
        static const QString statusStrings[] = {"G-Sync off", "G-Sync fullscreen only",
                                                "G-Sync fullscreen and windowed"};
        setToolTip(statusStrings[mode >= 0 && mode < 3 ? mode : 0]);
    }

private:
    void setupKeyBindings()
    {
        for (auto* hotkey : m_hotkeys)
        {
            delete hotkey;
        }
        m_hotkeys.clear();

        auto createHotkey = [this](const QString& action, const QString& defaultKey, int mode) {
            QString binding = m_settings.value(QString("keybinding_%1").arg(action), defaultKey).toString();
            qDebug() << "Registering hotkey for" << action << "with binding:" << binding;

            auto* hotkey = new QHotkey(QKeySequence(binding), true, this);
            connect(hotkey, &QHotkey::activated, this, [this, mode]() { onGSyncModeChanged(mode); });
            m_hotkeys.append(hotkey);
        };

        createHotkey("off", DEFAULT_KEYBINDING_OFF, 0);
        createHotkey("fullscreen", DEFAULT_KEYBINDING_FULLSCREEN, 1);
        createHotkey("fullscreen_windowed", DEFAULT_KEYBINDING_FULLSCREEN_WINDOWED, 2);
        createHotkey("toggle", DEFAULT_KEYBINDING_TOGGLE, -1);
    }

    QAction* createKeybindingAction(const QString& action, const QString& title)
    {
        QString binding = m_settings
                              .value(QString("keybinding_%1").arg(action),
                                     action == "off"                   ? DEFAULT_KEYBINDING_OFF
                                     : action == "fullscreen"          ? DEFAULT_KEYBINDING_FULLSCREEN
                                     : action == "fullscreen_windowed" ? DEFAULT_KEYBINDING_FULLSCREEN_WINDOWED
                                                                       : DEFAULT_KEYBINDING_TOGGLE)
                              .toString();

        auto* action_item = new QAction(QString("%1 (%2)").arg(title, binding), this);
        connect(action_item, &QAction::triggered, this, [this, action, title]() { onKeyBindingDialog(action, title); });
        return action_item;
    }

    QAction* createColorAction(const QString& text, int mode)
    {
        QColor currentColor = QColor(getColorForMode(mode));

        QPixmap colorPatch(16, 16);
        colorPatch.fill(currentColor);

        auto* action = new QAction(QIcon(colorPatch), text, this);
        connect(action, &QAction::triggered, this, [this, mode, currentColor, action]() {
            QColorDialog* dialog = new QColorDialog(currentColor, nullptr);
            setDialogIcon(dialog);
            dialog->setOption(QColorDialog::ShowAlphaChannel);
            if (dialog->exec() == QDialog::Accepted)
            {
                QColor newColor = dialog->selectedColor();
                onColorChanged(mode, newColor);

                QPixmap newPatch(16, 16);
                newPatch.fill(newColor);
                action->setIcon(QIcon(newPatch));
            }
            dialog->deleteLater();
        });
        return action;
    }

    void setDialogIcon(QDialog* dialog)
    {
        QFile file(":/resources/icon.svg");
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray svgData = file.readAll();
            file.close();

            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);

            QSvgRenderer renderer(svgData);
            QPainter     painter(&pixmap);
            renderer.render(&painter);

            if (!pixmap.isNull())
            {
                dialog->setWindowIcon(QIcon(pixmap));
            }
        }
    }

    void setupMenu()
    {
        auto* menu = new QMenu();

        menu->addAction("Exit", qApp, &QApplication::quit);
        menu->addSeparator();

        auto* settingsMenu  = menu->addMenu("Settings");
        auto* startupAction = settingsMenu->addAction("Run at startup");
        startupAction->setCheckable(true);

        QSettings startupSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  QSettings::NativeFormat);
        startupAction->setChecked(startupSettings.contains("GSyncToggle"));

        connect(startupAction, &QAction::toggled, this, &GSyncTrayIcon::onStartupToggled);

        settingsMenu->addSeparator();

        auto* keybindingsLabel = new QAction("Keybindings", this);
        keybindingsLabel->setEnabled(false);
        settingsMenu->addAction(keybindingsLabel);

        auto* enableKeybindingsAction = settingsMenu->addAction("Enable keybindings");
        enableKeybindingsAction->setCheckable(true);

        bool keybindingsEnabled = m_settings.value("keybindings_enabled", false).toBool();
        enableKeybindingsAction->setChecked(keybindingsEnabled);

        connect(enableKeybindingsAction, &QAction::toggled, this, [this](bool checked) {
            m_settings.setValue("keybindings_enabled", checked);
            if (checked)
            {
                setupKeyBindings();
            }
            else
            {
                for (auto* hotkey : m_hotkeys)
                {
                    delete hotkey;
                }
                m_hotkeys.clear();
            }
        });

        if (keybindingsEnabled)
        {
            setupKeyBindings();
        }

        settingsMenu->addAction(createKeybindingAction("off", "G-Sync off"));
        settingsMenu->addAction(createKeybindingAction("fullscreen", "G-Sync fullscreen only"));
        settingsMenu->addAction(createKeybindingAction("fullscreen_windowed", "G-Sync fullscreen and windowed"));
        settingsMenu->addAction(createKeybindingAction("toggle", "G-Sync toggle last state/off"));

        settingsMenu->addSeparator();

        auto* colorLabel = new QAction("Icon colors", this);
        colorLabel->setEnabled(false);
        settingsMenu->addAction(colorLabel);

        settingsMenu->addAction(createColorAction("G-Sync off", 0));
        settingsMenu->addAction(createColorAction("G-Sync fullscreen only", 1));
        settingsMenu->addAction(createColorAction("G-Sync fullscreen and windowed", 2));

        settingsMenu->addSeparator();

        auto* githubAction = settingsMenu->addAction("View on GitHub");
        connect(githubAction, &QAction::triggered, this, []() {
            QDesktopServices::openUrl(QUrl("https://github.com/seaspaceman/systray-gsync-toggle"));
        });

        menu->addSeparator();

        auto* offAction                = menu->addAction("G-Sync off");
        auto* fullscreenAction         = menu->addAction("G-Sync fullscreen only");
        auto* fullscreenWindowedAction = menu->addAction("G-Sync fullscreen and windowed");

        offAction->setCheckable(true);
        fullscreenAction->setCheckable(true);
        fullscreenWindowedAction->setCheckable(true);

        try
        {
            NvApiWrapper       nvapi;
            NvApiDrsSession    drs_session;
            NvDRSProfileHandle drs_profile{nullptr};
            NVDRS_SETTING      drs_setting{};

            drs_setting.version = NVDRS_SETTING_VER;
            assertSuccess(nvapi.DRS_LoadSettings(drs_session), "Failed to load session settings!");
            assertSuccess(nvapi.DRS_GetBaseProfile(drs_session, &drs_profile), "Failed to get base profile!");

            NvU32      currentValue{VRR_MODE_DEFAULT};
            const auto status{nvapi.DRS_GetSetting(drs_session, drs_profile, VRR_MODE_ID, &drs_setting)};
            if (status != NVAPI_SETTING_NOT_FOUND)
            {
                assertSuccess(status, "Failed to get VRR setting!");
                currentValue = drs_setting.u32CurrentValue;
            }

            switch (currentValue)
            {
                case 0:
                    offAction->setChecked(true);
                    break;
                case 1:
                    fullscreenAction->setChecked(true);
                    break;
                case 2:
                    fullscreenWindowedAction->setChecked(true);
                    break;
            }
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(nullptr, "Error", error.what());
        }

        connect(offAction, &QAction::triggered, this, [this, offAction, fullscreenAction, fullscreenWindowedAction]() {
            onGSyncModeChanged(0);
            offAction->setChecked(true);
            fullscreenAction->setChecked(false);
            fullscreenWindowedAction->setChecked(false);
        });
        connect(fullscreenAction, &QAction::triggered, this,
                [this, offAction, fullscreenAction, fullscreenWindowedAction]() {
                    onGSyncModeChanged(1);
                    offAction->setChecked(false);
                    fullscreenAction->setChecked(true);
                    fullscreenWindowedAction->setChecked(false);
                });
        connect(fullscreenWindowedAction, &QAction::triggered, this,
                [this, offAction, fullscreenAction, fullscreenWindowedAction]() {
                    onGSyncModeChanged(2);
                    offAction->setChecked(false);
                    fullscreenAction->setChecked(false);
                    fullscreenWindowedAction->setChecked(true);
                });

        setContextMenu(menu);
    }

    void setupIcon()
    {
        QFile file(":/resources/icon.svg");
        if (file.open(QIODevice::ReadOnly))
        {
            m_iconSvg = file.readAll();
            file.close();

            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);

            QSvgRenderer renderer(m_iconSvg);
            QPainter     painter(&pixmap);
            renderer.render(&painter);

            if (!pixmap.isNull())
            {
                setIcon(QIcon(pixmap));
            }
            else
            {
                QMessageBox::critical(nullptr, "Error", "Failed to render SVG");
            }
        }
        else
        {
            QMessageBox::critical(nullptr, "Error", "Failed to load icon file: " + file.errorString());
        }
    }

    void updateIconColor()
    {
        try
        {
            NvApiWrapper       nvapi;
            NvApiDrsSession    drs_session;
            NvDRSProfileHandle drs_profile{nullptr};
            NVDRS_SETTING      drs_setting{};

            drs_setting.version = NVDRS_SETTING_VER;
            assertSuccess(nvapi.DRS_LoadSettings(drs_session), "Failed to load session settings!");
            assertSuccess(nvapi.DRS_GetBaseProfile(drs_session, &drs_profile), "Failed to get base profile!");

            NvU32      currentValue{VRR_MODE_DEFAULT};
            const auto status{nvapi.DRS_GetSetting(drs_session, drs_profile, VRR_MODE_ID, &drs_setting)};
            if (status != NVAPI_SETTING_NOT_FOUND)
            {
                assertSuccess(status, "Failed to get VRR setting!");
                currentValue = drs_setting.u32CurrentValue;
            }

            QString color = getColorForMode(currentValue);

            QString coloredSvg = m_iconSvg;
            coloredSvg.replace("fill:#000000", QString("fill:%1").arg(color));
            coloredSvg.replace("stroke:#000000", QString("stroke:%1").arg(color));

            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);

            QSvgRenderer renderer(coloredSvg.toUtf8());
            QPainter     painter(&pixmap);
            renderer.render(&painter);

            if (!pixmap.isNull())
            {
                setIcon(QIcon(pixmap));
            }
        }
        catch (const std::exception& error)
        {
            QMessageBox::critical(nullptr, "Error", error.what());
        }
    }

    QString getColorForMode(int mode)
    {
        static const QString defaultColors[] = {DEFAULT_COLOR_OFF, DEFAULT_COLOR_FULLSCREEN,
                                                DEFAULT_COLOR_FULLSCREEN_WINDOWED};
        return m_settings.value(QString("color_mode_%1").arg(mode), defaultColors[mode >= 0 && mode < 3 ? mode : 0])
            .toString();
    }

    void updateMenuCheckmarks(int mode)
    {
        auto* menu = contextMenu();
        if (!menu)
            return;

        static const QMap<QString, int> modeMap = {
            {"G-Sync off", 0}, {"G-Sync fullscreen only", 1}, {"G-Sync fullscreen and windowed", 2}};

        for (auto* action : menu->actions())
        {
            if (modeMap.contains(action->text()))
            {
                action->setChecked(modeMap[action->text()] == mode);
            }
        }
    }

    QByteArray       m_iconSvg;
    QList<QHotkey*>  m_hotkeys;
    static QSettings m_settings;
};

QSettings GSyncTrayIcon::m_settings("HKEY_CURRENT_USER\\Software\\GSyncToggle", QSettings::NativeFormat);

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QFile file(":/resources/icon.svg");
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray svgData = file.readAll();
        file.close();

        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QSvgRenderer renderer(svgData);
        QPainter     painter(&pixmap);
        renderer.render(&painter);

        if (!pixmap.isNull())
        {
            app.setWindowIcon(QIcon(pixmap));
        }
    }

    GSyncTrayIcon trayIcon;
    trayIcon.show();

    return app.exec();
}

#include "main.moc"
