param(
 [Parameter(Mandatory=$true)][string]$PotPlayerPath,
 [string]$PackagePath=$PSScriptRoot,
 [string]$InstallRoot=(Join-Path $env:LOCALAPPDATA 'Programs\PotTrace'),
 [string]$DataRoot=(Join-Path $env:LOCALAPPDATA 'PotTrace')
)
$ErrorActionPreference='Stop'
$PotPlayerPath=[IO.Path]::GetFullPath($PotPlayerPath).TrimEnd('\')
$PackagePath=[IO.Path]::GetFullPath($PackagePath).TrimEnd('\')
$InstallRoot=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$DataRoot=[IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
if(-not (Test-Path -LiteralPath (Join-Path $PotPlayerPath 'PotPlayerMini64.exe'))){throw '请选择包含 PotPlayerMini64.exe 的 64 位 PotPlayer 文件夹。'}
$running=Get-Process PotPlayerMini64,PotTrace -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path.StartsWith($PotPlayerPath+'\',[StringComparison]::OrdinalIgnoreCase) -or $_.Path.StartsWith($InstallRoot+'\',[StringComparison]::OrdinalIgnoreCase)) }
if($running){throw '请先退出此目录的 PotPlayer 和 PotTrace，再运行安装程序。'}
$manifest=Get-Content -LiteralPath (Join-Path $PackagePath 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.Version -notmatch '^\d+\.\d+\.\d+([-a-z0-9.]*)?$'){throw '包版本无效。'}
$pluginNames=@('PotTrace.Bridge.dll','PlaybackStatistics - PotTrace.as')
$files=@($manifest.Files)
foreach($file in $files){
 $relative=[string]$file.Path
 if($relative -notmatch '^(app|plugin)/' -or $relative.Contains('..') -or $relative.Contains(':')){throw '包清单包含不允许的路径。'}
 if($relative.StartsWith('plugin/') -and $pluginNames -notcontains $relative.Substring(7)){throw '包清单包含未知插件文件。'}
 $source=[IO.Path]::GetFullPath((Join-Path $PackagePath $relative))
 if(-not $source.StartsWith($PackagePath+'\',[StringComparison]::OrdinalIgnoreCase)){throw '包文件超出安装包目录。'}
 if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.Sha256){throw "包文件校验失败：$relative"}
}
foreach($required in @('app/PotTrace.exe','plugin/PotTrace.Bridge.dll','plugin/PlaybackStatistics - PotTrace.as')){if($files.Path -notcontains $required){throw "包中缺少 $required"}}
$pluginDir=Join-Path $PotPlayerPath 'Extension\Playback\Statistics'
$ownership=Join-Path $pluginDir 'pottrace-install.json'
$previous=if(Test-Path -LiteralPath $ownership){Get-Content -LiteralPath $ownership -Raw -Encoding UTF8 | ConvertFrom-Json}else{$null}
if($previous -and $previous.InstallRoot -ne $InstallRoot){throw '升级必须使用原程序目录；迁移前请先卸载。'}
foreach($name in $pluginNames+@('pottrace-bridge.ini')){if((Test-Path -LiteralPath (Join-Path $pluginDir $name)) -and -not $previous){throw "发现同名但未登记的文件，请先备份处理：$name"}}
$versionDir=[IO.Path]::GetFullPath((Join-Path $InstallRoot ('versions\'+$manifest.Version+'-'+[Guid]::NewGuid().ToString('N'))))
if(-not $versionDir.StartsWith($InstallRoot+'\versions\',[StringComparison]::OrdinalIgnoreCase)){throw '程序目录校验失败。'}
$backups=@{};$created=@();$ownedApps=@()
try{
 New-Item -ItemType Directory -Path $pluginDir,$versionDir,$DataRoot -Force | Out-Null
 foreach($name in $pluginNames+@('pottrace-bridge.ini','pottrace-install.json')){
  $target=Join-Path $pluginDir $name
  $backups[$target]=if(Test-Path -LiteralPath $target){[IO.File]::ReadAllBytes($target)}else{$null}
 }
 $shortcutPath=Join-Path $InstallRoot '播放统计.lnk'
 $backups[$shortcutPath]=if(Test-Path -LiteralPath $shortcutPath){[IO.File]::ReadAllBytes($shortcutPath)}else{$null}
 foreach($file in $files | Where-Object { $_.Path.StartsWith('app/') }){
  $target=Join-Path $versionDir $file.Path.Substring(4)
  New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $PackagePath $file.Path) -Destination $target
  $ownedApps+=$target;$created+=$target
 }
 foreach($name in $pluginNames){Copy-Item -LiteralPath (Join-Path $PackagePath ('plugin/'+$name)) -Destination (Join-Path $pluginDir $name)}
 $exe=Join-Path $versionDir 'PotTrace.exe'
 [IO.File]::WriteAllText((Join-Path $pluginDir 'pottrace-bridge.ini'),"[PotTrace]`r`nHostPath=$exe`r`nDataDirectory=$DataRoot`r`nChannel=main`r`n",[Text.Encoding]::Unicode)
 $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcutPath);$link.TargetPath=$exe;$link.Arguments='--show --data-dir "'+$DataRoot+'"';$link.WorkingDirectory=$versionDir;$link.Save()
 $allApps=@($ownedApps);if($previous){$allApps+=@($previous.AppFiles)}
 $record=@{Version=$manifest.Version;InstallRoot=$InstallRoot;DataRoot=$DataRoot;AppFiles=@($allApps|Select-Object -Unique);PluginFiles=$pluginNames+@('pottrace-bridge.ini');Shortcut=$shortcutPath}
 [IO.File]::WriteAllText($ownership,($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 Write-Output "已部署 PotTrace $($manifest.Version)。首次使用请在 PotPlayer → 选项 → 扩展功能 → 播放统计中勾选 PotTrace。"
 Write-Output "统计窗口入口：$shortcutPath"
 Write-Output '是否正在统计以窗口中的连接状态为准；复制成功不代表扩展已启用。'
}catch{
 $originalError=$_
 foreach($target in $backups.Keys){
  try{
   if($null -ne $backups[$target]){
    $unchanged=(Test-Path -LiteralPath $target) -and ([Convert]::ToBase64String([IO.File]::ReadAllBytes($target)) -ceq [Convert]::ToBase64String($backups[$target]))
    if(-not $unchanged){[IO.File]::WriteAllBytes($target,$backups[$target])}
   }elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
  }catch{Write-Warning "回滚此文件失败，请检查权限：$target"}
 }
 foreach($target in $created){if($target.StartsWith($versionDir+'\',[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $target)){Remove-Item -LiteralPath $target -Force}}
 throw $originalError
}
