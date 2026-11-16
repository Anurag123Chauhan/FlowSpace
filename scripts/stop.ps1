# Gracefully stops a running FlowSpace instance by posting WM_CLOSE to its
# hidden message window (triggers the same path as Tray -> Exit).
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class StopFs {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
    public static List<IntPtr> Matches = new List<IntPtr>();
    public static bool Callback(IntPtr h, IntPtr l) {
        var cls = new StringBuilder(256);
        GetClassNameW(h, cls, 256);
        if (cls.ToString() == "FlowSpaceMessageWindow") { Matches.Add(h); }
        return true;
    }
    public static IntPtr Find() {
        Matches.Clear();
        EnumWindows(Callback, IntPtr.Zero);
        return Matches.Count > 0 ? Matches[0] : IntPtr.Zero;
    }
}
"@
$h = [StopFs]::Find()
if ($h -eq [IntPtr]::Zero) { Write-Output "FlowSpace is not running (message window not found)."; exit 0 }
[void][StopFs]::PostMessageW($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Output ("WM_CLOSE posted to FlowSpace message window 0x" + $h.ToString("X"))
