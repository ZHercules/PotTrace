param([string]$Version='0.1.0')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if($Version -notmatch '^\d+\.\d+\.\d+([-a-z0-9.]*)?$'){throw 'Invalid version'}
$package=Join-Path $repo "artifacts\v$Version-win11-x64-potplayer-dev"
if((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath ($package+'.zip'))){throw 'Package output already exists.'}
Push-Location $repo
try{
 & "$PSScriptRoot\build.ps1" -Configuration Release
 New-Item -ItemType Directory -Path (Join-Path $package 'app'),(Join-Path $package 'plugin') -Force | Out-Null
 & "$PSScriptRoot\dotnet.ps1" -Arguments @('publish','src/PotTrace.App','-c','Release','-r','win-x64','--self-contained','true',"-p:Version=$Version",'-p:PublishSingleFile=false','--output',(Join-Path $package 'app'))
 if($LASTEXITCODE -ne 0){throw 'Publish failed'}
 Copy-Item -LiteralPath 'out\collector\Release\PotTrace.Collector.ax' -Destination (Join-Path $package 'plugin')
 Copy-Item -LiteralPath 'out\deps\directshow-baseclasses\LICENSE' -Destination (Join-Path $package 'app\DirectShow-baseclasses-LICENSE.txt')
 Copy-Item -LiteralPath 'out\native\Release\PotTrace.Bridge.dll' -Destination (Join-Path $package 'plugin\PotTrace.Bridge.dll')
 Copy-Item -LiteralPath 'plugin\PlaybackStatistics - PotTrace.as' -Destination (Join-Path $package 'plugin\PlaybackStatistics - PotTrace.as')
 Copy-Item -LiteralPath 'packaging\Install.ps1','packaging\Uninstall.ps1','packaging\QuickSetup.ps1','packaging\CollectorConfig.ps1','packaging\快速安装.cmd','packaging\快速卸载.cmd' -Destination $package
 if(Test-Path -LiteralPath 'docs\installation.md'){Copy-Item -LiteralPath 'docs\installation.md' -Destination (Join-Path $package '安装说明.md')}
 Copy-Item -LiteralPath 'docs\stable-validation.md' -Destination (Join-Path $package '验收记录.md')
 $files=Get-ChildItem (Join-Path $package 'app'),(Join-Path $package 'plugin') -File -Recurse | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object { @{Path=$_.FullName.Substring($package.Length+1).Replace('\','/');Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} }
 @{CaptureBackend="DirectShow";Version=$Version;Files=@($files)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding UTF8
 # Debug symbols remain local build artifacts, outside the packaged manifest.
 Get-ChildItem (Join-Path $package 'app') -Filter '*.pdb' -File | ForEach-Object {Remove-Item -LiteralPath $_.FullName}
 & python "$PSScriptRoot\check-package-paths.py" $package
 if($LASTEXITCODE -ne 0){throw 'Release path check failed'}
 Compress-Archive -LiteralPath $package -DestinationPath ($package+'.zip')
 Write-Output "Package: $package.zip"
}finally{Pop-Location}
