import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
// Importing the Material module directly selects the style at compile time.
import QtQuick.Controls.Material
import MusicPlayer

ApplicationWindow {
    id: root

    // Index of the page in the StackLayout: 0 Library, 1 Playlists, 2 Statistics, 3 Soulseek, 4 Settings.
    property int page: 0
    // slskd answered (logged in or not, or asking for an API key); the Soulseek page needs it.
    readonly property bool slskdDetected: SlskdController.status !== SlskdController.Offline
                                          && SlskdController.status !== SlskdController.Checking
    // slskd went away while its page was open: back to the library.
    onSlskdDetectedChanged: if (!slskdDetected && page === 3) page = 0

    // ---- Panel layout (test): every panel is a rectangle measured in grid cells (kGridCols x kGridRows
    // over the area right of the sidebar). Values may be fractional: arranging starts from the exact
    // classic (exe) layout, so nothing moves or grows until a panel is dragged. Dragging snaps edges to
    // the grid lines and to the other panels' edges. Overlaps are refused. Remembered.
    readonly property int kGridCols: 48
    readonly property int kGridRows: 44
    property bool editLayout: false
    // id: [column, row, width, height] in cells. Matches the classic layout: pages + seek bar on the
    // left, now playing, rating, volume and controls in a side column.
    readonly property var defaultPanelRects: ({
        pages: [0, 0, 35, 39], seek: [0, 39, 35, 5],
        nowPlaying: [35, 0, 13, 17], lyrics: [35, 17, 13, 14], rating: [35, 31, 13, 4], volume: [35, 35, 13, 4], controls: [35, 39, 13, 5]
    })
    // Smallest size per panel, in pixels: small, so any size can be picked (content is clipped).
    readonly property var panelMinPixels: ({
        pages: [40, 36], seek: [40, 36], nowPlaying: [40, 36], lyrics: [40, 36], rating: [40, 36],
        volume: [40, 36], controls: [40, 36]
    })
    readonly property var panelNames: ({
        pages: qsTr("Pages"), seek: qsTr("Seek bar"), nowPlaying: qsTr("Now playing"), lyrics: qsTr("Lyrics"),
        rating: qsTr("Rating"), volume: qsTr("Volume"), controls: qsTr("Playback controls")
    })
    property var panelRects: defaultPanelRects
    // Panels hidden while arranging: id -> the rect they had (they are not in panelRects meanwhile).
    // Shown again from the sidebar while arranging.
    property var hiddenPanels: ({})

    // The lyrics panel, shown or hidden with the lyrics button on the now-playing panel. Remembered.
    property alias lyricsVisible: nowPlayingSettings.lyricsVisible

    Settings {
        id: nowPlayingSettings

        category: "nowPlaying"
        property bool lyricsVisible: true
    }

    // Lyrics nobody can see don't follow playback: panel hidden, or window in the tray / minimized.
    Binding {
        target: PlayerController
        property: "lyricsShown"
        value: root.lyricsVisible && root.visibility !== Window.Hidden && root.visibility !== Window.Minimized
    }

    Settings {
        id: layoutSettings

        category: "layout"
        property string grid: ""
        property string gridSize: ""
        property string hidden: ""
    }

    // Fractional cells: compare with a little slack so touching edges never count as overlapping.
    readonly property real kEps: 0.001

    function overlaps(a, b) {
        return a[0] < b[0] + b[2] - kEps && b[0] < a[0] + a[2] - kEps
            && a[1] < b[1] + b[3] - kEps && b[1] < a[1] + a[3] - kEps
    }

    // Inside the grid and overlapping nothing.
    function placeable(id, rect, rects) {
        if (rect[0] < -kEps || rect[1] < -kEps || rect[2] <= 0 || rect[3] <= 0
            || rect[0] + rect[2] > kGridCols + kEps || rect[1] + rect[3] > kGridRows + kEps)
            return false
        for (const other in rects) {
            if (other !== id && overlaps(rect, rects[other]))
                return false
        }
        return true
    }

    // A rectangle `id` may take while arranging: placeable, and not smaller than its minimum.
    function fits(id, rect, rects) {
        const min = panelMinPixels[id]
        return placeable(id, rect, rects)
            && rect[2] * panelGrid.cellWidth - panelGrid.gap >= min[0] - 0.5
            && rect[3] * panelGrid.cellHeight - panelGrid.gap >= min[1] - 0.5
    }

    // A stored layout must place every panel (sizes are not checked: the window may be smaller now).
    function validRects(rects) {
        if (rects === null || typeof rects !== "object")
            return false
        for (const id in defaultPanelRects) {
            if (id in hiddenPanels)
                continue
            const r = rects[id]
            if (!Array.isArray(r) || r.length < 4 || r.length > 6 || r.some(v => typeof v !== "number" || !isFinite(v))
                || !placeable(id, r, rects))
                return false
        }
        return true
    }

    // Snaps a value (in cells) to the nearest guide within half a cell, else to the nearest grid line.
    function snapTo(value, guides) {
        let best = Math.round(value)
        let distance = 0.5
        for (const g of guides) {
            if (Math.abs(g - value) < distance) {
                distance = Math.abs(g - value)
                best = g
            }
        }
        return best
    }

    // Edges of the other panels along one axis (0 = x, 1 = y): where a dragged edge may line up.
    function edgeGuides(id, axis) {
        const guides = []
        for (const other in panelRects) {
            if (other === id)
                continue
            const r = panelRects[other]
            guides.push(r[axis], r[axis] + r[axis + 2])
        }
        guides.push(0, axis === 0 ? kGridCols : kGridRows)
        return guides
    }

    // Moving by (dx, dy) cells: the left/top or right/bottom edge snaps, whichever is closer to a guide.
    function snapMove(id, rect, dx, dy) {
        const out = rect.slice()
        for (const axis of [0, 1]) {
            const start = rect[axis] + (axis === 0 ? dx : dy)
            const size = rect[axis + 2]
            const guides = edgeGuides(id, axis)
            const a = snapTo(start, guides)
            const b = snapTo(start + size, guides) - size
            out[axis] = Math.abs(a - start) <= Math.abs(b - start) ? a : b
        }
        return out
    }

    // Resizing by (dx, dy) cells: the right and bottom edges snap.
    function snapResize(id, rect, dx, dy) {
        return [rect[0], rect[1],
                snapTo(rect[0] + rect[2] + dx, edgeGuides(id, 0)) - rect[0],
                snapTo(rect[1] + rect[3] + dy, edgeGuides(id, 1)) - rect[1]]
    }

    // Cells plus the fixed sizes in pixels (see fixedPixels): the size the panel has now, as placed.
    function withFixed(id, rect) {
        rect = rect.slice(0, 4)
        rect.push(id in barHeights ? rect[3] * panelGrid.cellHeight - panelGrid.gap : -1,
                  id in sideWidths ? rect[2] * panelGrid.cellWidth - panelGrid.gap : -1)
        return rect
    }

    function saveLayout() {
        layoutSettings.grid = JSON.stringify(panelRects)
        layoutSettings.hidden = JSON.stringify(hiddenPanels)
        // The grid's size at the time, so fixed panels keep their pixel distance from the nearer edge.
        layoutSettings.gridSize = JSON.stringify([panelGrid.width, panelGrid.height])
    }

    function setPanelRect(id, rect) {
        const rects = Object.assign({}, panelRects)
        rects[id] = withFixed(id, rect)
        panelRects = rects
        saveLayout()
    }

    // Dropping a panel onto another one: when the drop overlaps exactly one panel, the two trade places
    // (each takes the other's rectangle). Returns the other panel's id, or "" if they can't swap.
    function swapTarget(id, rect) {
        let target = ""
        for (const other in panelRects) {
            if (other === id || !overlaps(rect, panelRects[other]))
                continue
            if (target !== "")
                return ""
            target = other
        }
        if (target === "")
            return ""
        const rects = Object.assign({}, panelRects)
        rects[id] = panelRects[target]
        rects[target] = panelRects[id]
        return fits(id, rects[id], rects) && fits(target, rects[target], rects) ? target : ""
    }

    function swapPanels(a, b) {
        const rects = Object.assign({}, panelRects)
        rects[a] = withFixed(a, panelRects[b])
        rects[b] = withFixed(b, panelRects[a])
        panelRects = rects
        saveLayout()
    }

    function hidePanel(id) {
        const rects = Object.assign({}, panelRects)
        const hidden = Object.assign({}, hiddenPanels)
        hidden[id] = rects[id]
        delete rects[id]
        hiddenPanels = hidden
        panelRects = rects
        saveLayout()
    }

    // Back where it was; if that space is taken now, the first free spot (largest size first).
    function showPanel(id) {
        let rect = hiddenPanels[id].slice(0, 4)
        if (!fits(id, rect, panelRects)) {
            rect = null
            for (let w = hiddenPanels[id][2], h = hiddenPanels[id][3]; !rect && (w >= 1 || h >= 1);
                 w = Math.floor(w / 2), h = Math.floor(h / 2)) {
                const cw = Math.max(1, Math.ceil(w))
                const ch = Math.max(1, Math.ceil(h))
                for (let y = 0; !rect && y + ch <= kGridRows; ++y) {
                    for (let x = 0; !rect && x + cw <= kGridCols; ++x) {
                        if (fits(id, [x, y, cw, ch], panelRects))
                            rect = [x, y, cw, ch]
                    }
                }
                if (w <= 1 && h <= 1)
                    break
            }
        }
        if (!rect)
            return // no free space: stays hidden until something is moved or hidden
        const hidden = Object.assign({}, hiddenPanels)
        delete hidden[id]
        hiddenPanels = hidden
        setPanelRect(id, rect)
    }

    // When the window size changes, panels keep their size in pixels where it matters (as in the classic
    // layout): bars (seek, controls, rating, volume) their height, every panel but Pages its width.
    // Stored as values 5 and 6 of a rect (-1 = scales with the window). A fixed panel keeps its pixel
    // distance from the window edge it is chained to (directly or through fixed neighbours); a scaling
    // panel's edge that touches a fixed one follows it.
    readonly property var barHeights: ({ seek: 64, controls: 64, rating: 52, volume: 52 })
    // Panels that keep their width (the side-column cards); Pages and the seek bar stretch.
    readonly property var sideWidths: ({ nowPlaying: 1, lyrics: 1, rating: 1, volume: 1, controls: 1 })
    // [width, height] of the panel grid when the layout was last changed.
    readonly property var savedGridSize: {
        try {
            const v = JSON.parse(layoutSettings.gridSize)
            if (Array.isArray(v) && v.length === 2 && v[0] > 0 && v[1] > 0)
                return v
        } catch (e) {
        }
        return [panelGrid.width, panelGrid.height]
    }

    // One axis (0 = horizontal, 1 = vertical) of renderedRects().
    function placeAxis(out, axis) {
        const pos = axis          // index of x / y
        const len = axis + 2      // index of w / h
        const fixedAt = axis === 0 ? 5 : 4
        const cell = axis === 0 ? panelGrid.cellWidth : panelGrid.cellHeight
        const total = axis === 0 ? panelGrid.width : panelGrid.height
        const limit = axis === 0 ? kGridCols : kGridRows
        const g = panelGrid.gap
        const across = (a, b) => axis === 0 ? a[1] < b[1] + b[3] - kEps && b[1] < a[1] + a[3] - kEps
                                            : a[0] < b[0] + b[2] - kEps && b[0] < a[0] + a[2] - kEps
        const fixed = Object.keys(panelRects).filter(id => panelRects[id].length > fixedAt && panelRects[id][fixedAt] > 0)
        // A fixed panel keeps its pixel distance from the window edge it was nearer to when placed, so
        // panels on the same side move together and never run into each other.
        const savedTotal = savedGridSize[axis]
        const savedCell = (savedTotal + g) / limit
        const place = (id) => {
            const c = panelRects[id]
            const size = c[fixedAt]
            const saved = c[pos] * savedCell
            out[id][pos] = saved + size / 2 <= savedTotal / 2 ? saved : total - (savedTotal - saved)
            out[id][len] = size
        }
        for (const id of fixed)
            place(id)
        // Scaling panels: an edge touching a fixed panel follows it.
        for (const id in out) {
            if (fixed.includes(id))
                continue
            const c = panelRects[id]
            let start = out[id][pos]
            let end = start + out[id][len]
            for (const o of fixed) {
                const co = panelRects[o]
                if (!across(co, c))
                    continue
                if (Math.abs(co[pos] - (c[pos] + c[len])) < kEps)
                    end = out[o][pos] - g
                if (Math.abs(co[pos] + co[len] - c[pos]) < kEps)
                    start = out[o][pos] + out[o][len] + g
            }
            out[id][pos] = start
            out[id][len] = Math.max(0, end - start)
        }
    }

    // Second try for an axis when the anchored placement does not fit: panels with a fixed size that sit
    // on a window edge keep it (e.g. the side column); everything between them is fitted, position and
    // size, into the space left in the middle, in proportion (so nothing overlaps).
    function flexAxis(out, axis) {
        const pos = axis
        const len = axis + 2
        const fixedAt = axis === 0 ? 5 : 4
        const total = axis === 0 ? panelGrid.width : panelGrid.height
        const limit = axis === 0 ? kGridCols : kGridRows
        const g = panelGrid.gap
        const savedTotal = savedGridSize[axis]
        const savedCell = (savedTotal + g) / limit
        const isFixed = (c) => c.length > fixedAt && c[fixedAt] > 0
        let leftEnd = 0                 // saved pixels: end of the left edge group, plus gap
        let rightStart = savedTotal + g // saved pixels: start of the right edge group
        const edge = {}
        for (const id in panelRects) {
            const c = panelRects[id]
            if (!isFixed(c))
                continue
            const saved = c[pos] * savedCell
            if (c[pos] < kEps) {
                edge[id] = [0, c[fixedAt]]
                leftEnd = Math.max(leftEnd, c[fixedAt] + g)
            } else if (c[pos] + c[len] > limit - kEps) {
                edge[id] = [total - (savedTotal - saved), c[fixedAt]]
                rightStart = Math.min(rightStart, saved)
            }
        }
        const nowRightStart = total + g - (savedTotal + g - rightStart)
        const k = (nowRightStart - leftEnd) / Math.max(1, rightStart - leftEnd)
        for (const id in out) {
            if (edge[id]) {
                out[id][pos] = edge[id][0]
                out[id][len] = edge[id][1]
                continue
            }
            const c = panelRects[id]
            const start = c[pos] * savedCell
            const span = c[len] * savedCell // includes the gap after it
            out[id][pos] = leftEnd + (start - leftEnd) * k
            out[id][len] = Math.max(0, span * k - g)
        }
    }

    // Pixel rectangles of every panel at the current grid size. Per axis, fixed sizes are kept as long as
    // everything still fits (inside the grid, no overlaps, every panel at least its minimum size); in a
    // window too small for that (e.g. snapped to half the screen) that axis simply scales with the window,
    // each panel kept at its minimum size where possible.
    function renderedRects() {
        const scaled = {}
        for (const id in panelRects) {
            const c = panelRects[id]
            scaled[id] = [c[0] * panelGrid.cellWidth, c[1] * panelGrid.cellHeight,
                          c[2] * panelGrid.cellWidth - panelGrid.gap, c[3] * panelGrid.cellHeight - panelGrid.gap]
        }
        const out = {}
        for (const id in scaled)
            out[id] = scaled[id].slice()
        for (const axis of [0, 1]) {
            const trial = {}
            for (const id in out)
                trial[id] = out[id].slice()
            placeAxis(trial, axis)
            const pos = axis
            const len = axis + 2
            let fitted = axisFits(trial, axis)
            if (!fitted) {
                for (const id in out)
                    trial[id] = out[id].slice()
                flexAxis(trial, axis)
                fitted = axisFits(trial, axis)
            }
            if (fitted) {
                for (const id in out) {
                    out[id][pos] = trial[id][pos]
                    out[id][len] = trial[id][len]
                }
            } else {
                const total = axis === 0 ? panelGrid.width : panelGrid.height
                for (const id in out) {
                    out[id][len] = Math.max(out[id][len], Math.min(panelMinPixels[id][axis], total - out[id][pos]))
                }
            }
        }
        return out
    }

    // Placed rectangles fit along an axis: inside the grid, no two panels in the same row/column
    // overlapping, none below its minimum size.
    function axisFits(rects, axis) {
        const pos = axis
        const len = axis + 2
        const total = axis === 0 ? panelGrid.width : panelGrid.height
        const ids = Object.keys(rects)
        for (const id of ids) {
            const r = rects[id]
            if (r[pos] < -0.5 || r[pos] + r[len] > total + 0.5 || r[len] < panelMinPixels[id][axis] - 0.5)
                return false
        }
        for (let i = 0; i < ids.length; ++i) {
            for (let j = i + 1; j < ids.length; ++j) {
                const a = panelRects[ids[i]]
                const b = panelRects[ids[j]]
                const across = axis === 0 ? a[1] < b[1] + b[3] - kEps && b[1] < a[1] + a[3] - kEps
                                          : a[0] < b[0] + b[2] - kEps && b[0] < a[0] + a[2] - kEps
                const ra = rects[ids[i]]
                const rb = rects[ids[j]]
                if (across && ra[pos] < rb[pos] + rb[len] - 0.5 && rb[pos] < ra[pos] + ra[len] - 0.5)
                    return false
            }
        }
        return true
    }
    readonly property var panelPixels: panelGrid.width >= 0 && panelGrid.height >= 0 && savedGridSize && !classicLayout
                                       ? renderedRects() : ({})

    // ---- Classic layout: the default until a panel is moved, pixel for pixel as before the grid:
    // pages + seek bar on the left; a side column (240-300 px) with now playing on top, lyrics under
    // it, then rating (52 px), volume (52 px) and controls (64 px, level with the seek bar).
    // The arranged layout is for the Library page only; every other page keeps the classic layout.
    readonly property bool classicLayout: layoutSettings.grid === "" || page !== 0
    onPageChanged: if (page !== 0) editLayout = false
    Connections {
        target: Theme
        function onArrangePanelsChanged() { if (!Theme.arrangePanels) root.editLayout = false }
    }
    readonly property real classicSideWidth: Math.round(Math.max(240, Math.min(300, root.width / Theme.uiScale * 0.27)))
    // Cover size of the classic now-playing card: 45 % of the side column's free height with lyrics,
    // 70 % without. Also the now-playing text scale in every layout.
    readonly property real classicCoverSide: Math.max(0, Math.min(classicSideWidth - 32,
                                                                  (panelGrid.height - 64 - 52 - 52 - 3 * panelGrid.gap)
                                                                  * (lyricsVisible ? 0.45 : 0.7)))

    // [x, y, width, height] in pixels inside the panel grid.
    function classicRect(id) {
        const W = panelGrid.width
        const H = panelGrid.height
        const g = panelGrid.gap
        const sw = classicSideWidth
        const sx = W - sw
        const controlsY = H - 64
        const volumeY = controlsY - g - 52
        const ratingY = volumeY - g - 52
        const npHeight = nowPlayingCard.implicitHeight
        const npY = lyricsVisible ? 0 : ratingY - g - npHeight
        switch (id) {
        case "pages": return [0, 0, sx - g, controlsY - g]
        case "seek": return [0, controlsY, sx - g, 64]
        case "controls": return [sx, controlsY, sw, 64]
        case "volume": return [sx, volumeY, sw, 52]
        case "rating": return [sx, ratingY, sw, 52]
        case "nowPlaying": return [sx, npY, sw, npHeight]
        case "lyrics": return [sx, npHeight + g, sw, Math.max(0, ratingY - g - npHeight - g)]
        }
        return [0, 0, 0, 0]
    }

    // Pixel rectangles (as on screen) back to cells at the current grid size, with their fixed sizes.
    function cellsFromPixels(pixelOf) {
        const rects = {}
        const cw = panelGrid.cellWidth
        const ch = panelGrid.cellHeight
        const g = panelGrid.gap
        for (const id in defaultPanelRects) {
            if (id in hiddenPanels)
                continue
            const r = pixelOf(id)
            rects[id] = [r[0] / cw, r[1] / ch, (r[2] + g) / cw, (r[3] + g) / ch,
                         id in barHeights ? r[3] : -1, id in sideWidths ? r[2] : -1]
        }
        return rects
    }

    // The classic layout in (fractional) cells: exactly the same pixels, the starting point for arranging.
    function cellsFromClassic() {
        return cellsFromPixels(classicRect)
    }

    // Arranging starts from what is on screen, re-measured at the current window size (the layout may
    // have been made at another size), so stored cells and pixels agree while dragging.
    onEditLayoutChanged: {
        if (!editLayout)
            return
        if (classicLayout) {
            panelRects = cellsFromClassic()
        } else {
            const px = renderedRects()
            panelRects = cellsFromPixels(id => px[id])
            saveLayout()
        }
    }

    function resetPanelRects() {
        panelRects = defaultPanelRects
        hiddenPanels = {}
        layoutSettings.grid = ""
        layoutSettings.gridSize = ""
        layoutSettings.hidden = ""
    }

    Component.onCompleted: {
        let stored = null
        try {
            stored = JSON.parse(layoutSettings.grid)
            const hidden = layoutSettings.hidden !== "" ? JSON.parse(layoutSettings.hidden) : {}
            if (hidden && typeof hidden === "object")
                hiddenPanels = hidden
        } catch (e) {
        }
        // Layouts saved before lyrics had a panel of their own: lyrics take the lower part of now playing.
        if (stored && stored.nowPlaying && !stored.lyrics && !("lyrics" in hiddenPanels)) {
            const r = stored.nowPlaying
            const top = Math.round(r[3] * 0.55)
            stored.nowPlaying = [r[0], r[1], r[2], top]
            stored.lyrics = [r[0], r[1] + top, r[2], r[3] - top]
        }
        if (validRects(stored)) {
            // Older layouts: fixed sizes from the classic layout.
            for (const id in stored) {
                const r = stored[id]
                if (r.length === 4)
                    r.push(id in barHeights ? barHeights[id] : -1)
                if (r.length === 5)
                    r.push(id in sideWidths ? classicSideWidth : -1)
            }
            panelRects = stored
        }
    }

    // Sidebar entry: icon only; the name is the tooltip and the accessible name.
    // A page of the StackLayout that is only created once it is first shown.
    component LazyPage: Loader {
        id: lazy

        required property int pageIndex

        active: false
        Connections {
            target: root
            function onPageChanged() {
                if (root.page === lazy.pageIndex)
                    lazy.active = true
            }
        }
    }

    // One panel on the grid. In edit mode an overlay covers it: drag it to move the panel, drag the
    // corner handle to resize it. While dragging, an outline shows where it would go: accent if it
    // fits, red if it would overlap another panel or leave the grid (then nothing changes).
    component PanelSlot: Item {
        id: slot

        required property string panelId
        default property alias content: holder.data
        readonly property var rect: root.panelRects[panelId]
        // Proposed rectangle while dragging, or null.
        property var preview: null
        readonly property bool previewFits: preview !== null && root.fits(panelId, preview, root.panelRects)
        // Moving onto exactly one other panel trades places with it.
        readonly property string previewSwap: preview !== null && !previewFits && !resizeArea.pressed
                                              ? root.swapTarget(panelId, preview) : ""

        visible: !(panelId in root.hiddenPanels)

        // The classic layout until a panel has been moved, then the grid.
        readonly property var classic: root.classicLayout ? root.classicRect(panelId) : null
        readonly property var placed: classic ? classic : (root.panelPixels[panelId] || [0, 0, 0, 0])

        x: placed[0]
        y: placed[1]
        width: placed[2]
        height: placed[3]
        z: preview !== null ? 10 : 0

        // Clipped: a panel made smaller than its content's minimum cuts it off at its own edge
        // instead of drawing over its neighbours.
        Item {
            id: holder

            anchors.fill: parent
            clip: true
        }

        // Where the panel would land.
        Rectangle {
            visible: slot.preview !== null
            parent: panelGrid
            z: 20
            x: slot.preview ? slot.preview[0] * panelGrid.cellWidth : 0
            y: slot.preview ? slot.preview[1] * panelGrid.cellHeight : 0
            width: slot.preview ? slot.preview[2] * panelGrid.cellWidth - panelGrid.gap : 0
            height: slot.preview ? slot.preview[3] * panelGrid.cellHeight - panelGrid.gap : 0
            radius: Theme.radius
            color: "transparent"
            border.width: 3
            border.color: slot.previewFits || slot.previewSwap !== "" ? Theme.accent
                                                                      : Material.color(Material.Red, Material.Shade400)
        }

        Rectangle {
            anchors.fill: parent
            visible: root.editLayout
            radius: Theme.radius
            color: Qt.rgba(0, 0, 0, slot.preview !== null ? 0.7 : 0.45)
            border.width: 2
            border.color: Qt.rgba(1, 1, 1, 0.35)

            Label {
                anchors.centerIn: parent
                text: "⠿  " + root.panelNames[slot.panelId]
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            // Move: the whole panel. Takes every click while arranging.
            MouseArea {
                property point start

                anchors.fill: parent
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: (mouse) => start = mapToItem(panelGrid, mouse.x, mouse.y)
                onPositionChanged: (mouse) => {
                    const p = mapToItem(panelGrid, mouse.x, mouse.y)
                    slot.preview = root.snapMove(slot.panelId, slot.rect, (p.x - start.x) / panelGrid.cellWidth,
                                                 (p.y - start.y) / panelGrid.cellHeight)
                }
                onReleased: {
                    if (slot.previewFits)
                        root.setPanelRect(slot.panelId, slot.preview)
                    else if (slot.previewSwap !== "")
                        root.swapPanels(slot.panelId, slot.previewSwap)
                    slot.preview = null
                }
            }

            // Hide: shown again from the sidebar while arranging.
            IconButton {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                iconName: "close"
                iconSize: 16
                tip: qsTr("Hide panel")
                onClicked: root.hidePanel(slot.panelId)
            }

            // Resize: the bottom-right corner.
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                width: 22
                height: 22
                radius: 6
                color: resizeArea.containsMouse || resizeArea.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.35)

                Label {
                    anchors.centerIn: parent
                    text: "⤡"
                    font.pixelSize: 14
                    color: Theme.window
                }

                MouseArea {
                    id: resizeArea

                    property point start

                    anchors.fill: parent
                    anchors.margins: -6 // easier to grab
                    hoverEnabled: true
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: (mouse) => start = mapToItem(panelGrid, mouse.x, mouse.y)
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const p = mapToItem(panelGrid, mouse.x, mouse.y)
                        slot.preview = root.snapResize(slot.panelId, slot.rect, (p.x - start.x) / panelGrid.cellWidth,
                                                       (p.y - start.y) / panelGrid.cellHeight)
                    }
                    onReleased: {
                        if (slot.previewFits)
                            root.setPanelRect(slot.panelId, slot.preview)
                        slot.preview = null
                    }
                }
            }
        }
    }

    component NavButton: ItemDelegate {
        id: navItem

        property int page: -1 // -1: not a page button (e.g. the layout toggle)

        Layout.fillWidth: true
        Layout.preferredHeight: width
        display: AbstractButton.IconOnly
        icon.width: 24
        icon.height: 24
        icon.color: highlighted ? Theme.accent : Material.foreground
        highlighted: page >= 0 && root.page === page
        onClicked: if (page >= 0) root.page = page

        background: Rectangle {
            radius: Math.min(Theme.radius, 12)
            color: navItem.highlighted ? Theme.selected
                 : navItem.hovered ? Theme.hover
                 : "transparent"

            Behavior on color {
                ColorAnimation { duration: 120 }
            }
        }
    }

    width: 1200
    height: 760
    minimumWidth: 800
    minimumHeight: 500
    visible: true
    title: Qt.application.displayName
    // Own title bar (TitleBar.qml); the native frame and its resize border are gone.
    flags: Qt.Window | Qt.FramelessWindowHint

    Material.theme: Material.Dark
    Material.accent: Theme.accent
    Material.background: Theme.card // popups, menus, combo box lists
    color: Theme.window

    // Settings → Appearance → Background image: blurred (Theme.backgroundBlur) and darkened behind the
    // see-through panels.
    background: Rectangle {
        color: Theme.window

        // A video (webm, mp4) plays muted on a loop; anything else is a still picture. Both come
        // already blurred and darkened from C++ (core/WallpaperBlur), so resizing only stretches them.
        readonly property bool video: /\.(webm|mp4)$/i.test(Theme.backgroundImage.toString())

        Image {
            anchors.fill: parent
            visible: !parent.video && Theme.backgroundImage !== ""
            source: visible ? "image://wallpaper/" + Math.round(Theme.backgroundBlur * 1000) + "/"
                              + encodeURIComponent(Theme.backgroundImage) : ""
            fillMode: Theme.backgroundFill
            asynchronous: true
            retainWhileLoading: true // no flash while the Blur slider moves
            cache: false
        }

        WallpaperVideo {
            anchors.fill: parent
            visible: parent.video
            source: parent.video ? Theme.backgroundImage : ""
            fillMode: Theme.backgroundFill
            blur: Theme.backgroundBlur
            // No decoding while nobody can see it.
            active: root.visible && root.visibility !== Window.Minimized
        }
    }

    // Opens the shared right-click menu for these tracks (one track, or an album's tracks).
    // Pages reach it through ApplicationWindow.window.openTrackMenu().
    function openTrackMenu(fileUrls, isAlbum) {
        trackMenu.urls = fileUrls
        trackMenu.isAlbum = isAlbum === true
        trackMenu.popup()
    }

    function showAlbumOf(fileUrl, highlightTrack) {
        root.page = 0
        libraryPage.openAlbumOf(fileUrl, highlightTrack === true)
    }

    function showArtist(name) {
        root.page = 0
        libraryPage.showArtist(name)
    }

    // Keyboard: Space toggles play/pause (text fields keep their spaces: a focused field takes
    // the key before the shortcut). Media keys work here too when another program owns them
    // globally (see MediaKeys.cpp).
    Shortcut {
        sequence: "Space"
        onActivated: PlayerController.togglePlay()
    }
    Shortcut {
        sequences: ["Media Play", "Media Pause", "Toggle Media Play/Pause"]
        onActivated: PlayerController.togglePlay()
    }
    Shortcut {
        sequence: "Media Next"
        onActivated: PlayerController.next()
    }
    Shortcut {
        sequence: "Media Previous"
        onActivated: PlayerController.previous()
    }

    TrackContextMenu {
        id: trackMenu

        onViewAlbumRequested: (fileUrl) => root.showAlbumOf(fileUrl)
        onNewPlaylistRequested: (fileUrls) => newPlaylistWindow.openWith(fileUrls)
        onDeleteAlbumRequested: (fileUrls) => deleteAlbumDialog.ask(fileUrls)
        onGainRequested: (fileUrls) => gainDialog.ask(fileUrls)
        onEditInfoRequested: (fileUrls) => tagDialog.ask(fileUrls)
    }

    // Edits the files' tags (LibraryController.tagsOf / writeTags). With several files, fields
    // that differ show "Several values" and only fields that are typed into get written.
    Dialog {
        id: tagDialog

        property var urls: []
        // Every song the dialog was opened for (an album). The arrows step from all of them (view -1)
        // through each song on its own; what is typed in each view is kept and all written on Save.
        property var allUrls: []
        property var songTitles: []
        property int view: -1
        property var pending: ({}) // view -> {fields, coverAction, newCover}
        property var original: ({})
        property int coverAction: 0 // 0 keep, 1 replace, 2 remove
        property url newCover
        property string failure
        readonly property bool several: urls.length > 1
        readonly property var keys: ["title", "artist", "albumArtist", "album", "genre", "year", "track", "disc", "composer", "label"]
        readonly property var labels: [qsTr("Title"), qsTr("Artist"), qsTr("Album artist"), qsTr("Album"),
                                       qsTr("Genre"), qsTr("Year"), qsTr("Track number"), qsTr("Disc number"),
                                       qsTr("Composer"),
                                       qsTr("Label")]

        function ask(fileUrls) {
            allUrls = fileUrls
            const titles = []
            if (fileUrls.length > 1) {
                for (const url of fileUrls) {
                    const tags = LibraryController.tagsOf([url])
                    titles.push(tags.title || decodeURIComponent(url.toString().split("/").pop()))
                }
            }
            songTitles = titles
            pending = {}
            failure = ""
            view = -1
            load()
            open()
        }

        // Fields typed into in the current view, and its cover change.
        function stash() {
            const fields = {}
            for (let i = 0; i < tagFields.count; ++i) {
                const field = tagFields.itemAt(i)
                if (field.edited)
                    fields[field.key] = field.text
            }
            const all = Object.assign({}, pending)
            all[view] = {fields: fields, coverAction: coverAction, newCover: newCover}
            pending = all
        }

        // Shows the current view: its songs' tags, with what was typed there before.
        function load() {
            urls = view < 0 ? allUrls : [allUrls[view]]
            original = LibraryController.tagsOf(urls)
            const kept = pending[view]
            coverAction = kept ? kept.coverAction : 0
            newCover = kept ? kept.newCover : ""
            for (let i = 0; i < tagFields.count; ++i) {
                const field = tagFields.itemAt(i)
                field.reset()
                if (kept && field.key in kept.fields) {
                    field.text = kept.fields[field.key]
                    field.edited = true
                }
            }
        }

        function step(by) {
            stash()
            view = Math.max(-1, Math.min(allUrls.length - 1, view + by))
            load()
        }

        anchors.centerIn: Overlay.overlay
        width: Math.min(560, parent.width - 40)
        modal: true
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }
        title: several ? qsTr("Edit info (%1 tracks)").arg(urls.length) : qsTr("Edit info")
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            stash()
            // All songs first, then each song's own changes over them.
            const views = Object.keys(pending).map(Number).sort((x, y) => x - y)
            failure = ""
            for (const v of views) {
                const change = pending[v]
                if (Object.keys(change.fields).length === 0 && change.coverAction === 0)
                    continue
                const error = LibraryController.writeTags(v < 0 ? allUrls : [allUrls[v]], change.fields,
                                                          change.coverAction, change.newCover)
                if (error !== "")
                    failure = error
            }
            pending = {}
            if (failure !== "") {
                load()
                open() // stay open with the message
            }
        }

        FileDialog {
            id: coverPicker

            title: qsTr("Choose a cover picture")
            nameFilters: [qsTr("Images (*.jpg *.jpeg *.png *.webp *.bmp)")]
            currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
            onAccepted: {
                tagDialog.newCover = selectedFile
                tagDialog.coverAction = 1
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            // Album: arrows step from all songs to each song on its own and back.
            RowLayout {
                Layout.fillWidth: true
                visible: tagDialog.allUrls.length > 1
                spacing: 4

                component StepButton: AbstractButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    opacity: enabled ? 1 : 0.3
                    background: Item {}
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                StepButton {
                    text: "‹"
                    enabled: tagDialog.view > -1
                    onClicked: tagDialog.step(-1)
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    text: tagDialog.view < 0 ? qsTr("All %1 songs").arg(tagDialog.allUrls.length)
                          : qsTr("%1 / %2  ·  %3").arg(tagDialog.view + 1).arg(tagDialog.allUrls.length)
                                                     .arg(tagDialog.songTitles[tagDialog.view] ?? "")
                }
                StepButton {
                    text: "›"
                    enabled: tagDialog.view < tagDialog.allUrls.length - 1
                    onClicked: tagDialog.step(1)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                // Cover: current, newly picked, or none.
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: false
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 120
                        radius: 6
                        color: Theme.subtle
                        clip: true

                        Image {
                            id: tagCover

                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                            source: tagDialog.coverAction === 1 ? tagDialog.newCover
                                  : tagDialog.coverAction === 0 && tagDialog.original.hasCover === true
                                    ? tagDialog.original.coverUrl : ""
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: tagCover.status !== Image.Ready
                            text: qsTr("No cover")
                            opacity: 0.5
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 0

                        Button {
                            flat: true
                            leftPadding: 6
                            rightPadding: 6
                            font.pixelSize: 12
                            text: qsTr("Change…")
                            onClicked: coverPicker.open()
                        }
                        Button {
                            flat: true
                            leftPadding: 6
                            rightPadding: 6
                            font.pixelSize: 12
                            text: qsTr("Remove")
                            enabled: tagDialog.coverAction === 1 || (tagDialog.coverAction === 0 && tagDialog.original.hasCover === true)
                            onClicked: tagDialog.coverAction = 2
                        }
                    }
                }

                // One row per field: small label, slim outlined box.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    Repeater {
                        id: tagFields

                        model: tagDialog.keys.length

                        delegate: RowLayout {
                            id: row

                            required property int index
                            readonly property string key: tagDialog.keys[index]
                            readonly property bool mixed: (tagDialog.original.mixed || []).includes(key)
                            property alias text: field.text
                            property bool edited: false

                            function reset() {
                                field.text = tagDialog.original[key] || ""
                                edited = false
                            }

                            Layout.fillWidth: true
                            spacing: 10
                            // A title or track number is one song's own.
                            enabled: !(tagDialog.several && (key === "title" || key === "track"))

                            Label {
                                Layout.preferredWidth: 84
                                text: tagDialog.labels[row.index]
                                opacity: 0.6
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            TextField {
                                id: field

                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Material.containerStyle: Material.Outlined
                                topPadding: 0
                                bottomPadding: 0
                                leftPadding: 10
                                font.pixelSize: 13
                                selectByMouse: true
                                placeholderText: row.mixed ? qsTr("Several values") : ""
                                inputMethodHints: row.key === "year" || row.key === "track" || row.key === "disc" ? Qt.ImhDigitsOnly : Qt.ImhNone
                                validator: RegularExpressionValidator {
                                    regularExpression: row.key === "year" || row.key === "track" || row.key === "disc" ? /\d{0,4}/ : /.*/
                                }
                                onTextEdited: row.edited = true
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: tagDialog.failure !== ""
                        text: tagDialog.failure
                        color: Theme.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // A song's own volume in dB (LibraryController.setReplayGainDb); the file's ReplayGain tag is shown as a hint.
    Dialog {
        id: gainDialog

        property var urls: []
        property real tagDb: NaN

        function ask(fileUrls) {
            urls = fileUrls
            tagDb = LibraryController.tagGainDb(fileUrls[0])
            const own = LibraryController.replayGainDb(fileUrls[0])
            gainSlider.value = !isNaN(own) ? own : 0
            open()
        }

        function dbText(db) {
            return qsTr("%1 dB").arg((db > 0 ? "+" : "") + db.toFixed(1))
        }

        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }
        title: urls.length > 1 ? qsTr("Volume adjustment (%1 tracks)").arg(urls.length) : qsTr("Volume adjustment")
        standardButtons: Dialog.Save | Dialog.Reset | Dialog.Cancel
        onAccepted: LibraryController.setReplayGainDb(urls, gainSlider.value)
        onReset: {
            LibraryController.setReplayGainDb(urls, NaN)
            close()
        }

        ColumnLayout {
            width: parent.width
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Slider {
                    id: gainSlider

                    Layout.fillWidth: true
                    from: -20
                    to: 20
                    stepSize: 0.5
                }

                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    text: gainDialog.dbText(gainSlider.value)
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.6
                font.pixelSize: 12
                text: (isNaN(gainDialog.tagDb) ? "" : qsTr("The file's ReplayGain tag suggests %1.").arg(gainDialog.dbText(gainDialog.tagDb)) + " ")
                      + qsTr("Reset removes the adjustment.")
            }
        }
    }

    // Deleting an album from the disk always asks first.
    Dialog {
        id: deleteAlbumDialog

        property var urls: []
        property string failure

        function ask(fileUrls) {
            urls = fileUrls
            failure = ""
            open()
        }

        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        // Dim the app with black, not Material's light veil.
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }
        title: qsTr("Delete album")
        standardButtons: failure === "" ? Dialog.Yes | Dialog.No : Dialog.Close
        onAccepted: {
            const error = LibraryController.deleteAlbum(urls)
            if (error !== "") {
                failure = error
                open() // show what went wrong
            }
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: deleteAlbumDialog.failure !== ""
                  ? deleteAlbumDialog.failure
                  : (deleteAlbumDialog.urls.length === 1
                     ? qsTr("Delete this album's track from your computer?")
                     : qsTr("Delete this album's %1 tracks from your computer?").arg(deleteAlbumDialog.urls.length))
                    + " " + qsTr("The files and their folder (cover art included) go to the Recycle Bin.")
        }
    }

    NewPlaylistWindow {
        id: newPlaylistWindow

        onCreated: root.page = 1
    }

    TitleBar {
        id: titleBar

        anchors.left: parent.left
        anchors.right: parent.right
        window: root
    }

    // Lays the UI out at its area / uiScale, then scales it back up to fill it.
    // The title bar stays outside so window buttons keep their size.
    Item {
        id: zoomArea

        anchors.fill: parent
        anchors.topMargin: titleBar.height

        Item {
            width: zoomArea.width / Theme.uiScale
            height: zoomArea.height / Theme.uiScale
            scale: Theme.uiScale
            transformOrigin: Item.TopLeft

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 0
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Card {
                        Layout.preferredWidth: 72
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4

                            // While an album or artist is open it is the way back (same as Escape): album to
                            // the grid it came from, artist to the whole library.
                            NavButton {
                                readonly property bool back: root.page === 0 && (libraryPage.openAlbum >= 0 || libraryPage.artistMode)

                                page: 0
                                text: back ? qsTr("Back") : qsTr("Library")
                                icon.source: back ? "icons/back.svg" : "icons/library.svg"
                                onClicked: if (back) libraryPage.goBack()
                            }
                            NavButton { page: 1; text: qsTr("Playlists"); icon.source: "icons/playlist.svg" }
                            NavButton { page: 2; text: qsTr("Statistics"); icon.source: "icons/statistics.svg" }
                            // Greyed out until slskd answers (it may still be starting, or not installed).
                            NavButton {
                                page: 3
                                text: qsTr("Soulseek")
                                icon.source: "icons/soulseek.svg"
                                enabled: root.slskdDetected
                                opacity: enabled ? 1 : 0.3
                            }

                            Item { Layout.fillHeight: true }

                            // Layout editing (test): drag the panels around while this is on.
                            NavButton {
                                // Settings → Experimental; arranging applies to the Library page only.
                                visible: Theme.arrangePanels && root.page === 0
                                text: root.editLayout ? qsTr("Done arranging panels") : qsTr("Arrange panels")
                                icon.source: "icons/dashboard.svg"
                                highlighted: root.editLayout
                                onClicked: root.editLayout = !root.editLayout
                            }

                            // Hidden panels, to show again.
                            NavButton {
                                visible: root.editLayout && Object.keys(root.hiddenPanels).length > 0
                                text: qsTr("Show hidden panel")
                                icon.source: "icons/add.svg"
                                highlighted: hiddenMenu.visible
                                onClicked: hiddenMenu.popup(width + 4, 0)

                                CompactMenu {
                                    id: hiddenMenu

                                    Instantiator {
                                        model: Object.keys(root.hiddenPanels)

                                        delegate: CompactMenuItem {
                                            required property string modelData

                                            text: root.panelNames[modelData]
                                            onTriggered: root.showPanel(modelData)
                                        }

                                        onObjectAdded: (index, item) => hiddenMenu.insertItem(index, item)
                                        onObjectRemoved: (index, item) => hiddenMenu.removeItem(item)
                                    }
                                }
                            }

                            NavButton {
                                visible: root.editLayout
                                text: qsTr("Reset panel layout")
                                icon.source: "icons/refresh.svg"
                                onClicked: root.resetPanelRects()
                            }

                            // Output device; the list is read when the menu opens (devices come and go).
                            NavButton {
                                id: deviceButton

                                property var devices: []

                                text: PlayerController.outputDevice !== "" ? PlayerController.outputDevice
                                                                           : qsTr("Output device: system default")
                                icon.source: "icons/speaker.svg"
                                highlighted: deviceMenu.visible
                                onClicked: {
                                    devices = PlayerController.outputDevices()
                                    deviceMenu.popup(width + 4, 0)
                                }

                                CompactMenu {
                                    id: deviceMenu

                                    // As wide as the longest device name (within the window).
                                    implicitWidth: {
                                        let widest = 180
                                        for (let i = 0; i < count; ++i)
                                            widest = Math.max(widest, itemAt(i)?.implicitWidth ?? 0)
                                        return Math.min(widest + leftPadding + rightPadding, root.width - 120)
                                    }

                                    CompactMenuItem {
                                        text: qsTr("System default")
                                        checkable: true
                                        checked: PlayerController.outputDevice === ""
                                        onTriggered: PlayerController.outputDevice = ""
                                    }

                                    Instantiator {
                                        model: deviceButton.devices

                                        delegate: CompactMenuItem {
                                            required property string modelData

                                            text: modelData
                                            checkable: true
                                            checked: PlayerController.outputDevice === modelData
                                            onTriggered: PlayerController.outputDevice = modelData
                                        }

                                        onObjectAdded: (index, item) => deviceMenu.insertItem(index + 1, item)
                                        onObjectRemoved: (index, item) => deviceMenu.removeItem(item)
                                    }
                                }
                            }

                            NavButton { page: 4; text: qsTr("Settings"); icon.source: "icons/settings.svg" }
                        }
                    }

                    // The panel grid (see panelRects). Cell sizes include the 12 px gap between panels.
                    Item {
                        id: panelGrid

                        readonly property real gap: 12
                        readonly property real cellWidth: (width + gap) / root.kGridCols
                        readonly property real cellHeight: (height + gap) / root.kGridRows

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 0

                        // Grid lines while arranging, so the cells are visible.
                        Repeater {
                            model: root.editLayout ? root.kGridCols * root.kGridRows : 0

                            delegate: Rectangle {
                                required property int index

                                x: (index % root.kGridCols) * panelGrid.cellWidth
                                y: Math.floor(index / root.kGridCols) * panelGrid.cellHeight
                                width: panelGrid.cellWidth - 2
                                height: panelGrid.cellHeight - 2
                                radius: 2
                                color: Qt.rgba(1, 1, 1, 0.05)
                            }
                        }

                        PanelSlot {
                            id: pagesSlot

                            panelId: "pages"

                            StackLayout {
                                anchors.fill: parent
                                currentIndex: root.page

                                LibraryPage { id: libraryPage }

                                // The other pages are built the first time they are opened, not at
                                // startup, then kept (so they keep their scroll position and state).
                                LazyPage {
                                    pageIndex: 1
                                    sourceComponent: PlaylistsPage {
                                        onNewPlaylistRequested: newPlaylistWindow.openWith([])
                                    }
                                }

                                LazyPage {
                                    pageIndex: 2
                                    sourceComponent: StatisticsPage {}
                                }

                                LazyPage {
                                    pageIndex: 3
                                    sourceComponent: SoulseekPage {}
                                }

                                LazyPage {
                                    pageIndex: 4
                                    sourceComponent: SettingsPage {}
                                }
                            }
                        }

                        PanelSlot {
                            id: seekSlot

                            panelId: "seek"

                            SeekBar {
                                anchors.fill: parent
                            }
                        }

                        PanelSlot {
                            id: nowPlayingSlot

                            panelId: "nowPlaying"

                            NowPlayingPanel {
                                id: nowPlayingCard

                                anchors.fill: parent
                                // Classic layout: the cover as the side column sized it (45 % of its
                                // height with lyrics, 70 % without), and the panel fits around it.
                                fixedCoverSide: root.classicLayout ? root.classicCoverSide : -1
                                textScale: root.classicCoverSide
                                onAlbumClicked: {
                                    root.showAlbumOf(PlayerController.url)
                                }
                                onArtistClicked: root.showArtist(PlayerController.artist)
                            }
                        }

                        // Hidden with the lyrics button (still shown while arranging, so it can be moved).
                        PanelSlot {
                            id: lyricsSlot

                            panelId: "lyrics"
                            visible: (root.lyricsVisible || root.editLayout) && !("lyrics" in root.hiddenPanels)

                            LyricsPanel {
                                anchors.fill: parent
                            }
                        }

                        PanelSlot {
                            id: ratingSlot

                            panelId: "rating"

                            RatingCard {
                                anchors.fill: parent
                            }
                        }

                        PanelSlot {
                            id: volumeSlot

                            panelId: "volume"

                            VolumeControl {
                                anchors.fill: parent
                            }
                        }

                        PanelSlot {
                            id: controlsSlot

                            panelId: "controls"

                            PlaybackControls {
                                anchors.fill: parent
                            }
                        }
                    }
                }
            }
        }

    }

    // On top of everything so Ctrl+wheel is seen before lists scroll; plain wheel
    // events are not accepted here and pass through to the items below.
    Item {
        anchors.fill: parent
        z: 1000

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            target: null
            // 120 units = one wheel notch = 10 %; touchpads send smaller steps.
            onWheel: (event) => {
                const next = Theme.uiScale + event.angleDelta.y / 1200
                Theme.uiScale = Math.round(Math.min(2.0, Math.max(0.6, next)) * 100) / 100
            }
        }
    }

    // Thin outline so the frameless window separates from what is behind it.
    Rectangle {
        anchors.fill: parent
        z: 998
        visible: root.visibility !== Window.Maximized
        color: "transparent"
        border.color: Theme.subtle
    }

    // Resize border for the frameless window; off when maximized.
    Item {
        anchors.fill: parent
        z: 999
        visible: root.visibility !== Window.Maximized

        readonly property int grip: 6

        ResizeEdge { edges: Qt.LeftEdge; x: 0; y: parent.grip; width: parent.grip; height: parent.height - 2 * parent.grip }
        ResizeEdge { edges: Qt.RightEdge; x: parent.width - parent.grip; y: parent.grip; width: parent.grip; height: parent.height - 2 * parent.grip }
        ResizeEdge { edges: Qt.TopEdge; x: parent.grip; y: 0; width: parent.width - 2 * parent.grip; height: parent.grip }
        ResizeEdge { edges: Qt.BottomEdge; x: parent.grip; y: parent.height - parent.grip; width: parent.width - 2 * parent.grip; height: parent.grip }
        ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: parent.grip; height: parent.grip }
        ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: parent.width - parent.grip; y: 0; width: parent.grip; height: parent.grip }
        ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: parent.height - parent.grip; width: parent.grip; height: parent.grip }
        ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: parent.width - parent.grip; y: parent.height - parent.grip; width: parent.grip; height: parent.grip }
    }
}
