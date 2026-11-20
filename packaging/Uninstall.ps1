param([Parameter(Mandatory=$true)][string]$PotPlayerPath,[switch]$RemoveHistory)
$ErrorActionPreference='Stop'
$PotPlayerPath=[IO.Path]::GetFullPath($PotPlayerPath).TrimEnd('\')
$pluginDir=Join-Path $PotPlayerPath 'Extension\Playback\Statistics'
$recordPath=Join-Path $pluginDir 'pottrace-install.json'
if(-not (Test-Path -LiteralPath $recordPath)){throw '此目录没有 PotTrace 安装登记；未删除任何文件。'}
$record=Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
$root=[IO.Path]::GetFullPath($record.InstallRoot).TrimEnd('\')
$running=Get-Process PotPlayerMini64,PotTrace -ErrorAction SilentlyContinue | Where-Object {$_.Path -and ($_.Path.StartsWith($PotPlayerPath+'\',[StringComparison]::OrdinalIgnoreCase) -or $_.Path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase))}
if($running){throw '请先退出此目录的 PotPlayer 和 PotTrace，再卸载。'}
$allowed=@('PotTrace.Bridge.dll','PlaybackStatistics - PotTrace.as','pottrace-bridge.ini')
if($record.Collector){
 . "$PSScriptRoot\CollectorConfig.ps1"
 $allowed+=@('PotTrace.Collector.ax','pottrace-collector.ini')
 $playerIni=Join-Path $PotPlayerPath 'PotPlayerMini64.ini'
 $ax=Join-Path $pluginDir 'PotTrace.Collector.ax'
 if($record.RegistryBackend){
  $playerProfileKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\DAUM\PotPlayerMini64',$true)
  if(-not $playerProfileKey){throw '当前用户的播放器配置不存在。'}
  Assert-RegistryCollectorList $playerProfileKey $ax
 }else{
  $before=Read-PlayerIni $playerIni
  $after=Remove-CollectorOverride $before $ax ([bool]$record.HadTerminator)
 }
}
$targets=@()
foreach($name in $record.PluginFiles){if($allowed -notcontains $name){throw '安装登记中的插件路径不合法。'};$targets+=Join-Path $pluginDir $name}
foreach($path in $record.AppFiles){$resolved=[IO.Path]::GetFullPath($path);if(-not $resolved.StartsWith($root+'\versions\',[StringComparison]::OrdinalIgnoreCase)){throw '安装登记中的程序路径超出自有目录。'};$targets+=$resolved}
$shortcut=Join-Path $root '播放统计.lnk';if($record.Shortcut -ne $shortcut){throw '快捷方式路径校验失败。'};$targets+=$shortcut
if($record.MenuShortcut){
 $menuLink=Join-Path ([Environment]::GetFolderPath('Programs')) 'PotTrace 播放统计.lnk'
 if($record.MenuShortcut -ne $menuLink){throw '开始菜单快捷方式路径校验失败。'}
 if(Test-Path -LiteralPath $menuLink){
  $menuTarget=(New-Object -ComObject WScript.Shell).CreateShortcut($menuLink).TargetPath
  if($record.AppFiles -contains $menuTarget){$targets+=$menuLink}
 }
}
if($RemoveHistory){
 $data=[IO.Path]::GetFullPath($record.DataRoot).TrimEnd('\')
 if($data -eq [IO.Path]::GetPathRoot($data).TrimEnd('\') -or $data -eq $PotPlayerPath -or $data -eq $root){throw '拒绝清理不安全的数据目录。'}
 # Delete only known data files; never recurse into a user-supplied directory.
 foreach($name in @('pottrace.db','pottrace.db-wal','pottrace.db-shm','pottrace.db.before-restore','settings.json','settings.json.tmp','diagnostics.log')){$targets+=Join-Path $data $name}
 if(Test-Path -LiteralPath $data -PathType Container){
  foreach($copy in Get-ChildItem -LiteralPath $data -File -Filter 'pottrace.db.before-restore-*'){
   if($copy.Name -match '^pottrace\.db\.before-restore-[0-9a-f]{32}$' -and [IO.Path]::GetDirectoryName($copy.FullName).Equals($data,[StringComparison]::OrdinalIgnoreCase)){$targets+=$copy.FullName}
  }
 }
}
# Keep the ownership record and filter registration until all owned files
# have been released. A sharing violation can then be retried normally.
foreach($target in $targets){if(Test-Path -LiteralPath $target -PathType Leaf){Remove-Item -LiteralPath $target -Force}}
if($record.RegistryBackend){Remove-RegistryCollectorList $playerProfileKey $ax}
elseif($record.Collector){Save-PlayerIni $playerIni $before $after}
try {Remove-Item -LiteralPath $recordPath -Force}
catch {
 if($record.RegistryBackend){New-RegistryCollectorList $playerProfileKey $ax}
 elseif($record.Collector){Save-PlayerIni $playerIni $after $before}
 throw
}finally{if($playerProfileKey){$playerProfileKey.Dispose()}}
Write-Output $(if($RemoveHistory){'PotTrace 已卸载，并移除指定数据文件。'}else{'PotTrace 已卸载；播放历史和设置已保留。'})
