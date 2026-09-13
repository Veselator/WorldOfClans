# Drags the mouse across the game window, from one client-relative point to another.
# Used to exercise the rubber-band selection while iterating on the interface.
param(
    [int]$X1 = 0,
    [int]$Y1 = 0,
    [int]$X2 = 0,
    [int]$Y2 = 0,
    [int]$Steps = 12,
    [int]$SettleMs = 600
)

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class Drag {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
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

$window = [Drag]::FindByClass("WorldOfClansWindow")
if ($window -eq [IntPtr]::Zero) { Write-Error "Game window not found"; exit 1 }
[void][Drag]::SetForegroundWindow($window)
Start-Sleep -Milliseconds 250

function To-Screen([int]$x, [int]$y) {
    $p = New-Object Drag+POINT
    $p.X = $x; $p.Y = $y
    [void][Drag]::ClientToScreen($window, [ref]$p)
    return $p
}

$from = To-Screen $X1 $Y1
$to = To-Screen $X2 $Y2

[void][Drag]::SetCursorPos($from.X, $from.Y)
Start-Sleep -Milliseconds 120
[Drag]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)   # left down

for ($i = 1; $i -le $Steps; $i++) {
    $t = $i / $Steps
    $x = [int]($from.X + ($to.X - $from.X) * $t)
    $y = [int]($from.Y + ($to.Y - $from.Y) * $t)
    [void][Drag]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 30
}

[Drag]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)   # left up
Start-Sleep -Milliseconds $SettleMs
Write-Output "ok"
