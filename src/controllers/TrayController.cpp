#include "controllers/TrayController.h"

#include "controllers/PlayerController.h"
#include "controllers/SettingsController.h"

#include <QGuiApplication>
#include <QSystemTrayIcon>
#include <QWindow>

using namespace Qt::StringLiterals;

TrayController::TrayController(SettingsController &settings, PlayerController &player, const QIcon &icon,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_tray(new QSystemTrayIcon(icon, this))
{
    m_tray->setToolTip(QGuiApplication::applicationDisplayName());
    // What is playing, as a heading; clicking it opens the window.
    m_nowPlaying = m_menu.addAction(QString(), this, &TrayController::restoreWindow);
    m_menu.addSeparator();
    connect(&m_player, &PlayerController::trackChanged, this, &TrayController::updateNowPlaying);
    connect(&m_player, &PlayerController::playingChanged, this, &TrayController::updateNowPlaying);
    connect(&m_settings, &SettingsController::translationApplied, this, &TrayController::updateNowPlaying);
    updateNowPlaying();
    m_menu.addAction(tr("Show %1").arg(QGuiApplication::applicationDisplayName()), this, &TrayController::restoreWindow);
    m_menu.addSeparator();
    m_menu.addAction(tr("Quit"), qApp, &QCoreApplication::quit);
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
    if (!m_player.hasTrack()) {
        m_nowPlaying->setText(tr("Nothing playing"));
        m_nowPlaying->setEnabled(false);
        m_tray->setToolTip(QGuiApplication::applicationDisplayName());
        return;
    }
    const QString song = m_player.artist().isEmpty() ? m_player.title()
                                                     : tr("%1 – %2").arg(m_player.title(), m_player.artist());
    // "&" would turn the next letter into a keyboard shortcut in the menu.
    m_nowPlaying->setText((m_player.playing() ? u"▶  "_s : u"❚❚  "_s) + QString(song).replace(u'&', u"&&"_s));
    m_nowPlaying->setEnabled(true);
    m_tray->setToolTip(QGuiApplication::applicationDisplayName() + u" — "_s + song);
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
