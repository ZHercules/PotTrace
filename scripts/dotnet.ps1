param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = Join-Path $repoRoot '.tools'
$sharedTools = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\.tools'))
if (-not (Test-Path -LiteralPath (Join-Path $toolsRoot 'dotnet\dotnet.exe')) -and (Test-Path -LiteralPath (Join-Path $sharedTools 'dotnet\dotnet.exe'))) { $toolsRoot = $sharedTools }
$localSdk = Join-Path $toolsRoot 'dotnet\dotnet.exe'
if (-not (Test-Path -LiteralPath $localSdk)) { $localSdk = (Get-Command dotnet -ErrorAction Stop).Source }
$env:DOTNET_CLI_HOME = Join-Path $toolsRoot 'cli-home'
$env:NUGET_PACKAGES = Join-Path $toolsRoot 'nuget'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
& $localSdk @Arguments
exit $LASTEXITCODE
