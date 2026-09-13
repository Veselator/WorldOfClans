# Sends a sequence of keys (with modifiers) to the game window.
# Usage: keys.ps1 "home" "shift+end" "ctrl+a" "x" "backspace"
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Keys)
$SettleMs = 500

Add-Type @"
using System;using System.Text;using System.Runtime.InteropServices;
public class Keys2 {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr extra);
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

$h = [Keys2]::Find("WorldOfClansWindow")
if ($h -eq [IntPtr]::Zero) { Write-Output "window not found"; exit 1 }
[Keys2]::BringWindowToTop($h) | Out-Null
[Keys2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 250

# vk, scancode, extended(1/0). Windows rewrites the modifier state around an injected key
# whose scancode does not match its virtual key, which is why shift+arrow silently lost the
# shift until these were filled in properly.
$map = @{
  "home" = @(0x24,0x47,1); "end" = @(0x23,0x4F,1); "left" = @(0x25,0x4B,1); "right" = @(0x27,0x4D,1);
  "up" = @(0x26,0x48,1); "down" = @(0x28,0x50,1);
  "backspace" = @(0x08,0x0E,0); "delete" = @(0x2E,0x53,1); "enter" = @(0x0D,0x1C,0);
  "escape" = @(0x1B,0x01,0); "tab" = @(0x09,0x0F,0); "space" = @(0x20,0x39,0)
}
foreach ($n in 65..90) { $map[([string][char]$n).ToLower()] = @($n, 0, 0) }
foreach ($n in 0..9) { $map["$n"] = @((0x30 + $n), 0, 0) }
foreach ($n in 1..12) { $map["f$n"] = @((0x6F + $n), 0, 0) }

foreach ($combo in $Keys) {
  $parts = $combo.ToLower().Split('+')
  $mods = @()
  $key = $parts[-1]
  foreach ($p in $parts[0..($parts.Length - 2)]) {
    if ($p -eq "shift") { $mods += ,@(0x10, 0x2A) }
    elseif ($p -eq "ctrl") { $mods += ,@(0x11, 0x1D) }
    elseif ($p -eq "alt") { $mods += ,@(0x12, 0x38) }
  }
  $entry = $map[$key]
  if (-not $entry) { Write-Output "unknown key: $key"; continue }
  $vk = $entry[0]; $scan = $entry[1]; $ext = [uint32]$entry[2]

  foreach ($m in $mods) { [Keys2]::keybd_event([byte]$m[0], [byte]$m[1], 0, [IntPtr]::Zero) }
  if ($mods.Count -gt 0) { Start-Sleep -Milliseconds 200 }
  [Keys2]::keybd_event([byte]$vk, [byte]$scan, $ext, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 70
  [Keys2]::keybd_event([byte]$vk, [byte]$scan, $ext -bor 2, [IntPtr]::Zero)
  if ($mods.Count -gt 0) { Start-Sleep -Milliseconds 150 }
  foreach ($m in $mods) { [Keys2]::keybd_event([byte]$m[0], [byte]$m[1], 2, [IntPtr]::Zero) }
  Start-Sleep -Milliseconds 120
}

Start-Sleep -Milliseconds $SettleMs
Write-Output "ok"

