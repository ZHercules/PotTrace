$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
Push-Location $repo
try{
 & "$PSScriptRoot\dotnet.ps1" test PotTrace.sln -c Release --logger 'console;verbosity=minimal'
 if($LASTEXITCODE -ne 0){throw 'Managed tests failed'}
 $ctest=& "$PSScriptRoot\find-cmake-tool.ps1" -Name ctest
 & $ctest --test-dir out/native -C Release --output-on-failure
 if($LASTEXITCODE -ne 0){throw 'Native tests failed; run build.ps1 first'}
 Import-Module Pester -RequiredVersion 3.4.0
 $result=Invoke-Pester -Script tests/packaging/Install.Tests.ps1 -PassThru
 if($result.FailedCount -gt 0){throw 'Packaging tests failed'}
}finally{Pop-Location}
