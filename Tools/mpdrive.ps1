# Drives one of several running copies of the game, picked by index.
# Used to test a multiplayer lobby with two instances on one machine.
param(
    [int]$Index = 0,
    [int]$X = -1,
    [int]$Y = -1,
    [string]$Type = "",
    [string]$Key = "",
    [string]$Shot = "",
    [switch]$Right,
    [int]$SettleMs = 600
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Collections.Generic;using System.Text;using System.Runtime.InteropServices;
public class MpDrive {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr extra);
 [DllImport("user32.dll")] public static extern short VkKeyScanW(char c);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 public struct RECT { public int L, T, R, B; }
 public struct POINT { public int X, Y; }

 public static List<IntPtr> All(string wanted) {
   var found = new List<IntPtr>();
   var pids = new List<uint>();
   EnumWindows(delegate(IntPtr h, IntPtr l) {
     var sb = new StringBuilder(256);
     GetClassNameW(h, sb, sb.Capacity);
     if (sb.ToString() == wanted && IsWindowVisible(h)) {
       uint pid; GetWindowThreadProcessId(h, out pid);
       if (!pids.Contains(pid)) { pids.Add(pid); found.Add(h); }
     }
     return true;
   }, IntPtr.Zero);
   // Stable order, so "instance 0" stays instance 0 between calls.
   found.Sort(delegate(IntPtr a, IntPtr b) { return a.ToInt64().CompareTo(b.ToInt64()); });
   return found;
 }
}
"@

$windows = [MpDrive]::All("WorldOfClansWindow")
if ($windows.Count -le $Index) { Write-Output "no window $Index (found $($windows.Count))"; exit 1 }
$h = $windows[$Index]

[void][MpDrive]::ShowWindow($h, 9)
[void][MpDrive]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043)
[void][MpDrive]::BringWindowToTop($h)
[void][MpDrive]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 350

if ($X -ge 0 -and $Y -ge 0) {
    $p = New-Object MpDrive+POINT
    $p.X = $X; $p.Y = $Y
    [void][MpDrive]::ClientToScreen($h, [ref]$p)
    [void][MpDrive]::SetCursorPos($p.X, $p.Y)
    Start-Sleep -Milliseconds 120
    $down = 0x0002; $up = 0x0004
    if ($Right) { $down = 0x0008; $up = 0x0010 }
    [MpDrive]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [MpDrive]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}

if ($Type -ne "") {
    foreach ($c in $Type.ToCharArray()) {
        $vk = [MpDrive]::VkKeyScanW($c)
        $code = [byte]($vk -band 0xFF)
        $shifted = (($vk -shr 8) -band 1) -ne 0
        if ($shifted) { [MpDrive]::keybd_event(0x10, 0, 0, [IntPtr]::Zero) }
        [MpDrive]::keybd_event($code, 0, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 40
        [MpDrive]::keybd_event($code, 0, 2, [IntPtr]::Zero)
        if ($shifted) { [MpDrive]::keybd_event(0x10, 0, 2, [IntPtr]::Zero) }
        Start-Sleep -Milliseconds 40
    }
}

if ($Key -ne "") {
    $codes = @{ "space" = 0x20; "escape" = 0x1B; "enter" = 0x0D; "backspace" = 0x08 }
    $vk = $codes[$Key.ToLower()]
    if ($vk) {
        [MpDrive]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [MpDrive]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)
    }
}

Start-Sleep -Milliseconds $SettleMs

if ($Shot -ne "") {
    [void][MpDrive]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 500
    $r = New-Object MpDrive+RECT
    [void][MpDrive]::GetClientRect($h, [ref]$r)
    $p = New-Object MpDrive+POINT
    [void][MpDrive]::ClientToScreen($h, [ref]$p)
    $bmp = New-Object System.Drawing.Bitmap($r.R, $r.B)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($p.X, $p.Y, 0, 0, (New-Object System.Drawing.Size($r.R, $r.B)))
    New-Item -ItemType Directory -Force -Path (Split-Path $Shot) | Out-Null
    $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output $Shot
} else {
    Write-Output "ok"
}

[void][MpDrive]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0043)
