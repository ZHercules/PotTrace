$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Describe 'Windows PowerShell entry points' {
    It 'verifies pinned DirectShow sources and rejects changed inputs' {
        $verifier=Join-Path $repo 'scripts\verify-directshow.ps1'
        (Test-Path -LiteralPath $verifier) | Should Be $true
        $source=Join-Path $TestDrive 'baseclasses'
        New-Item -ItemType Directory -Path $source -Force | Out-Null
        Set-Content (Join-Path $source 'sample.h') 'known source'
        Set-Content (Join-Path $source 'LICENSE') 'known license'
        $manifest=Join-Path $TestDrive 'sources.json'
        @{commit='434f6002bdf9cf9829406c3ff2b33387982d6168';licenseSha256=(Get-FileHash (Join-Path $source 'LICENSE')).Hash;files=@(@{name='sample.h';sha256=(Get-FileHash (Join-Path $source 'sample.h')).Hash})} | ConvertTo-Json -Depth 4 | Set-Content $manifest -Encoding UTF8
        {& $verifier -SourceDirectory $source -ManifestPath $manifest} | Should Not Throw
        Add-Content (Join-Path $source 'sample.h') 'changed'
        {& $verifier -SourceDirectory $source -ManifestPath $manifest} | Should Throw
    }
    It 'parses installer and publishing scripts without encoding errors' {
        foreach($relative in @('packaging\Install.ps1','packaging\Uninstall.ps1','scripts\publish.ps1')) {
            $tokens=$null;$parseErrors=$null
            [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $relative),[ref]$tokens,[ref]$parseErrors)
            @($parseErrors).Count | Should Be 0
        }
    }
    It 'uses the repository-local SDK in a normal checkout' {
        $checkout=Join-Path $TestDrive 'normal-checkout'
        New-Item -ItemType Directory -Path (Join-Path $checkout 'scripts'),(Join-Path $checkout '.tools\dotnet') -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo 'scripts\dotnet.ps1') -Destination (Join-Path $checkout 'scripts\dotnet.ps1')
        # A known executable witnesses which SDK path was actually launched.
        Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\where.exe') -Destination (Join-Path $checkout '.tools\dotnet\dotnet.exe')
        $output=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $checkout 'scripts\dotnet.ps1') powershell.exe 2>&1
        $LASTEXITCODE | Should Be 0
        ($output -join "`n") | Should Match 'WindowsPowerShell.*powershell.exe'
    }
}
Describe 'PotTrace owned installation' {
    BeforeEach {
        $caseRoot = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
        $player = Join-Path $caseRoot 'Player'
        $package = Join-Path $caseRoot 'Package'
        $install = Join-Path $caseRoot 'Programs'
        $data = Join-Path $caseRoot 'Data'
        New-Item -ItemType Directory -Path $player,(Join-Path $package 'app'),(Join-Path $package 'plugin'),$data -Force | Out-Null
        Set-Content (Join-Path $player 'PotPlayerMini64.exe') 'fixture'
        Set-Content (Join-Path $player 'user-file.txt') 'keep'
        Set-Content (Join-Path $data 'pottrace.db') 'user history'
        Set-Content (Join-Path $package 'app\PotTrace.exe') 'application'
        Set-Content (Join-Path $package 'plugin\PotTrace.Bridge.dll') 'bridge'
        Set-Content (Join-Path $package 'plugin\PlaybackStatistics - PotTrace.as') 'script'
        $files = Get-ChildItem $package -File -Recurse | ForEach-Object { @{ Path=$_.FullName.Substring($package.Length+1).Replace('\','/'); Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } }
        @{Version='0.1.0';Files=@($files)} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $package 'manifest.json')
    }
    It 'installs owned files and preserves unrelated files and history' {
        & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        Test-Path (Join-Path $player 'Extension\Playback\Statistics\PotTrace.Bridge.dll') | Should Be $true
        Get-Content (Join-Path $player 'user-file.txt') | Should Be 'keep'
        Get-Content (Join-Path $data 'pottrace.db') | Should Be 'user history'
    }
    It 'upgrades without resetting settings and uninstalls only owned files' {
        & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        Set-Content (Join-Path $data 'settings.json') '{"RecordingPaused":true}'
        & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        Get-Content (Join-Path $data 'settings.json') | Should Be '{"RecordingPaused":true}'
        Test-Path (Join-Path $player 'Extension\Playback\Statistics\PotTrace.Bridge.dll') | Should Be $true
        & (Join-Path $repo 'packaging\Uninstall.ps1') -PotPlayerPath $player
        Test-Path (Join-Path $player 'Extension\Playback\Statistics\PotTrace.Bridge.dll') | Should Be $false
        Get-Content (Join-Path $data 'pottrace.db') | Should Be 'user history'
        Get-Content (Join-Path $player 'user-file.txt') | Should Be 'keep'
    }
    It 'restores the preview configuration and removes AX if collector upgrade fails after INI edit' {
        $installer=Join-Path $repo 'packaging\Install.ps1'
        & $installer -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        $pluginDir=Join-Path $player 'Extension\Playback\Statistics'
        $registration=Get-Item (Join-Path $pluginDir 'pottrace-install.json')
        $bridgeIni=Get-Content (Join-Path $pluginDir 'pottrace-bridge.ini') -Raw
        $ini=Join-Path $player 'PotPlayerMini64.ini'
        Set-Content $ini "[Settings]`r`nVolume=42" -Encoding Unicode
        $iniHash=(Get-FileHash $ini).Hash
        $ax=Join-Path $package 'plugin\PotTrace.Collector.ax'
        Set-Content $ax 'fixture'
        $manifest=Get-Content (Join-Path $package manifest.json) -Raw | ConvertFrom-Json
        $manifest | Add-Member NoteProperty CaptureBackend DirectShow
        $manifest.Files+=@{Path='plugin/PotTrace.Collector.ax';Sha256=(Get-FileHash $ax).Hash}
        $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $package manifest.json)
        Mock Get-Item { @{VersionInfo=@{FileVersion='1, 7, 22859, 0'}} } -ParameterFilter {$LiteralPath -like '*PotPlayer64.dll'}
        $registration.IsReadOnly=$true
        try{
            $failure=$null
            try{& $installer -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data}catch{$failure=$_.Exception.ToString()}
            $failure | Should Match 'pottrace-install\.json'
            (Get-FileHash $ini).Hash | Should Be $iniHash
            Get-Content (Join-Path $pluginDir 'pottrace-bridge.ini') -Raw | Should Be $bridgeIni
            Test-Path (Join-Path $pluginDir 'PotTrace.Collector.ax') | Should Be $false
            Test-Path (Join-Path $pluginDir 'pottrace-collector.ini') | Should Be $false
        }finally{$registration.IsReadOnly=$false}
    }
    It 'rejects a changed package before writing any plugin file' {
        Add-Content (Join-Path $package 'plugin\PotTrace.Bridge.dll') 'tampered'
        $failure=$null
        try { & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data } catch { $failure=$_.Exception.Message }
        $failure | Should Match '校验失败'
        Test-Path (Join-Path $player 'Extension\Playback\Statistics\PotTrace.Bridge.dll') | Should Be $false
    }
    It 'removes owned recovery copies only when history removal is explicit' {
        & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        $recovery=Join-Path $data ('pottrace.db.before-restore-'+[Guid]::NewGuid().ToString('N'))
        Set-Content $recovery 'owned recovery'
        $unrelated=Join-Path $data 'pottrace.db.before-restore-my-notes.txt'
        Set-Content $unrelated 'keep'
        & (Join-Path $repo 'packaging\Uninstall.ps1') -PotPlayerPath $player -RemoveHistory
        Test-Path $recovery | Should Be $false
        Get-Content $unrelated | Should Be 'keep'
    }
    It 'rolls back an upgrade when a plugin file is read-only' {
        & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data
        $bridge=Join-Path $player 'Extension\Playback\Statistics\PotTrace.Bridge.dll'
        $config=Join-Path $player 'Extension\Playback\Statistics\pottrace-bridge.ini'
        $before=Get-Content $config -Raw
        (Get-Item $bridge).IsReadOnly=$true
        try {
            $failure=$null
            try { & (Join-Path $repo 'packaging\Install.ps1') -PotPlayerPath $player -PackagePath $package -InstallRoot $install -DataRoot $data } catch { $failure=$_.Exception.Message }
            [bool]$failure | Should Be $true
            (Get-Content $config -Raw) | Should Be $before
            @(Get-ChildItem (Join-Path $install 'versions') -Filter PotTrace.exe -File -Recurse).Count | Should Be 1
        } finally { (Get-Item $bridge).IsReadOnly=$false }
    }
}

Describe 'DirectShow private filter configuration' {
    . (Join-Path $repo 'packaging\CollectorConfig.ps1')
    $ax='D:\隔离 播放器\PotTrace.Collector.ax'
    It 'installs registry filters with typed values and refuses to remove changed entries' {
        $testKey='Software\PotTrace-FilterTest-'+[guid]::NewGuid().ToString('N')
        $owner=[Microsoft.Win32.Registry]::CurrentUser
        $tempKey=$owner.CreateSubKey($testKey)
        try {
            New-RegistryCollectorList $tempKey $ax
            Assert-RegistryCollectorList $tempKey $ax
            {New-RegistryCollectorList $tempKey $ax} | Should Throw
            $tempEntry=$tempKey.OpenSubKey('Override\0000',$true)
            try {
                $tempEntry.SetValue('Name','changed')
                {Remove-RegistryCollectorList $tempKey $ax} | Should Throw
                $tempEntry.SetValue('Name','PotTrace Collector')
            }finally{$tempEntry.Dispose()}
            $tempList=$tempKey.OpenSubKey('Override',$true)
            try{$tempList.DeleteSubKey('0001')}finally{$tempList.Dispose()}
            Remove-RegistryCollectorList $tempKey $ax
            $tempKey.SubKeyCount | Should Be 0
        }finally{$tempKey.Dispose();$owner.DeleteSubKeyTree($testKey)}
    }
    It 'restores existing numbered filters and preserves unrelated settings changes' {
        $before="[Settings]`r`nVolume=30`r`n[Override\0000]`r`nType=1`r`nName=Existing`r`n[Override\0001]`r`nType=-2`r`n[Other]`r`nKey=value`r`n"
        $installed=Add-CollectorOverride $before $ax
        (Remove-CollectorOverride ($installed.Replace('Volume=30','Volume=40')) $ax $true) | Should Be ($before.Replace('Volume=30','Volume=40'))
    }
    It 'does not remove an externally modified collector or accept duplicate registration' {
        $installed=Add-CollectorOverride "[Settings]`n" $ax
        {Remove-CollectorOverride ($installed.Replace('Disabled=0','Disabled=1')) $ax $false} | Should Throw
        {Add-CollectorOverride $installed $ax} | Should Throw
    }
    It 'requires an existing supported INI and refuses an unnumbered override section' {
        {Read-PlayerIni (Join-Path $TestDrive 'not-present.ini')} | Should Throw
        {Add-CollectorOverride "[Override\Custom]`nType=-2`n" $ax} | Should Throw
    }
}
