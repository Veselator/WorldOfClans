# Sends a click (or a key) to the game window at client-relative coordinates.
# Used to drive the game while iterating on the interface.
param(
    [int]$X = -1,
    [int]$Y = -1,
    [string]$Key = "",
    [int]$Wheel = 0,
    [int]$SettleMs = 700,
    [int]$HoldMs = 60
)

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class Drive {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr extra);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern short VkKeyScanW(char c);
 public struct POINT { public int X, Y; }
 public static IntPtr Find(string wanted) {
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

$h = [Drive]::Find("WorldOfClansWindow")
if ($h -eq [IntPtr]::Zero) { Write-Output "window not found"; exit 1 }
# Keep the game above the terminal, or synthetic clicks land on whatever is covering it.
[Drive]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
[Drive]::BringWindowToTop($h) | Out-Null
[Drive]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 250

if ($X -ge 0 -and $Y -ge 0) {
    $p = New-Object Drive+POINT
    $p.X = $X; $p.Y = $Y
    [Drive]::ClientToScreen($h, [ref]$p) | Out-Null
    [Drive]::SetCursorPos($p.X, $p.Y) | Out-Null
    Start-Sleep -Milliseconds 120
    if ($Wheel -ne 0) {
        $delta = [int]($Wheel * 120)
        if ($delta -lt 0) { $delta = $delta + 4294967296 }
        [Drive]::mouse_event(0x0800, 0, 0, [uint32]$delta, [IntPtr]::Zero)
    } else {
        [Drive]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # left down
        Start-Sleep -Milliseconds 90
        [Drive]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # left up
    }
}

if ($Key -ne "") {
    $codes = @{ "space" = 0x20; "escape" = 0x1B; "f1" = 0x70; "f2" = 0x71; "f3" = 0x72; "f4" = 0x73; "f5" = 0x74; "f9" = 0x78; "plus" = 0xBB; "minus" = 0xBD;
                "0" = 0x30; "1" = 0x31; "2" = 0x32; "3" = 0x33; "4" = 0x34; "5" = 0x35;
                "w" = 0x57; "a" = 0x41; "s" = 0x53; "d" = 0x44;
                "q" = 0x51; "e" = 0x45; "r" = 0x52; "shift" = 0x10 }
    $vk = $codes[$Key.ToLower()]
    if ($vk) {
        [Drive]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds $HoldMs
        [Drive]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)
    }
}

Start-Sleep -Milliseconds $SettleMs
Write-Output "ok"
