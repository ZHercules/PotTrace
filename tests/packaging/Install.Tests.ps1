$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Describe 'Windows PowerShell entry points' {
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
