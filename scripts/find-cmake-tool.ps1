param([Parameter(Mandatory=$true)][ValidateSet('cmake','ctest')][string]$Name)
$command=Get-Command "$Name.exe" -CommandType Application -ErrorAction SilentlyContinue
if($command){return $command.Source}
$locator=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(Test-Path -LiteralPath $locator){
 $relative="Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$Name.exe"
 $found=& $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find $relative
 if($found){return ($found | Select-Object -First 1)}
}
throw "Install the Visual Studio CMake tools or add $Name.exe to PATH."
