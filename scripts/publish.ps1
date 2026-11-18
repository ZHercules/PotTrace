param([string]$Version='0.1.0-preview')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
if($Version -notmatch '^\d+\.\d+\.\d+([-a-z0-9.]*)?$'){throw 'Invalid version'}
$package=Join-Path $repo "artifacts\PotTrace-$Version-win-x64"
if(Test-Path -LiteralPath $package){throw 'Output directory already exists. Use a new version or move the previous package first.'}
Push-Location $repo
try{
 & "$PSScriptRoot\build.ps1" -Configuration Release
 New-Item -ItemType Directory -Path (Join-Path $package 'app'),(Join-Path $package 'plugin') -Force | Out-Null
 & "$PSScriptRoot\dotnet.ps1" -Arguments @('publish','src/PotTrace.App','-c','Release','-r','win-x64','--self-contained','true','-p:PublishSingleFile=false','--output',(Join-Path $package 'app'))
 if($LASTEXITCODE -ne 0){throw 'Publish failed'}
 Copy-Item -LiteralPath 'out\native\Release\PotTrace.Bridge.dll' -Destination (Join-Path $package 'plugin\PotTrace.Bridge.dll')
 Copy-Item -LiteralPath 'plugin\PlaybackStatistics - PotTrace.as' -Destination (Join-Path $package 'plugin\PlaybackStatistics - PotTrace.as')
 Copy-Item -LiteralPath 'packaging\Install.ps1','packaging\Uninstall.ps1' -Destination $package
 if(Test-Path -LiteralPath 'docs\installation.md'){Copy-Item -LiteralPath 'docs\installation.md' -Destination (Join-Path $package '安装说明.md')}
 if(Test-Path -LiteralPath 'docs\acceptance.md'){Copy-Item -LiteralPath 'docs\acceptance.md' -Destination (Join-Path $package '验收记录.md')}
 $files=Get-ChildItem (Join-Path $package 'app'),(Join-Path $package 'plugin') -File -Recurse | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object { @{Path=$_.FullName.Substring($package.Length+1).Replace('\','/');Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} }
 @{Version=$Version;Files=@($files)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $package 'manifest.json') -Encoding UTF8
 # Debug symbols remain local build artifacts, outside the packaged manifest.
 Get-ChildItem (Join-Path $package 'app') -Filter '*.pdb' -File | ForEach-Object {Remove-Item -LiteralPath $_.FullName}
 Compress-Archive -LiteralPath $package -DestinationPath ($package+'.zip')
 Write-Output "Package: $package.zip"
}finally{Pop-Location}
