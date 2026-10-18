import QtQuick
import QtQuick.Controls.Material
import MusicPlayer

// Right-click menu in the app's style: compact rows on PopupBackground instead of Material's
// large sheet. Use CompactMenuItem for its items; submenus are CompactMenus too.
Menu {
    delegate: CompactMenuItem {}
    implicitWidth: 180
    topPadding: 4
    bottomPadding: 4
    background: PopupBackground {}
}
