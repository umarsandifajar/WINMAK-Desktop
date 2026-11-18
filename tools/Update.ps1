# Launched only after the user chooses to restart. Runs outside the install folder.
param(
    [Parameter(Mandatory)][string]$InstallDirectory,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$StatePath,
    [Parameter(Mandatory)][int]$WaitForProcessId,
    [ValidateSet('0', '1')][string]$StartCore = '1',
    [ValidateRange(1, 60)][int]$StartupTimeoutSeconds = 20
)
$ErrorActionPreference = 'Stop'
$install = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
$package = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\')
$StatePath = [IO.Path]::GetFullPath($StatePath)
$cache = Split-Path -Parent $StatePath
$parent = Split-Path -Parent $install
$name = Split-Path -Leaf $install
$suffix = [Guid]::NewGuid().ToString('N')
$incoming = Join-Path $parent "$name.update-$suffix"
$backup = Join-Path $parent "$name.previous-$suffix"
$result = Join-Path $cache 'result.json'
$coreStopped = $false
$swapped = $false
$confirmed = $false
$restored = $false
$started = New-Object 'System.Collections.Generic.List[System.Diagnostics.Process]'
# Never the install folder: what starts there would hold it, and the next update could not replace it.
$workingDirectory = [Environment]::GetFolderPath('UserProfile')

function Assert-TemporaryPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ((Split-Path -Parent $absolute) -ne $parent -or
        ((Split-Path -Leaf $absolute) -ne "$name.update-$suffix" -and
         (Split-Path -Leaf $absolute) -ne "$name.previous-$suffix")) { throw 'æ›´æ–°ä¸´æ—¶ç›®å½•æ— æ•ˆã€‚' }
}

# Moves what is in $From into $To (merging folders). A folder some process is in (its current
# directory, e.g. an app started from an older Atrium) cannot be moved itself: its contents go, it stays.
function Move-Tree([string]$From, [string]$To, [switch]$Keep) {
    if (!(Test-Path -LiteralPath $To)) { New-Item -ItemType Directory -Path $To | Out-Null }
    foreach ($item in @(Get-ChildItem -LiteralPath $From -Force)) {
        $target = Join-Path $To $item.Name
        if ($item.PSIsContainer -and (Test-Path -LiteralPath $target)) { Move-Tree $item.FullName $target; continue }
        if ($item.PSIsContainer) {
            try { Move-Item -LiteralPath $item.FullName -Destination $target }
            catch { Move-Tree $item.FullName $target }
            continue
        }
        # A file just written may be open for a moment (a virus scanner reading it).
        for ($try = 1; ; $try++) {
            try { Move-Item -LiteralPath $item.FullName -Destination $target; break }
            catch {
                if ($try -lt 6) { Start-Sleep -Milliseconds 300; continue }
                throw "æ— æ³•æ›¿æ¢ $($item.FullName)ï¼ˆ$($_.Exception.Message.TrimEnd('ã€‚', '.'))ï¼‰ï¼Œè¯·å…³é—­æ­£åœ¨ä½¿ç”¨å®ƒçš„ç¨‹åºåŽé‡è¯•ã€‚"
            }
        }
    }
    if (!$Keep) {
        try { Remove-Item -LiteralPath $From -Force } catch { }
    }
}

# What settings wrote to its crash log since $Since (its exception's first line), for the message.
function Get-CrashCause([datetime]$Since) {
    $log = Join-Path $env:LOCALAPPDATA 'Atrium\logs\settings.log'
    try {
        if (!(Test-Path -LiteralPath $log) -or (Get-Item -LiteralPath $log).LastWriteTime -lt $Since) { return '' }
        $last = Get-Content -LiteralPath $log -Tail 60 -Encoding UTF8 | Where-Object { $_ -match '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ' } | Select-Object -Last 1
        if (!$last) { return '' }
        $cause = $last.Substring(20).Trim()
        if ($cause.Length -gt 200) { $cause = $cause.Substring(0, 200) + '...' }
        return "ï¼š$cause"
    } catch { return '' }
}

# Smart App Control or an App Control (WDAC) policy refused a file: ERROR_SYSTEM_INTEGRITY_* 4550-4559 and
# 4580-4582. Windows PowerShell's Start-Process keeps only the system message, not the code, so compare that.
function Test-AppControlBlock([string]$Message) {
    foreach ($code in @(4550..4559) + @(4580..4582)) {
        $text = ([ComponentModel.Win32Exception]::new($code)).Message.Trim().TrimEnd('ã€‚', '.')
        if ($text -and $Message.Contains($text)) { return $true }
    }
    return $false
}

function Start-Verified([string]$Executable, [string]$Kind, [string]$Arguments) {
    $eventName = "Local\Atrium.Update.$Kind.$suffix"
    $ready = New-Object System.Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, $eventName)
    try {
        $startedAt = (Get-Date).AddSeconds(-1)
        try {
            $process = Start-Process -FilePath $Executable -ArgumentList "$Arguments --ready-event $eventName" -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
        } catch {
            if (!(Test-AppControlBlock $_.Exception.Message)) { throw }
            # The release is not code-signed; each version's new files have no reputation yet.
            throw ("Windows åº”ç”¨ç¨‹åºæŽ§åˆ¶é˜»æ­¢äº†æ–°ç‰ˆ $Kind è¿è¡Œï¼Œå·²ä¿ç•™æ—§ç‰ˆæœ¬ã€‚æ–°ç‰ˆæ–‡ä»¶æ²¡æœ‰ä»£ç ç­¾åï¼Œå¯èƒ½è¢«æ™ºèƒ½åº”ç”¨æŽ§åˆ¶æˆ–å•ä½çš„åº”ç”¨æŽ§åˆ¶ç­–ç•¥æ‹¦æˆªï¼š" +
                   "å¯åœ¨ Windows å®‰å…¨ä¸­å¿ƒ â€º åº”ç”¨å’Œæµè§ˆå™¨æŽ§åˆ¶ â€º æ™ºèƒ½åº”ç”¨æŽ§åˆ¶ä¸­æŸ¥çœ‹ï¼›å•ä½ç”µè„‘è¯·è”ç³»ç®¡ç†å‘˜æ”¾è¡Œ Atrium æ‰€åœ¨ç›®å½•ã€‚ï¼ˆ$($_.Exception.Message.Trim())ï¼‰")
        }
        $started.Add($process)
        $until = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
        do {
            if ($process.HasExited) {
                $cause = if ($Kind -eq 'Settings') { Get-CrashCause $startedAt } else { '' }
                throw ("$Kind å¯åŠ¨å¤±è´¥ï¼ˆé€€å‡ºç  0x{0:X8}ï¼‰{1}ã€‚" -f $process.ExitCode, $cause)
            }
            if ($ready.WaitOne(100)) {
                if ($process.HasExited) { throw "$Kind åœ¨å°±ç»ªåŽé€€å‡ºã€‚" }
                return
            }
        } while ([DateTime]::UtcNow -lt $until)
        throw "$Kind æœªç¡®è®¤å¯åŠ¨å°±ç»ªï¼Œå·²ä¿ç•™æ—§ç‰ˆæœ¬ã€‚"
    } finally { $ready.Dispose() }
}

function Stop-NewProcesses {
    foreach ($process in $started) {
        if (!$process.HasExited) {
            [void]$process.CloseMainWindow()
            if (!$process.WaitForExit(3000)) { Stop-Process -Id $process.Id -Force }
            [void]$process.WaitForExit(3000)
        }
    }
}

function Write-Result([string]$ErrorText) {
    $temporary = $result + '.tmp'
    @{ error = $ErrorText } | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $result -Force
}

try {
    if (!$parent -or !$name -or $install -eq [IO.Path]::GetPathRoot($install).TrimEnd('\') -or
        !$package.StartsWith($cache + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'æ›´æ–°ç›®å½•æ— æ•ˆã€‚' }
    Assert-TemporaryPath $incoming
    Assert-TemporaryPath $backup
    foreach ($root in @($install, $package)) {
        if (!(Test-Path -LiteralPath (Join-Path $root 'WINMAK.exe')) -or
            !(Test-Path -LiteralPath (Join-Path $root 'Settings\Atrium.Settings.exe'))) { throw 'æ‰¾ä¸åˆ°å®Œæ•´çš„ Atrium ç¨‹åºæ–‡ä»¶ã€‚' }
    }
    # Copy before stopping the app; disk/access failures leave the old app running.
    New-Item -ItemType Directory -Path $incoming | Out-Null
    Get-ChildItem -LiteralPath $package -Force | Copy-Item -Destination $incoming -Recurse -Force
    # A downloaded release must retain the development package's explicit identity.
    if (Test-Path -LiteralPath (Join-Path $install 'development.txt')) {
        Copy-Item -LiteralPath (Join-Path $install 'development.txt') -Destination $incoming -Force
    }
    $settingsProcess = Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
    if ($settingsProcess -and !$settingsProcess.WaitForExit(30000)) { throw 'è®¾ç½®ä¸­å¿ƒå°šæœªé€€å‡ºï¼Œæ›´æ–°æœªå®‰è£…ã€‚' }
    $quit = Start-Process -FilePath (Join-Path $install 'WINMAK.exe') -ArgumentList '--exit' -WindowStyle Hidden -PassThru
    try {
        if (!$quit.WaitForExit(15000) -or $quit.ExitCode -ne 0) { throw 'Atrium å°šæœªæ­£å¸¸é€€å‡ºï¼Œæ›´æ–°æœªå®‰è£…ã€‚' }
    } finally { $quit.Dispose() }
    $coreStopped = $true
    Assert-TemporaryPath $backup
    Assert-TemporaryPath $incoming
    # The folder itself stays (an app may be in it); what is in it is replaced.
    try { Move-Tree $install $backup -Keep }
    catch {
        Move-Tree $backup $install
        $restored = $true
        throw
    }
    try { Move-Tree $incoming $install }
    catch {
        Move-Tree $install $incoming -Keep
        Move-Tree $backup $install
        $restored = $true
        throw
    }
    $swapped = $true
    if ($StartCore -eq '1') { Start-Verified (Join-Path $install 'WINMAK.exe') 'Core' '' }
    $settingsArguments = if ($StartCore -eq '1') { '--page about --require-core-ready' } else { '--page about' }
    Start-Verified (Join-Path $install 'Settings\Atrium.Settings.exe') 'Settings' $settingsArguments
    foreach ($process in $started) {
        if ($process.HasExited) { throw 'æ–°ç‰ˆç¨‹åºåœ¨å¯åŠ¨ç¡®è®¤æœŸé—´é€€å‡ºã€‚' }
    }
    # With the core intentionally off, only settings is launched and verified.
    $confirmed = $true
    Remove-Item -LiteralPath $StatePath -Force
    Write-Result ''
} catch {
    $failure = $_.Exception.Message
    if ($swapped -and !$confirmed) {
        try {
            Stop-NewProcesses
            Assert-TemporaryPath $incoming
            Move-Tree $install $incoming -Keep
            Assert-TemporaryPath $backup
            Move-Tree $backup $install
            $restored = $true
        } catch { $failure += " æ¢å¤æ—§åŒ…æœªå®Œæˆï¼š$($_.Exception.Message)ï¼›æ¢å¤å‰¯æœ¬ä¿ç•™åœ¨ $backupã€‚" }
    }
    Write-Result $failure
} finally {
    # Never delete an unconfirmed recovery point, even when rollback itself failed.
    $cleanup = @($incoming)
    if ($confirmed) { $cleanup += $backup }
    foreach ($temporary in $cleanup) {
        Assert-TemporaryPath $temporary
        if (Test-Path -LiteralPath $temporary) {
            try { Remove-Item -LiteralPath $temporary -Recurse -Force } catch { Write-Warning $_.Exception.Message }
        }
    }
    if (!$confirmed -and (!$swapped -or $restored)) {
        if ($coreStopped -and $StartCore -eq '1') {
            Start-Process -FilePath (Join-Path $install 'WINMAK.exe') -WorkingDirectory $workingDirectory -WindowStyle Hidden
        }
        Start-Process -FilePath (Join-Path $install 'Settings\Atrium.Settings.exe') -ArgumentList '--page about' -WorkingDirectory $workingDirectory -WindowStyle Hidden
    }
    if ($confirmed) {
        $work = $package
        while ($work.StartsWith($cache + '\', [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Parent $work) -ne $cache) {
            $work = Split-Path -Parent $work
        }
        $absoluteWork = [IO.Path]::GetFullPath($work)
        if ((Split-Path -Parent $absoluteWork) -eq $cache -and (Split-Path -Leaf $absoluteWork) -match '^package-(background|[0-9a-f]{32})$') {
            try { Remove-Item -LiteralPath $absoluteWork -Recurse -Force } catch { Write-Warning $_.Exception.Message }
        }
    }
    foreach ($process in $started) { $process.Dispose() }
}
