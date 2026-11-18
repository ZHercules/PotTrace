param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
 & "$PSScriptRoot\dotnet.ps1" build PotTrace.sln -c $Configuration
 if($LASTEXITCODE -ne 0){throw '.NET build failed'}
 $cmake=(Get-Command cmake -ErrorAction SilentlyContinue).Source
 if(-not $cmake){$cmake='D:\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
 if(-not (Test-Path -LiteralPath $cmake)){throw 'Install Visual Studio C++ build tools and CMake first.'}
 & $cmake -S native/PotTrace.Bridge -B out/native -G 'Visual Studio 18 2026' -A x64
 if($LASTEXITCODE -ne 0){throw 'Native configure failed'}
 & $cmake --build out/native --config $Configuration
 if($LASTEXITCODE -ne 0){throw 'Native build failed'}
}finally{Pop-Location}
