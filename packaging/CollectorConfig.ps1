# PotPlayer stores private filters in numbered Override sections followed by Type=-2.
$collectorClsid='{A5092F25-09C8-46EB-9138-638E3C0B4287}'
function Read-PlayerIni([string]$Path) {
 if(-not [IO.File]::Exists($Path)){throw '需要已启用的 PotPlayer INI 配置；此包未支持注册表配置。'}
 $bytes=[IO.File]::ReadAllBytes($Path)
 if($bytes.Length -lt 2 -or $bytes[0] -ne 255 -or $bytes[1] -ne 254){throw '不支持的 PotPlayer INI 编码。'}
 return [Text.UnicodeEncoding]::new($false,$true,$true).GetString($bytes,2,$bytes.Length-2)
}
function Save-PlayerIni([string]$Path,[string]$Before,[string]$After) {
 if((Read-PlayerIni $Path) -cne $Before){throw '播放器配置在操作期间发生变化，请关闭播放器后重试。'}
 $temp=Join-Path (Split-Path $Path) ([Guid]::NewGuid().ToString('N')+'.tmp')
 try {
  [IO.File]::WriteAllText($temp,$After,[Text.Encoding]::Unicode)
  [IO.File]::Replace($temp,$Path,$temp+'.bak')
 }finally{
  foreach($f in @($temp,($temp+'.bak'))){if([IO.File]::Exists($f)){[IO.File]::Delete($f)}}
 }
}
function Get-OverrideSections([string]$Text){
 $blocks=[regex]::Matches($Text,'(?ims)^\[Override\\(?<index>\d{4})\][ \t]*\r?\n(?<body>.*?)(?=^\[|\z)')
 if($blocks.Count -ne [regex]::Matches($Text,'(?im)^\[Override\\').Count){throw '存在未知滤镜配置格式。'}
 for($i=0;$i -lt $blocks.Count;$i++){
  $block=$blocks[$i]
  if([int]$block.Groups['index'].Value -ne $i){throw '滤镜编号不连续，无法安全修改。'}
  if(($block.Groups['body'].Value.Trim() -eq 'Type=-2') -ne ($i -eq $blocks.Count-1)){throw '滤镜配置结束标记不正确。'}
  $block
 }
}
function Get-CollectorLines([string]$Ax){
 @('Type=1',"Path=$Ax","CLSID=$collectorClsid",'Name=PotTrace Collector','Disabled=0','FilterType=0','Merit=2097152','MeritHi=0','mod0000={73646976-0000-0010-8000-00AA00389B71}','mod0001={00000000-0000-0000-0000-000000000000}')
}

# The diagnostic registry installation owns only a newly created filter list.
# Existing lists need insertion/renumbering and are left to the INI installer.
function New-RegistryCollectorList($playerProfileKey,[string]$Ax) {
 if($playerProfileKey.GetSubKeyNames() -contains 'Override'){throw '已有外部滤镜配置，无法以空列表方式安装。'}
 $list=$playerProfileKey.CreateSubKey('Override')
 $created=@()
 try {
  foreach($name in @('0000','0001')){
   $entry=$list.CreateSubKey($name);$created+=$name
   try{
    $lines=if($name -eq '0000'){Get-CollectorLines $Ax}else{'Type=-2'}
    foreach($line in $lines){
     $field,$value=$line -split '=',2
     if($field -in @('Type','Disabled','FilterType','Merit','MeritHi')){$entry.SetValue($field,[int]$value,[Microsoft.Win32.RegistryValueKind]::DWord)}
     else{$entry.SetValue($field,$value,[Microsoft.Win32.RegistryValueKind]::String)}
    }
   }finally{$entry.Dispose()}
  }
 }catch{
  foreach($name in $created){$list.DeleteSubKey($name,$false)}
  $list.Dispose();$playerProfileKey.DeleteSubKey('Override',$false);throw
 }finally{$list.Dispose()}
}
function Assert-RegistryCollectorList($playerProfileKey,[string]$Ax){
 $list=$playerProfileKey.OpenSubKey('Override')
 if(-not $list){throw '登记的滤镜列表已移除。'}
 try{
  $names=@($list.GetSubKeyNames() | Sort-Object)
  # Registry mode drops the trailing -2 sentinel when PotPlayer saves its preferences.
  if($list.ValueCount -or ($names -join ',') -notin @('0000','0000,0001')){throw '滤镜列表已有其他配置，请保留并检查。'}
  foreach($name in $names){
   $entry=$list.OpenSubKey($name)
   try{
    $lines=@(if($name -eq '0000'){Get-CollectorLines $Ax}else{'Type=-2'})
    if($entry.SubKeyCount -or $entry.ValueCount -ne $lines.Count){throw '滤镜登记字段已修改。'}
    foreach($line in $lines){
     $field,$value=$line -split '=',2
     $kind=if($field -in @('Type','Disabled','FilterType','Merit','MeritHi')){'DWord'}else{'String'}
     if($entry.GetValueKind($field).ToString() -ne $kind -or [string]$entry.GetValue($field) -ne $value){throw '滤镜登记值已修改。'}
    }
   }finally{$entry.Dispose()}
  }
 }finally{$list.Dispose()}
}
function Remove-RegistryCollectorList($playerProfileKey,[string]$Ax){
 Assert-RegistryCollectorList $playerProfileKey $Ax
 $list=$playerProfileKey.OpenSubKey('Override',$true)
 try{foreach($name in @('0000','0001')){$list.DeleteSubKey($name,$false)}}finally{$list.Dispose()}
 $playerProfileKey.DeleteSubKey('Override')
}
function Add-CollectorOverride([string]$Text,[string]$Ax){
 $blocks=@(Get-OverrideSections $Text)
 if($Text.IndexOf($collectorClsid,[StringComparison]::OrdinalIgnoreCase) -ge 0){throw '已有同 CLSID 的未登记滤镜。'}
 $nl=if($Text.Contains("`r`n")){"`r`n"}else{"`n"}
 $index=[Math]::Max(0,$blocks.Count-1)
 if($index -ge 9999){throw '滤镜列表已满。'}
 $header='[Override\{0:D4}]' -f $index
 $next='[Override\{0:D4}]' -f ($index+1)
 $entry=$header+$nl+((Get-CollectorLines $Ax) -join $nl)+$nl
 if($blocks.Count){
  $offset=$blocks[-1].Index
  return $Text.Remove($offset,$header.Length).Insert($offset,$entry+$next)
 }
 $sep=if($Text.EndsWith("`n")){''}else{$nl}
 return $Text+$sep+$entry+$next+$nl+'Type=-2'+$nl
}
function Remove-CollectorOverride([string]$Text,[string]$Ax,[bool]$HadTerminator){
 $blocks=@(Get-OverrideSections $Text)
 if($blocks.Count -lt 2){throw '登记的滤镜配置已变动，保留当前配置。'}
 $ours=$blocks[-2]
 $lines=@($ours.Groups['body'].Value.Trim() -split '\r?\n' | Where-Object {$_.Trim()} | Sort-Object)
 $expected=@(Get-CollectorLines $Ax | Sort-Object)
 if(($lines -join "`n") -ne ($expected -join "`n")){throw '登记的滤镜内容或顺序已修改，请检查后再卸载。'}
 $last=$blocks[-1]
 $result=if($HadTerminator){
  $Text.Remove($last.Index,("[Override\0000]").Length).Insert($last.Index,('[Override\{0:D4}]' -f ($blocks.Count-2)))
 }else{$Text.Remove($last.Index,$last.Length)}
 return $result.Remove($ours.Index,$ours.Length)
}
