# Regenerates resources/app.ico (the exe's icon in Explorer and the taskbar): the white star of
# qml/icons/app.svg, drawn at 16-256 px and stored as PNG entries. Run: powershell -File resources/make_icon.ps1
Add-Type -AssemblyName System.Drawing

# Star points on a 64-unit grid, as in app.svg.
$points = @(32,3, 40.2,21.7, 60.6,23.6, 45.2,37.2, 49.7,57.2, 32,46.7, 14.3,57.2, 18.8,37.2, 3.4,23.6, 23.8,21.7)
$sizes = 16, 24, 32, 48, 64, 128, 256

$images = foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = 'AntiAlias'
    $graphics.PixelOffsetMode = 'HighQuality'
    $scale = $size / 64.0
    $star = for ($i = 0; $i -lt $points.Count; $i += 2) {
        New-Object System.Drawing.PointF ([single]($points[$i] * $scale)), ([single]($points[$i + 1] * $scale))
    }
    $graphics.FillPolygon([System.Drawing.Brushes]::White, [System.Drawing.PointF[]]$star)
    $graphics.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    , $stream.ToArray()
}

# ICO: header, one 16-byte directory entry per image, then the PNG data.
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] } # 0 means 256
    $writer.Write([byte]$dim); $writer.Write([byte]$dim); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'app.ico'), $out.ToArray())
