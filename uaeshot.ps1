param(
    [string]$Config = "moonstone-ace-hd.uae",
    [string]$Steps = "wait:40,shot:boot",
    [string]$Dir = "$PSScriptRoot\build\shots",
    [switch]$Keep,
    [switch]$Joy,
    [int]$Instance = 0,          # N > 0: own config copy build\uaeshot-N.uae and log, kills only its own WinUAE (0 = the old behaviour: kills every winuae64)
    [string]$Autoplay = "",      # autoplay script (docs/AUTOPLAY.md): copied to the HD dir as autoplay.txt; no keystrokes, no focus
    [switch]$Hidden,             # -Autoplay: minimise the emulator window. EXPERIMENTAL: on this machine the emulation then stalls (no frames), so the default (bottom of the z-order, no activation) is the one to use
    [int]$Timeout = 180,         # -Autoplay: give up after this many seconds
    [int]$ShotDelayMs = 300,    # -Autoplay: wait this long after a "shot" log line before capturing (a slow host does the same; raise it to test the shot freeze)
    [string]$Exe = ""            # -Autoplay: copy this hunk exe over the HD dir's "moonstone" first (default: leave the HD dir as is)
)
# Boot WinUAE with $Config and run a step script, e.g.
#   -Steps "wait:40,shot:C3-0,key:Space,wait:2,shot:C3-1,hold:Down:500,shot:C3-2"
# shot:<name>  PrintWindow capture into $Dir\<name>.png (works unfocused)
# key:<k>      tap; hold:<k>:<ms>  hold; keys need focus, so the window is fronted first
# type:<TEXT>  tap each letter/digit of TEXT (A-Z, 0-9)
# wait:<s>     sleep. WinUAE is killed at the end unless -Keep.
# -Joy         joystick port 1 = WinUAE "kbd2": cursor keys move, RCtrl is fire (those keys then stop
#              reaching the Amiga keyboard).
#
# Headless mode (no desktop focus, no keystrokes, no mouse; several can run side by side with different -Instance):
#   -Instance 2 -Autoplay script.txt [-Hidden] [-Config x.uae] [-Exe path\to\moonstone] [-Timeout s]
# needs a build with -DMS_AUTOPLAY=1 (docs/AUTOPLAY.md). The script is copied to the HD dir (the filesystem2= line of
# -Config) as autoplay.txt and the game injects the input itself. WinUAE is started with -log -serlog: the Amiga's serial
# output (rt/serlog) appears in its console window, tools\conread.ps1 copies that console to build\uaeshot-N.log. Lines
# "AUTOPLAY shot <name> frame N" (script line `frame N shot <name>`) make this script capture the emulator window to
# $Dir\<name>.png, "AUTOPLAY quit" ends the run; without one it stops after -Timeout seconds and takes "autoplay-end".
# -Steps is ignored. The window is sent to the bottom of the z-order without activating it (-Hidden minimises it instead, but a minimised WinUAE stalls here).
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Uae {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
    public struct RECT { public int Left, Top, Right, Bottom; }
    // The emulator's own window of process pid (class PCsuxRox); with -log the process' MainWindowHandle is the console.
    public static IntPtr FindEmuWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr lp) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid) return true;
            var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64);
            if (sb.ToString() == "PCsuxRox") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@
[Uae]::SetProcessDPIAware() | Out-Null
$keys = @{ Space = 0x20; Return = 0x0D; Esc = 0x1B; Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28;
           RCtrl = 0xA3; F1 = 0x70; F2 = 0x71; F3 = 0x72; F4 = 0x73; Y = 0x59; N = 0x4E; A = 0x41 }
$keys["Backspace"] = 0x08
for ($c = 0; $c -lt 10; $c++) { $keys["$c"] = 0x30 + $c }
for ($c = 0; $c -lt 26; $c++) { $keys["$([char](65 + $c))"] = 65 + $c }

$root = $PSScriptRoot   # the checkout this script lives in (also works from a git worktree)
$isAuto = $Autoplay -ne ""
# Injected keys (keybd_event) only reach WinUAE's Windows-message keyboard, not its default raw-input one,
# so boot a copy of $Config with input config 1 = "WinUAE keyboard" (as moonshard's configs do).
$src = (Resolve-Path (Join-Path $root $Config)).Path
$tag = if ($Instance -gt 0) { "uaeshot-$Instance" } else { "uaeshot" }
$cfg = "$root\build\$tag.uae"
$log = "$root\build\$tag.log"
$extra = "input.config=1", "input.1.keyboard.0.friendlyname=WinUAE keyboard", "input.1.keyboard.0.name=NULLKEYBOARD",
         "input.1.keyboard.0.empty=false", "input.1.keyboard.0.disabled=false"
# Mouse off (port 0 unplugged, custom-input mice disabled, window not capturing): a captured host click is port-0 fire, which skips the intro (rt_intro_skip_poll).
$extra += "joyport0=none", "win32.start_uncaptured=true", "input.1.mouse.0.disabled=true", "input.1.mouse.0.empty=true",
          "input.1.mouse.1.disabled=true", "input.1.mouse.1.empty=true"
if ($Joy) { $extra += "joyport1=kbd2", "joyport1autofire=none" }
# "direct" serial: the game's transmit never waits for the emulated baud rate.
if ($isAuto) { $extra += "serial_direct=true", "joyport1=none", "win32.iconified_pause=false", "win32.inactive_pause=false", "win32.iconified_nosound=false", "sound_output=none" }
(Get-Content $src) + $extra | Set-Content -Encoding ascii $cfg

if ($isAuto) {
    $hd = $null
    foreach ($l in (Get-Content $src)) {
        if ($l -match '^filesystem2=[a-z]+,[^:]+:[^:]+:([^,]+),') { $hd = $Matches[1] }
    }
    if (-not $hd) { throw "no filesystem2= line in ${src}: cannot place autoplay.txt" }
    if ($Exe) { Copy-Item -Force (Join-Path $root $Exe) (Join-Path $hd "moonstone") }
    Copy-Item -Force (Resolve-Path $Autoplay).Path (Join-Path $hd "autoplay.txt")   # CRLF or LF, the parser takes both
    Write-Host "autoplay.txt -> $hd"
}

if (Test-Path $log) { Remove-Item -Force $log }
if ($Instance -le 0) { Get-Process winuae64 -ErrorAction SilentlyContinue | Stop-Process -Force }
$wargs = "-config=`"$cfg`""
if ($isAuto) { $wargs += " -log -serlog" }
$p = Start-Process "C:\Program Files\WinUAE\winuae64.exe" -ArgumentList $wargs -PassThru
if ($isAuto) {
    Start-Process powershell.exe -WindowStyle Hidden -ArgumentList "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$root\tools\conread.ps1`"",
        "-ProcId", $p.Id, "-Out", "`"$log`"" | Out-Null
}
for ($i = 0; $i -lt 50 -and $p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 200; $p.Refresh() }
$h = $p.MainWindowHandle
if ($isAuto) {   # the emulator's own window, not the -log console
    for ($i = 0; $i -lt 50; $i++) { $w = [Uae]::FindEmuWindow([uint32]$p.Id); if ($w -ne [IntPtr]::Zero) { $h = $w; break }; Start-Sleep -Milliseconds 200 }
    if ($Hidden) { [Uae]::ShowWindow($h, 7) | Out-Null }   # SW_SHOWMINNOACTIVE
    else { [Uae]::SetWindowPos($h, [IntPtr]1, 0, 0, 0, 0, 0x13) | Out-Null }   # HWND_BOTTOM, NOSIZE|NOMOVE|NOACTIVATE
}
Start-Sleep -Milliseconds 1500
# Front the window once at boot: the first activation captures the mouse and a key sent with it can be lost.
# (Not in autoplay mode: that is the point of it.)
if (-not $isAuto) { [Uae]::SetForegroundWindow($h) | Out-Null }

function Shot([string]$Name) {
    $r = New-Object Uae+RECT
    [Uae]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    # We are DPI aware, but WinUAE's window usually is not, and its monitor may have another scale (it can also be dragged to a
    # different monitor between two shots): GetWindowRect then reports physical pixels while PrintWindow draws the window at its
    # logical (96 dpi) size into the top-left of the bitmap. So no DPI arithmetic: fill the bitmap with a sentinel colour, let
    # PrintWindow draw, and keep exactly the drawn part. A DPI-aware window fills the whole bitmap and is kept whole.
    # (tools/shotcmp.py in addition copes with captures at other scales or inside a larger canvas.)
    $sent = [System.Drawing.Color]::FromArgb(255, 255, 0, 255)
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear($sent)
    $hdc = $g.GetHdc()
    [Uae]::PrintWindow($h, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $ry = [Math]::Min(12, $ht - 1); $cx = [Math]::Min(12, $w - 1)
    $dw = $w; while ($dw -gt 1 -and $bmp.GetPixel($dw - 1, $ry).ToArgb() -eq $sent.ToArgb()) { $dw-- }
    $dh = $ht; while ($dh -gt 1 -and $bmp.GetPixel($cx, $dh - 1).ToArgb() -eq $sent.ToArgb()) { $dh-- }
    if ($dw -lt $w -or $dh -lt $ht) {
        $crop = $bmp.Clone((New-Object System.Drawing.Rectangle 0, 0, $dw, $dh), $bmp.PixelFormat)
        $bmp.Dispose(); $bmp = $crop
    }
    $bmp.Save("$Dir\$Name.png")
    $bmp.Dispose()
    Write-Host "shot $Name"
}
function Press([string]$K, [int]$Ms) {
    [Uae]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 150
    $vk = [byte]$keys[$K]
    $scan = [byte][Uae]::MapVirtualKey($vk, 0)
    # KEYEVENTF_EXTENDEDKEY: arrows and RCtrl, otherwise they arrive as numpad keys / LCtrl
    $ext = if (@(0x25, 0x26, 0x27, 0x28, 0xA3) -contains $vk) { 1 } else { 0 }
    [Uae]::keybd_event($vk, $scan, $ext, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $Ms
    [Uae]::keybd_event($vk, $scan, $ext -bor 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Write-Host "key $K ${Ms}ms"
}

if ($isAuto) {
    # Follow the serial log: "AUTOPLAY shot <name> frame N" -> capture, "AUTOPLAY quit" -> done.
    $seen = 0
    $t0 = Get-Date
    $done = $false
    while (-not $done -and ((Get-Date) - $t0).TotalSeconds -lt $Timeout -and -not $p.HasExited) {
        if (Test-Path $log) {
            $lines = @(Get-Content $log -ErrorAction SilentlyContinue)
            for (; $seen -lt $lines.Count; $seen++) {
                $l = $lines[$seen]
                if ($l -cmatch '^AUTOPLAY shot (\S+)') {
                    # The guest freezes for 400 frames after this line (rt/autoplay SHOT_FREEZE_FRAMES): capture inside that window.
                    $sn = $Matches[1]; Start-Sleep -Milliseconds $ShotDelayMs; Shot $sn
                    $late = @(Get-Content $log -ErrorAction SilentlyContinue) -cmatch ('^AUTOPLAY shot-end ' + [regex]::Escape($sn) + '$')
                    if ($late) { Write-Host "SHOT-LATE $sn (the freeze was already over when the capture finished: the picture may be a later screen)" }
                }
                elseif ($l -cmatch '^AUTOPLAY quit') { Start-Sleep -Milliseconds 300; $done = $true }
                if ($l -cmatch '^AUTOPLAY') { Write-Host "  $l" }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $done) { Write-Host "autoplay: no quit line within ${Timeout}s"; if (-not $p.HasExited) { Shot "autoplay-end" } }
}
else {
    foreach ($s in $Steps.Split(',')) {
        $a = $s.Split(':')
        switch ($a[0]) {
            'wait' { Start-Sleep -Milliseconds ([double]$a[1] * 1000) }
            'shot' { Shot $a[1] }
            'key'  { Press $a[1] 120 }
            'hold' { Press $a[1] ([int]$a[2]) }
            'type' { foreach ($ch in $a[1].ToUpper().ToCharArray()) { Press "$ch" 80 } }
            default { throw "bad step $s" }
        }
    }
}
if (-not $Keep -and -not $p.HasExited) { $p | Stop-Process -Force }
