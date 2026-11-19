# Generates build/flowspace.ico — the FlowSpace logo (two offset rounded squares).
# Pure .NET System.Drawing, no external tools. Writes a proper multi-size ICO:
# PNG-compressed 256x256 plus 32-bit DIB entries for the smaller sizes, so Windows
# has crisp, correctly coloured artwork in the title bar, taskbar and Explorer.
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot '..\build\flowspace.ico'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256

function Draw-RoundedSquare($graphics, $x, $y, $w, $h, $r, $color) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Min($r * 2, [Math]::Min($w, $h))
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush($color)
    $graphics.FillPath($brush, $path)
    $brush.Dispose()
    $path.Dispose()
}

$images = New-Object System.Collections.ArrayList
foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $k = $size / 256.0
    $accent = [System.Drawing.Color]::FromArgb(255, 67, 137, 216)   # #4389D8
    $white = [System.Drawing.Color]::FromArgb(255, 250, 251, 253)

    # Back square (accent), front square (white) offset up-right, accent dot.
    Draw-RoundedSquare $graphics (34 * $k) (54 * $k) (188 * $k) (168 * $k) (22 * $k) $accent
    Draw-RoundedSquare $graphics (64 * $k) (34 * $k) (188 * $k) (162 * $k) (22 * $k) $white
    Draw-RoundedSquare $graphics (164 * $k) (164 * $k) (30 * $k) (30 * $k) (8 * $k) $accent
    $graphics.Dispose()

    if ($size -ge 256) {
        $pngStream = New-Object System.IO.MemoryStream
        $bmp.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
        [void]$images.Add([pscustomobject]@{ Size = $size; Bytes = $pngStream.ToArray() })
        $pngStream.Dispose()
    }
    else {
        $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
        $bits = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = $bits.Stride
        $pixels = New-Object byte[] ($stride * $size)
        [System.Runtime.InteropServices.Marshal]::Copy($bits.Scan0, $pixels, 0, $pixels.Length)
        $bmp.UnlockBits($bits)

        $maskStride = [int]([Math]::Floor(($size + 31) / 32) * 4)
        $xor = New-Object byte[] ($size * $size * 4)
        $and = New-Object byte[] ($maskStride * $size)

        for ($row = 0; $row -lt $size; $row++) {
            $srcRow = ($size - 1 - $row) * $stride
            for ($col = 0; $col -lt $size; $col++) {
                $s = $srcRow + $col * 4
                $d = ($row * $size + $col) * 4
                $xor[$d] = $pixels[$s]
                $xor[$d + 1] = $pixels[$s + 1]
                $xor[$d + 2] = $pixels[$s + 2]
                $xor[$d + 3] = $pixels[$s + 3]
                if ($pixels[$s + 3] -lt 128) {
                    $idx = $row * $maskStride + [Math]::Floor($col / 8)
                    $and[$idx] = $and[$idx] -bor (1 -shl (7 - ($col % 8)))
                }
            }
        }

        $dib = New-Object System.IO.MemoryStream
        $dw = New-Object System.IO.BinaryWriter($dib)
        $dw.Write([UInt32]40)            # biSize
        $dw.Write([Int32]$size)          # biWidth
        $dw.Write([Int32]($size * 2))    # biHeight (XOR + AND)
        $dw.Write([UInt16]1)             # biPlanes
        $dw.Write([UInt16]32)            # biBitCount
        $dw.Write([UInt32]0)             # biCompression
        $dw.Write([UInt32]($xor.Length + $and.Length))
        $dw.Write([Int32]0)              # biXPelsPerMeter
        $dw.Write([Int32]0)              # biYPelsPerMeter
        $dw.Write([UInt32]0)             # biClrUsed
        $dw.Write([UInt32]0)             # biClrImportant
        $dw.Write($xor)
        $dw.Write($and)
        $dw.Flush()
        [void]$images.Add([pscustomobject]@{ Size = $size; Bytes = $dib.ToArray() })
        $dib.Dispose()
    }

    $bmp.Dispose()
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)
$writer.Write([UInt16]0)                  # reserved
$writer.Write([UInt16]1)                  # type: icon
$writer.Write([UInt16]$images.Count)

$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $writer.Write([byte]$dim)
    $writer.Write([byte]$dim)
    $writer.Write([byte]0)                # colour count
    $writer.Write([byte]0)                # reserved
    $writer.Write([UInt16]1)              # planes
    $writer.Write([UInt16]32)             # bit count
    $writer.Write([UInt32]$img.Bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) {
    $writer.Write($img.Bytes)
}
$writer.Flush()

if ($stream.Length -ne $offset) {
    throw "ICO assembly mismatch: stream=$($stream.Length) expected=$offset"
}

[System.IO.File]::WriteAllBytes($out, $stream.ToArray())
$stream.Dispose()
Write-Output "Icon written: $((Resolve-Path $out).Path) ($((Get-Item $out).Length) bytes, $($images.Count) sizes)"
