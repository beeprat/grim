import QtQuick

// One edge or corner of a frameless window; hands the drag to the OS resize.
MouseArea {
    required property int edges

    cursorShape: {
        const horizontal = edges & (Qt.LeftEdge | Qt.RightEdge)
        const vertical = edges & (Qt.TopEdge | Qt.BottomEdge)
        if (horizontal && vertical)
            return (edges === (Qt.LeftEdge | Qt.TopEdge) || edges === (Qt.RightEdge | Qt.BottomEdge))
                   ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
        return horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }
    onPressed: Window.window.startSystemResize(edges)
}
