param(
 [Parameter(Mandatory=$true)][string]$PotPlayerPath,
 [string]$PackagePath=$PSScriptRoot,
 [string]$InstallRoot=(Join-Path $env:LOCALAPPDATA 'Programs\PotTrace'),
 [string]$DataRoot=(Join-Path $env:LOCALAPPDATA 'PotTrace'),
 [switch]$QuickShortcut,
 [switch]$Validate23172
)
$ErrorActionPreference='Stop'
if(-not $PackagePath){$PackagePath=$PSScriptRoot}
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
$collector=$manifest.CaptureBackend -eq 'DirectShow'
if($collector){
 . "$PSScriptRoot\CollectorConfig.ps1"
 $pluginNames+='PotTrace.Collector.ax'
 $playerIni=Join-Path $PotPlayerPath 'PotPlayerMini64.ini'
 $dll=Get-Item -LiteralPath (Join-Path $PotPlayerPath 'PotPlayer64.dll')
 $registryBackend=$dll.VersionInfo.FileVersion -eq '1, 7, 23172, 0' -and -not [IO.File]::Exists($playerIni)
 if($registryBackend){
  $metadata=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\DAUM\PotPlayer64')
  try{if(-not $metadata -or $metadata.GetValue('ProgramPath') -ne (Join-Path $PotPlayerPath 'PotPlayerMini64.exe')){throw '目标目录与当前用户注册表安装位置不符。'}}finally{if($metadata){$metadata.Dispose()}}
  $playerProfileKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\DAUM\PotPlayerMini64',$true)
  if(-not $playerProfileKey){throw '无法访问播放器当前用户配置。'}
 }else{$iniBefore=Read-PlayerIni $playerIni}
 if($Validate23172 -or $registryBackend){
  if($dll.VersionInfo.FileVersion -ne '1, 7, 23172, 0' -or
     (Get-FileHash -LiteralPath $dll.FullName).Hash -ne 'B72FDFE269DF2062502897E752A0E83A5DB938975A62D8F31D441F7D3E83461E' -or
     (Get-FileHash -LiteralPath (Join-Path $PotPlayerPath 'PotPlayerMini64.exe')).Hash -ne '6F0F63BEE95D6715E0656F8B0BCBCCE0CC3B27F03F012C8B8C4C34557A2E1CB7'){throw '播放器文件与已验证的 1.7.23172.0 构建不一致。'}
 }else{
  if($dll.VersionInfo.FileVersion -notmatch '^1, 7, 22859, 0$'){throw '当前包已验证 PotPlayer 1.7.22859.0 / EVR，请使用匹配版本。'}
 }
}
$files=@($manifest.Files)
if(($files.Path -contains 'plugin/PotTrace.Collector.ax') -ne $collector){throw '采集器清单与文件不匹配。'}
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
$managedNames=$pluginNames+@('pottrace-bridge.ini')
if($collector){
 $managedNames+='pottrace-collector.ini'
 $ax=Join-Path $pluginDir 'PotTrace.Collector.ax'
 if([bool]$previous.RegistryBackend -ne [bool]$registryBackend -and $previous.Collector){throw '播放器配置方式已改变，请先检查原有安装。'}
 if($registryBackend){
  if($previous.Collector){Assert-RegistryCollectorList $playerProfileKey $ax}
  elseif($playerProfileKey.GetSubKeyNames() -contains 'Override'){throw '已有外部滤镜配置，无法以空列表方式安装。'}
 }elseif($previous.Collector){
  $hadTerminator=[bool]$previous.HadTerminator
  $null=Remove-CollectorOverride $iniBefore $ax $hadTerminator
  $iniAfter=$iniBefore
 }else{
  $hadTerminator=@(Get-OverrideSections $iniBefore).Count -gt 0
  $iniAfter=Add-CollectorOverride $iniBefore $ax
 }
}elseif($previous.Collector){throw '请先卸载正式采集器再切换后端。'}
foreach($name in $managedNames){
 if((Test-Path -LiteralPath (Join-Path $pluginDir $name)) -and ($previous.PluginFiles -notcontains $name)){throw "发现未登记文件：$name"}
}
$menuLink=Join-Path ([Environment]::GetFolderPath('Programs')) 'PotTrace 播放统计.lnk'
$withMenu=$QuickShortcut -or ($previous -and $previous.MenuShortcut)
if($withMenu -and (Test-Path -LiteralPath $menuLink)){
 $menuTarget=(New-Object -ComObject WScript.Shell).CreateShortcut($menuLink).TargetPath
 if(-not $previous -or $previous.MenuShortcut -ne $menuLink -or $previous.AppFiles -notcontains $menuTarget){throw "开始菜单已有同名快捷方式，请先检查：$menuLink"}
}
$versionDir=[IO.Path]::GetFullPath((Join-Path $InstallRoot ('versions\'+$manifest.Version+'-'+[Guid]::NewGuid().ToString('N'))))
if(-not $versionDir.StartsWith($InstallRoot+'\versions\',[StringComparison]::OrdinalIgnoreCase)){throw '程序目录校验失败。'}
$backups=@{};$created=@();$ownedApps=@()
try{
 New-Item -ItemType Directory -Path $pluginDir,$versionDir,$DataRoot -Force | Out-Null
 foreach($name in $managedNames+@('pottrace-install.json')){
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
 $backendLines=''
 if($collector){
  $backendLines="SourceMode=DirectShow`r`nCaptureBackend=Collector`r`n"
  $policyPath=Join-Path $DataRoot 'capture-policy.json'
  $diagnosticLine=if($Validate23172){"Diagnostics=1`r`n"}else{''}
  if($registryBackend){$diagnosticLine+="ProfileBackend=Registry`r`n"}
  [IO.File]::WriteAllText((Join-Path $pluginDir 'pottrace-collector.ini'),"[PotTrace]`r`n${diagnosticLine}BridgePath=$pluginDir\PotTrace.Bridge.dll`r`nPolicyPath=$policyPath`r`nPlayerIni=$playerIni`r`n",[Text.Encoding]::Unicode)
 }
 [IO.File]::WriteAllText((Join-Path $pluginDir 'pottrace-bridge.ini'),"[PotTrace]`r`n${backendLines}HostPath=$exe`r`nDataDirectory=$DataRoot`r`nChannel=main`r`n",[Text.Encoding]::Unicode)
 $shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($shortcutPath);$link.TargetPath=$exe;$link.Arguments='--show --data-dir "'+$DataRoot+'"';if($collector){$link.Arguments+=' --collector'};$link.WorkingDirectory=$versionDir;$link.Save()
 if($withMenu){
  $backups[$menuLink]=if(Test-Path -LiteralPath $menuLink){[IO.File]::ReadAllBytes($menuLink)}else{$null}
  $menu=$shell.CreateShortcut($menuLink)
  $menu.TargetPath=$exe
  $menu.Arguments=$link.Arguments
  $menu.WorkingDirectory=$versionDir
  $menu.Hotkey='CTRL+ALT+SHIFT+H'
  $menu.Save()
 }
 $allApps=@($ownedApps);if($previous){$allApps+=@($previous.AppFiles)}
 $record=@{Version=$manifest.Version;InstallRoot=$InstallRoot;DataRoot=$DataRoot;AppFiles=@($allApps|Select-Object -Unique);PluginFiles=$managedNames;Shortcut=$shortcutPath}
 if($collector){
  $record.Collector=$true
  $record.HadTerminator=$hadTerminator
  if($registryBackend){
   $record.RegistryBackend=$true
   if(-not $previous.Collector){New-RegistryCollectorList $playerProfileKey $ax;$registryChanged=$true}
  }elseif($iniBefore -cne $iniAfter){Save-PlayerIni $playerIni $iniBefore $iniAfter;$iniChanged=$true}
 }
 if($withMenu){$record.MenuShortcut=$menuLink}
 [IO.File]::WriteAllText($ownership,($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 Write-Output "已部署 PotTrace $($manifest.Version)。首次使用请在 PotPlayer → 选项 → 扩展功能 → 播放统计中勾选 PotTrace。"
 if($collector){Write-Output '打开统计设置，勾选“启用播放统计”；选择“详细”保存播放片段。'}
 Write-Output "统计窗口入口：$shortcutPath"
 Write-Output '是否正在统计以窗口中的连接状态为准；复制成功不代表扩展已启用。'
}catch{
 $originalError=$_
 if($registryChanged){try{Remove-RegistryCollectorList $playerProfileKey $ax}catch{Write-Warning "注册表滤镜回退失败：$_"}}
 if($iniChanged){try{Save-PlayerIni $playerIni $iniAfter $iniBefore}catch{Write-Warning "滤镜配置回退失败：$_"}}
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
}finally{if($playerProfileKey){$playerProfileKey.Dispose()}}
