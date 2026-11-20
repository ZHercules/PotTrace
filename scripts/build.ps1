param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
 & "$PSScriptRoot\dotnet.ps1" build PotTrace.sln -c $Configuration
 if($LASTEXITCODE -ne 0){throw '.NET build failed'}
 $cmake=& "$PSScriptRoot\find-cmake-tool.ps1" -Name cmake
 if(-not (Test-Path -LiteralPath $cmake)){throw 'Install Visual Studio C++ build tools and CMake first.'}
 & $cmake -S native/PotTrace.Bridge -B out/native -G 'Visual Studio 18 2026' -A x64
 if($LASTEXITCODE -ne 0){throw 'Native configure failed'}
 & $cmake --build out/native --config $Configuration
 if($LASTEXITCODE -ne 0){throw 'Native build failed'}
 & "$PSScriptRoot\verify-directshow.ps1"
 & $cmake -S native/PotTrace.Collector -B out/collector -G 'Visual Studio 18 2026' -A x64
 if($LASTEXITCODE -ne 0){throw 'Collector configure failed'}
 & $cmake --build out/collector --config $Configuration
 if($LASTEXITCODE -ne 0){throw 'Collector build failed'}
}finally{Pop-Location}
