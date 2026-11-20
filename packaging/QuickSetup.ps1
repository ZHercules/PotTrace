param(
 [Parameter(Mandatory=$true)][ValidateSet('Install','Uninstall')][string]$Action,
 [Parameter(Position=0)][AllowEmptyString()][string]$PotPlayerPath
)
$ErrorActionPreference='Stop'
if(-not $PotPlayerPath){
 Add-Type -AssemblyName System.Windows.Forms
 $picker=New-Object System.Windows.Forms.OpenFileDialog
 try{
  $picker.Title='选择目标 PotPlayer 的 PotPlayerMini64.exe'
  $picker.Filter='64 位 PotPlayer|PotPlayerMini64.exe'
  foreach($folder in @('E:\pot\PotPlayer',(Join-Path $env:ProgramFiles 'DAUM\PotPlayer'),(Join-Path $env:ProgramFiles 'PotPlayer'))){
   if(Test-Path -LiteralPath (Join-Path $folder 'PotPlayerMini64.exe')){$picker.InitialDirectory=$folder;break}
  }
  if($picker.ShowDialog() -ne 'OK'){return}
  $PotPlayerPath=Split-Path -Parent $picker.FileName
 }finally{$picker.Dispose()}
}
if($Action -eq 'Install'){
 & "$PSScriptRoot\Install.ps1" -PotPlayerPath $PotPlayerPath -QuickShortcut
 Write-Output '按 Ctrl+Alt+Shift+H 或在开始菜单选择「PotTrace 播放统计」打开窗口。'
}else{
 & "$PSScriptRoot\Uninstall.ps1" -PotPlayerPath $PotPlayerPath
}
