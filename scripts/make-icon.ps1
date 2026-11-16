# Generates build/flowspace.ico — the FlowSpace logo (two offset rounded squares).
# Pure .NET System.Drawing, no external tools. Single 256x256 image; Windows scales it.
$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot '..\build\flowspace.ico'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

Add-Type -AssemblyName System.Drawing

$size = 256
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

function Draw-RoundedSquare($graphics, $x, $y, $w, $h, $r, $color) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
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

$accent = [System.Drawing.Color]::FromArgb(255, 67, 137, 216)  # #4389D8
$white = [System.Drawing.Color]::FromArgb(255, 240, 242, 245)

# Back square (accent), front square (white) offset up-right.
$inset = 34
Draw-RoundedSquare $g $inset ($inset + 20) ($size - 2 * $inset) ($size - 2 * $inset - 20) 22 $accent
$inset2 = 34
$fw = $size - 2 * $inset2
Draw-RoundedSquare $g ($inset2 + 30) $inset2 $fw ($fw - 26) 22 $white

# Small accent dot on the front square.
Draw-RoundedSquare $g ($size - 92) ($size - 92) 30 30 8 $accent

$g.Dispose()

# Extract as icon and save (Icon.FromHandle writes a valid single-image .ico).
$hicon = $bmp.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($hicon)
$fs = [System.IO.File]::Create($out)
$icon.Save($fs)
$fs.Dispose()
$icon.Dispose()
$bmp.Dispose()

Write-Output "Icon written: $((Resolve-Path $out).Path) ($((Get-Item $out).Length) bytes)"
