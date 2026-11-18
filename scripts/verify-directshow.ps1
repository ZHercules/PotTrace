param(
 [string]$SourceDirectory=(Join-Path $PSScriptRoot '..\out\deps\directshow-baseclasses'),
 [string]$ManifestPath=(Join-Path $PSScriptRoot '..\native\PotTrace.Collector\baseclasses.lock.json')
)
$ErrorActionPreference='Stop'
$manifest=Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.commit -ne '434f6002bdf9cf9829406c3ff2b33387982d6168'){throw 'Unexpected DirectShow source revision'}
$names=@{}
foreach($file in $manifest.files){
 if([IO.Path]::GetFileName($file.name) -ne $file.name -or $file.name -match '[:\\/]' -or $names.ContainsKey($file.name)){throw 'Unsafe or duplicate dependency name'}
 $names[$file.name]=$true
 $path=Join-Path $SourceDirectory $file.name
 if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing pinned DirectShow source: $path. Run probes/DirectShowProbe/fetch_baseclasses.py first."}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256){throw "Changed DirectShow source: $($file.name)"}
}
if((Get-FileHash -LiteralPath (Join-Path $SourceDirectory 'LICENSE') -Algorithm SHA256).Hash -ne $manifest.licenseSha256){throw 'Changed dependency license'}
foreach($file in Get-ChildItem -LiteralPath $SourceDirectory -File){
 if($file.Extension -in '.cpp','.h' -and -not $names.ContainsKey($file.Name)){throw "Unpinned compilation input: $($file.Name)"}
}
Write-Output "Pinned DirectShow inputs verified: $($names.Count) files and license"
