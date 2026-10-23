#include "controllers/TrayController.h"

#include "controllers/PlayerController.h"
#include "controllers/SettingsController.h"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QToolButton>
#include <QWidgetAction>
#include <QSystemTrayIcon>
#include <QWindow>

using namespace Qt::StringLiterals;

constexpr int kMenuWidth = 240;

TrayController::TrayController(SettingsController &settings, PlayerController &player, const QIcon &icon,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_tray(new QSystemTrayIcon(icon, this))
{
    m_tray->setToolTip(QGuiApplication::applicationDisplayName());
    // The title of what is playing, centered, as a heading; clicking it opens the window.
    m_nowPlaying = new QToolButton;
    m_nowPlaying->setAutoRaise(true);
    m_nowPlaying->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_nowPlaying, &QToolButton::clicked, this, [this] {
        m_menu.close();
        restoreWindow();
    });
    auto *titleAction = new QWidgetAction(&m_menu);
    titleAction->setDefaultWidget(m_nowPlaying);
    m_menu.addAction(titleAction);
    m_menu.addSeparator();
    // Playback controls as one row of icon buttons; clicking them keeps the menu open.
    auto *controls = new QWidget;
    auto *row = new QHBoxLayout(controls);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);
    row->addStretch(); // centered in the fixed-width menu
    const auto addButton = [&](auto onClick) {
        auto *button = new QToolButton(controls);
        button->setAutoRaise(true);
        button->setIconSize(QSize(20, 20));
        connect(button, &QToolButton::clicked, this, onClick);
        row->addWidget(button);
        return button;
    };
    m_shuffle = addButton([this] { m_player.setShuffle(!m_player.shuffle()); });
    m_shuffle->setCheckable(true);
    m_previous = addButton([this] { m_player.previous(); });
    m_playPause = addButton([this] { m_player.togglePlay(); });
    m_next = addButton([this] { m_player.next(); });
    m_repeat = addButton([this] { m_player.setRepeat(PlayerController::Repeat((m_player.repeat() + 1) % 3)); });
    m_repeat->setCheckable(true);
    row->addStretch();
    auto *controlsAction = new QWidgetAction(&m_menu);
    controlsAction->setDefaultWidget(controls);
    m_menu.addAction(controlsAction);
    m_menu.addSeparator();
    connect(&m_player, &PlayerController::shuffleChanged, this, &TrayController::updateNowPlaying);
    connect(&m_player, &PlayerController::repeatChanged, this, &TrayController::updateNowPlaying);
    connect(&m_player, &PlayerController::trackChanged, this, &TrayController::updateNowPlaying);
    connect(&m_player, &PlayerController::playingChanged, this, &TrayController::updateNowPlaying);
    connect(&m_settings, &SettingsController::translationApplied, this, &TrayController::updateNowPlaying);
    updateNowPlaying();
    m_menu.addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    m_menu.setFixedWidth(kMenuWidth);
    m_tray->setContextMenu(&m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            restoreWindow();
    });

    // The icon is only shown while the option is on.
    m_tray->setVisible(m_settings.minimizeToTray());
    connect(&m_settings, &SettingsController::minimizeToTrayChanged, this, [this] {
        m_tray->setVisible(m_settings.minimizeToTray());
        if (!m_settings.minimizeToTray())
            restoreWindow(); // never leave the window hidden with no way back
    });
}

void TrayController::setWindow(QWindow *window)
{
    m_window = window;
    connect(window, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
        // Queued: hiding while Windows is still processing the minimize is undone by it.
        if (state == Qt::WindowMinimized && m_settings.minimizeToTray())
            QMetaObject::invokeMethod(m_window, &QWindow::hide, Qt::QueuedConnection);
    });
}

void TrayController::updateNowPlaying()
{
    m_shuffle->setIcon(menuIcon(u"shuffle"_s));
    m_shuffle->setChecked(m_player.shuffle());
    m_previous->setIcon(menuIcon(u"previous"_s));
    m_playPause->setIcon(menuIcon(m_player.playing() ? u"pause"_s : u"play"_s));
    m_playPause->setEnabled(m_player.hasTrack());
    m_next->setIcon(menuIcon(u"next"_s));
    m_repeat->setIcon(menuIcon(m_player.repeat() == PlayerController::RepeatOne ? u"repeat_one"_s : u"repeat"_s));
    m_repeat->setChecked(m_player.repeat() != PlayerController::RepeatOff);
    if (!m_player.hasTrack()) {
        m_nowPlaying->setText(tr("Nothing playing"));
        m_nowPlaying->setEnabled(false);
        m_tray->setToolTip(QGuiApplication::applicationDisplayName());
        return;
    }
    const QString song = m_player.artist().isEmpty() ? m_player.title()
                                                     : tr("%1 – %2").arg(m_player.title(), m_player.artist());
    // Cut to the menu's fixed width rather than widening the menu.
    const QString shown = m_nowPlaying->fontMetrics().elidedText(m_player.title(), Qt::ElideRight, kMenuWidth - 32);
    // "&" would turn the next letter into a keyboard shortcut.
    m_nowPlaying->setText(QString(shown).replace(u'&', u"&&"_s));
    m_nowPlaying->setEnabled(true);
    m_tray->setToolTip(QGuiApplication::applicationDisplayName() + u" — "_s + song);
}

// The app's white icons, recolored to the menu's text color (the menu may be light).
QIcon TrayController::menuIcon(const QString &name) const
{
    QFile file(u":/qt/qml/MusicPlayer/qml/icons/"_s + name + u".svg"_s);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray color = m_menu.palette().color(QPalette::Text).name().toLatin1();
    QByteArray svg = file.readAll().replace("#ffffff", color);
    QBuffer buffer(&svg);
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(QSize(48, 48)); // sharp up to 240 % display scaling at 20 px
    QPixmap pixmap = QPixmap::fromImage(reader.read());
    pixmap.setDevicePixelRatio(48.0 / 20);
    return QIcon(pixmap);
}

void TrayController::restoreWindow()
{
    if (!m_window)
        return;
    m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}
