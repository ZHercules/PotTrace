param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$localSdk = Join-Path $repoRoot '..\..\.tools\dotnet\dotnet.exe'
if (-not (Test-Path -LiteralPath $localSdk)) { $localSdk = (Get-Command dotnet -ErrorAction Stop).Source }
$toolsRoot = if (Test-Path -LiteralPath (Join-Path $repoRoot '..\..\.tools')) { [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\.tools')) } else { Join-Path $repoRoot '.tools' }
$env:DOTNET_CLI_HOME = Join-Path $toolsRoot 'cli-home'
$env:NUGET_PACKAGES = Join-Path $toolsRoot 'nuget'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
& $localSdk @Arguments
exit $LASTEXITCODE
