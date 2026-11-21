# Dev loop for this fork: copies the freshly built programs (bin\ from build.cmd) over the installed WacOS
# (%LOCALAPPDATA%\Programs\WacOS) and restarts the Shell. Icons, sounds, fonts, cursor and settings are left alone.
#   build.cmd [target]  then  powershell -ExecutionPolicy Bypass -File tools\dev-deploy.ps1 [-NoRestart]
param([switch]$NoRestart)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root 'bin'
$dest = Join-Path $env:LOCALAPPDATA 'Programs\WacOS'
if (-not (Test-Path "$dest\bin\WacOSShell.exe")) { throw "WacOS is not installed in $dest (install the release package first)" }

$progs = 'WacOSShell.exe', 'WacOSFinder.exe', 'WacOSSettings.exe', 'WacOSActivityMonitor.exe', 'WacOSTerminal.exe',
         'WacOSCalculator.exe', 'WacOSNotes.exe', 'WacOSScreenshot.exe', 'WacOSPreview.exe', 'WacOSReminders.exe',
         'WacOSCalendar.exe', 'WacOSTextEdit.exe', 'WacOSClock.exe', 'WacOSWeather.exe', 'WacOSContacts.exe',
         'WacOSPhotos.exe', 'WacOSTimeMachine.exe', 'WacOSMusic.exe', 'WacOSQuickTime.exe', 'WacOSForceQuit.exe',
         'WacOSTitleBars.exe', 'WacOSWindow.dll', 'WacOSWindow32.dll', 'WacOSGancho.dll', 'WacOSGancho32.dll', 'versao.txt'
$scripts = 'atualizar-github.ps1', 'baixar-apple.ps1', 'musica-registrar.ps1', 'quicktime-registrar.ps1',
           'icones-sistema.ps1', 'icones-sistema-extra.ps1', 'dialogo-desligar.ps1'

# 1. quit the Shell the proper way (gives the Windows taskbar back and unhooks the title bars), then the apps
$shellWasOpen = [bool](Get-Process WacOSShell -ErrorAction SilentlyContinue)
if ($shellWasOpen) {
    Start-Process "$dest\bin\WacOSShell.exe" -ArgumentList '--sair' -Wait
    for ($i = 0; $i -lt 40 -and (Get-Process WacOSShell -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
    if (Get-Process WacOSShell -ErrorAction SilentlyContinue) { throw 'the Shell did not quit (Ctrl+Alt+Shift+F12 and run again)' }
}
if (Get-Process WacOSTitleBars -ErrorAction SilentlyContinue) { Start-Process "$dest\bin\WacOSTitleBars.exe" -ArgumentList '--sair' -Wait }
Get-Process WacOS* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# 2. copy what was built (a DLL still loaded in another app is reported, not fatal)
$copied = 0; $failed = @()
foreach ($p in $progs) {
    $f = Join-Path $src $p
    if (-not (Test-Path $f)) { continue }
    try { Copy-Item $f "$dest\bin\$p" -Force; $copied++ } catch { $failed += $p }
}
foreach ($s in $scripts) { Copy-Item (Join-Path $PSScriptRoot $s) "$dest\tools\$s" -Force -ErrorAction SilentlyContinue }
if (Test-Path "$src\WacOSSetup.exe") { Copy-Item "$src\WacOSSetup.exe" "$dest\WacOSSetup.exe" -Force -ErrorAction SilentlyContinue }
Write-Host "copied: $copied files to $dest\bin"
if ($failed) { Write-Host ("in use, not replaced (sign out/in, then run again): " + ($failed -join ', ')) -ForegroundColor Yellow }

# 3. start it again the same way as at sign-in (scheduled task: detached from this console)
if ($shellWasOpen -and -not $NoRestart) {
    if (Get-ScheduledTask -TaskName 'WacOS' -ErrorAction SilentlyContinue) { Start-ScheduledTask -TaskName 'WacOS' }
    else { Start-Process "$dest\bin\WacOSShell.exe" -WorkingDirectory "$dest\bin" }
    Write-Host 'Shell restarted.'
}
