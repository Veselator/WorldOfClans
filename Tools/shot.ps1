# Captures the game window's client area to a PNG. Used to check the UI while iterating.
param([string]$Out = "$env:TEMP\woc\shot.png")

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Collections.Generic;using System.Text;using System.Runtime.InteropServices;
public class WinCap {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 public struct RECT { public int L, T, R, B; }
 public struct POINT { public int X, Y; }

 public static IntPtr FindByClass(string wanted) {
   IntPtr found = IntPtr.Zero;
   EnumWindows(delegate(IntPtr h, IntPtr l) {
     var sb = new StringBuilder(256);
     GetClassNameW(h, sb, sb.Capacity);
     if (sb.ToString() == wanted && IsWindowVisible(h)) { found = h; return false; }
     return true;
   }, IntPtr.Zero);
   return found;
 }
}
"@

$h = [WinCap]::FindByClass("WorldOfClansWindow")
if ($h -eq [IntPtr]::Zero) { Write-Output "window not found"; exit 1 }

# HWND_TOPMOST briefly, so the capture is not covered by the terminal.
[WinCap]::ShowWindow($h, 9) | Out-Null
[WinCap]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
[WinCap]::BringWindowToTop($h) | Out-Null
[WinCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 1000

$r = New-Object WinCap+RECT
[WinCap]::GetClientRect($h, [ref]$r) | Out-Null
$p = New-Object WinCap+POINT
[WinCap]::ClientToScreen($h, [ref]$p) | Out-Null

$bmp = New-Object System.Drawing.Bitmap($r.R, $r.B)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($p.X, $p.Y, 0, 0, (New-Object System.Drawing.Size($r.R, $r.B)))
New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

[WinCap]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0043) | Out-Null
Write-Output $Out
