param(
 [Parameter(Mandatory=$true)][string]$Archive,
 [Parameter(Mandatory=$true)][string]$PlayerDirectory,
 [switch]$Portable
)
$ErrorActionPreference='Stop'
function Check($condition,[string]$message){if(-not $condition){throw $message}}
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$id=[guid]::NewGuid().ToString('N')
$case=Join-Path $repo "out\release-install-$id"
$player=Join-Path $case 'test player'
$menu=Join-Path $case 'start menu\Programs'
$env:LOCALAPPDATA=Join-Path $case 'local'
$null=New-Item -ItemType Directory -Path $player,$menu -Force
Expand-Archive -LiteralPath $Archive -DestinationPath (Join-Path $case 'zip')
$package=Join-Path "$case\zip" ([IO.Path]::GetFileNameWithoutExtension($Archive))
$manifest=Get-Content "$package\manifest.json" -Raw -Encoding UTF8 | ConvertFrom-Json
Check ($manifest.Version -eq '0.1.0') 'Wrong packaged version'
foreach($name in 'PotPlayerMini64.exe','PotPlayer64.dll'){Copy-Item -LiteralPath (Join-Path $PlayerDirectory $name) -Destination $player}
$ini=Join-Path $player 'PotPlayerMini64.ini'
if($Portable){[IO.File]::WriteAllText($ini,"[Settings]`r`nVideoRen2=1`r`n",[Text.Encoding]::Unicode);$iniHash=(Get-FileHash $ini).Hash}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ReleaseTestRegistry {
 [DllImport("advapi32.dll")] public static extern int RegOverridePredefKey(IntPtr key, IntPtr replacement);
}
'@
$hkcu=[IntPtr](-2147483647)
$testName="Software\PotTrace-ReleaseTest-$id"
$realUser=[Microsoft.Win32.Registry]::CurrentUser
$testKey=$realUser.CreateSubKey($testName)
try{
 foreach($section in 'Shell Folders','User Shell Folders'){
  $key=$testKey.CreateSubKey("Software\Microsoft\Windows\CurrentVersion\Explorer\$section")
  try{$key.SetValue('Programs',$menu);$key.SetValue('Start Menu',(Split-Path $menu))}finally{$key.Dispose()}
 }
 $key=$testKey.CreateSubKey('Software\DAUM\PotPlayer64')
 try{$key.SetValue('ProgramPath',(Join-Path $player 'PotPlayerMini64.exe'))}finally{$key.Dispose()}
 $profile=$testKey.CreateSubKey('Software\DAUM\PotPlayerMini64')
 $key=$profile.CreateSubKey('Settings')
 try{$key.SetValue('VideoRen2',15)}finally{$key.Dispose()}
 Check (([ReleaseTestRegistry]::RegOverridePredefKey($hkcu,$testKey.Handle.DangerousGetHandle())) -eq 0) 'Could not isolate HKCU'
 $isolatedMenu=[Environment]::GetFolderPath('Programs') -eq $menu
 $recordPath=Join-Path $player 'Extension\Playback\Statistics\pottrace-install.json'
 $data=Join-Path $env:LOCALAPPDATA 'PotTrace'
 $null=New-Item -ItemType Directory -Path $data
 $sentinel=Join-Path $data 'settings.json'
 Set-Content -LiteralPath $sentinel -Value '{"Mode":1}'
 $history=Join-Path $data 'pottrace.db'
 & python -c "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); c.execute('CREATE TABLE fixture(id INTEGER)'); c.commit(); c.close()" $history
 Check ($LASTEXITCODE -eq 0) 'Could not seed SQLite'
 $hash=(Get-FileHash $history).Hash
 $settingsHash=(Get-FileHash $sentinel).Hash
 foreach($step in 'install','upgrade'){
  if($isolatedMenu){& "$package\QuickSetup.ps1" -Action Install -PotPlayerPath $player}
  else{& "$package\Install.ps1" -PotPlayerPath $player}
  $record=Get-Content $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Check ($record.Collector -and ([bool]$record.RegistryBackend -eq (-not $Portable))) 'Wrong filter backend'
  Check ($record.Version -eq $manifest.Version) 'Wrong installed version'
  if($isolatedMenu){Check ((Split-Path $record.MenuShortcut -Parent) -eq $menu -and (Test-Path -LiteralPath $record.MenuShortcut)) 'Wrong shortcut location'}
  Check (Test-Path -LiteralPath $record.Shortcut) 'Missing app shortcut'
  $shortcut=(New-Object -ComObject WScript.Shell).CreateShortcut($record.Shortcut)
  Check (Test-Path -LiteralPath $shortcut.TargetPath) 'Missing shortcut target'
  foreach($file in $manifest.Files){
   $path=if($file.Path.StartsWith('app/')){Join-Path (Split-Path $shortcut.TargetPath) $file.Path.Substring(4)}else{Join-Path (Split-Path $recordPath) $file.Path.Substring(7)}
   Check ((Get-FileHash -LiteralPath $path).Hash -eq $file.Sha256) "Installed hash mismatch: $($file.Path)"
  }
  if(-not $Portable){
   $ax=$profile.OpenSubKey('Override\0000')
   try{Check ($ax.GetValue('Path') -eq (Join-Path (Split-Path $recordPath) 'PotTrace.Collector.ax')) 'Missing registry filter'}finally{$ax.Dispose()}
  }
 }
 & "$package\QuickSetup.ps1" -Action Uninstall -PotPlayerPath $player
 foreach($path in @($recordPath,$record.Shortcut)+$record.AppFiles){Check (-not (Test-Path -LiteralPath $path)) "Still installed: $path"}
 if($record.MenuShortcut){Check (-not (Test-Path -LiteralPath $record.MenuShortcut)) 'Start menu shortcut remains'}
 foreach($name in $record.PluginFiles){Check (-not (Test-Path -LiteralPath (Join-Path (Split-Path $recordPath) $name))) "Plugin remains: $name"}
 if($Portable){Check ((Get-FileHash $ini).Hash -eq $iniHash) 'INI was not restored'}
 else{Check (-not (Test-Path $ini) -and $profile.GetSubKeyNames() -notcontains 'Override') 'Registry uninstall did not restore profile'}
 Check ((Get-FileHash $history).Hash -eq $hash -and (Get-FileHash $sentinel).Hash -eq $settingsHash) 'History or settings changed'
 Write-Output "PASS install, upgrade, shortcut, payload hashes, uninstall, profile and history preservation: $case"
}finally{
 [void][ReleaseTestRegistry]::RegOverridePredefKey($hkcu,[IntPtr]::Zero)
 if($profile){$profile.Dispose()}
 $testKey.Dispose()
 $realUser.DeleteSubKeyTree($testName,$false)
}
