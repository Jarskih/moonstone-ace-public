param(
    [Parameter(Mandatory = $true)][int]$ProcId,   # winuae64 started with -log -serlog (its console shows the Amiga's serial output)
    [Parameter(Mandatory = $true)][string]$Out,   # text file the new console lines are appended to (ASCII)
    [int]$PollMs = 150
)
# Helper of uaeshot.ps1 -Autoplay: WinUAE's `-serlog` prints what the Amiga writes to the serial port into WinUAE's own
# console window. This script (run hidden, in its own process because it gives up its console) attaches to that console and
# appends every new line to $Out. It ends when the WinUAE process does.
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class ConRead {
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool CloseHandle(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct CSBI {
        public COORD size, cursor; public ushort attr; public SMALL_RECT win; public COORD max;
    }
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleScreenBufferSize(IntPtr h, COORD size);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CSBI i);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacter(IntPtr h, StringBuilder s, uint len, COORD at, out uint got);

    // Rows [from, cursorRow) as trimmed lines; sets rows = size.Y and cursorRow. Null when the console is gone.
    public static string[] Lines(IntPtr h, int from, out int cursorRow, out int sizeY) {
        CSBI i; cursorRow = 0; sizeY = 0;
        if (!GetConsoleScreenBufferInfo(h, out i)) return null;
        cursorRow = i.cursor.Y; sizeY = i.size.Y;
        int n = cursorRow - from; if (n < 0) n = 0;
        var res = new string[n];
        for (int r = 0; r < n; r++) {
            var sb = new StringBuilder(i.size.X + 1); uint got;
            COORD c; c.X = 0; c.Y = (short)(from + r);
            ReadConsoleOutputCharacter(h, sb, (uint)i.size.X, c, out got);
            res[r] = sb.ToString(0, (int)got).TrimEnd();
        }
        return res;
    }
}
"@
[ConRead]::FreeConsole() | Out-Null
# the console may not exist yet / attach can fail transiently: retry for ~15 s
$ok = $false
for ($i = 0; $i -lt 60 -and -not $ok; $i++) {
    [void][ConRead]::FreeConsole()
    $ok = [ConRead]::AttachConsole([uint32]$ProcId)
    if (-not $ok) { Start-Sleep -Milliseconds 250 }
}
if (-not $ok) { exit 2 }
$cw = [ConRead]::GetConsoleWindow()
$h = [ConRead]::CreateFile('CONOUT$', [uint32]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
# WinUAE's console buffer is small (about 500 rows): make it long enough that a whole run never scrolls.
$info = New-Object ConRead+CSBI
[void][ConRead]::GetConsoleScreenBufferInfo($h, [ref]$info)
$big = New-Object ConRead+COORD
$big.X = $info.size.X; $big.Y = 9000
[void][ConRead]::SetConsoleScreenBufferSize($h, $big)
$from = 0
$lastLine = $null
# uaeshot.ps1 reads $Out while we append: open it shared and retry, a sharing violation must never end this script
# (it did: the log then froze and the run saw no AUTOPLAY lines).
function Append($lines) {
    $bytes = [Text.Encoding]::ASCII.GetBytes((($lines -join "`r`n") + "`r`n"))
    for ($t = 0; $t -lt 100; $t++) {
        try {
            $fs = [IO.File]::Open($Out, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            try { $fs.Write($bytes, 0, $bytes.Length) } finally { $fs.Close() }
            return
        } catch { Start-Sleep -Milliseconds 30 }
    }
}
while ($true) {
    # WinUAE can shrink the buffer again after our first resize (the log then froze at ~530 lines): keep it big every pass.
    [void][ConRead]::GetConsoleScreenBufferInfo($h, [ref]$info)
    if ($info.size.Y -lt 9000) { [void][ConRead]::SetConsoleScreenBufferSize($h, $big) }
    $cur = 0; $sy = 0
    $lines = [ConRead]::Lines($h, $from, [ref]$cur, [ref]$sy)
    if ($null -eq $lines) {   # transient read failure: only the end of WinUAE ends the loop
        if (-not (Get-Process -Id $ProcId -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds $PollMs; continue
    }
    if ($cur -ge $sy - 1 -and $cur -ge $from) {
        # the buffer is full and scrolls: find the last line we wrote and continue behind it
        $all = [ConRead]::Lines($h, 0, [ref]$cur, [ref]$sy)
        $at = -1
        if ($null -ne $lastLine) { for ($k = $all.Count - 1; $k -ge 0; $k--) { if ($all[$k] -eq $lastLine) { $at = $k; break } } }
        $lines = if ($at -ge $all.Count - 1) { @() } elseif ($at -ge 0) { $all[($at + 1)..($all.Count - 1)] } else { $all }
        $from = $cur
    }
    else { $from = $cur }
    if ($lines.Count -gt 0) {
        Append $lines
        $lastLine = $lines[$lines.Count - 1]
    }
    if (-not (Get-Process -Id $ProcId -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds $PollMs
}

