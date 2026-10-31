param([ValidateSet('chrome','firefox')][string]$Browser = 'chrome', [switch]$PackageOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$install) { throw 'Install Visual Studio C++ Build Tools (Desktop development with C++).' }
$msbuild = Join-Path $install 'MSBuild/Current/Bin/MSBuild.exe'
$out = Join-Path $root $(if ($Browser -eq 'firefox') { 'desktop-firefox' } else { 'desktop' })
New-Item -ItemType Directory -Force $out | Out-Null
if (!$PackageOnly) {
    & (Join-Path $PSScriptRoot 'fetch-engines.ps1')
    & $msbuild (Join-Path $PSScriptRoot 'Mephisto.vcxproj') /restore /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$out/" /verbosity:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $LASTEXITCODE" }
} elseif (!(Test-Path -LiteralPath (Join-Path $out 'Mephisto.exe'))) { throw 'Build Mephisto.exe before using -PackageOnly.' }
Copy-Item -LiteralPath (Join-Path $root 'bin/stockfish.exe') -Destination (Join-Path $out 'stockfish.exe') -Force
if (Test-Path -LiteralPath (Join-Path $root 'bin/engines')) {
    New-Item -ItemType Directory -Force (Join-Path $out 'engines') | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $root 'bin/engines') | Copy-Item -Destination (Join-Path $out 'engines') -Recurse -Force
}
$core = Join-Path $out 'browser-core'
$extensionFiles = @('manifest.json','lib/chess.js','src/scripts/desktop-core.js','src/scripts/desktop-background.js','src/popup/desktop.html','res/icons/icon16.png','res/icons/icon32.png','res/icons/icon48.png','res/icons/icon128.png')
if ($Browser -ne 'firefox') { $extensionFiles += '_locales/en/messages.json' }
foreach ($relative in $extensionFiles) {
    $destination = Join-Path $core $relative
    New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'QUICKSTART.md') -Destination (Join-Path $out 'README.md') -Force
$archiveName = 'Mephisto-Desktop.zip'
if ($Browser -eq 'firefox') {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $manifest = Get-Content -LiteralPath (Join-Path $core 'manifest.json') -Raw | ConvertFrom-Json
    $manifest.PSObject.Properties.Remove('update_url')
    # All manifest strings are literal; Firefox needs no locale loading at startup.
    $manifest.PSObject.Properties.Remove('default_locale')
    $manifest.version = '3.0.3'
    $obsoleteLocale = Join-Path $core '_locales/en/messages.json'
    if (Test-Path -LiteralPath $obsoleteLocale) { Remove-Item -LiteralPath $obsoleteLocale }
    foreach ($relativeDirectory in @('_locales/en','_locales')) {
        $localeDirectory = Join-Path $core $relativeDirectory
        if ((Test-Path -LiteralPath $localeDirectory) -and @(Get-ChildItem -LiteralPath $localeDirectory -Force).Count -eq 0) {
            [System.IO.Directory]::Delete($localeDirectory, $false)
        }
    }
    $manifest.background = [ordered]@{ page = 'src/scripts/desktop-background.html'; persistent = $false }
    $manifest | Add-Member -NotePropertyName browser_specific_settings -NotePropertyValue ([ordered]@{
        gecko = [ordered]@{ id = 'mephisto-desktop@local'; strict_min_version = '140.0';
            data_collection_permissions = @{ required = @('browsingActivity','websiteContent') } }
    })
    [System.IO.File]::WriteAllText((Join-Path $core 'manifest.json'), ($manifest | ConvertTo-Json -Depth 20), $utf8)
    [System.IO.File]::WriteAllText((Join-Path $core 'src/scripts/desktop-background.html'), '<!doctype html><html><head><meta charset="utf-8"><script type="module" src="desktop-background.js"></script></head><body></body></html>', $utf8)
    $extensionFiles += 'src/scripts/desktop-background.html'
    foreach ($script in @('desktop-core.js','desktop-background.js')) {
        $target = Join-Path $core "src/scripts/$script"
        $source = [System.IO.File]::ReadAllText($target).Replace('chrome.runtime', 'browser.runtime').Replace('chrome.tabs', 'browser.tabs')
        [System.IO.File]::WriteAllText($target, $source, $utf8)
    }
    $xpi = Join-Path $out 'Mephisto-Firefox-addon.xpi'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Add-Type -AssemblyName System.IO.Compression
    $tempArchive = Join-Path $out ('addon-' + [guid]::NewGuid().ToString('N') + '.zip')
    try {
        # Explicit paths keep ZIP entries portable and exclude stale staging files.
        $zip = [System.IO.Compression.ZipFile]::Open($tempArchive, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($relative in $extensionFiles) {
                $entryName = $relative.Replace('\','/')
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $core $relative), $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally { $zip.Dispose() }
        $zip = [System.IO.Compression.ZipFile]::OpenRead($tempArchive)
        try {
            foreach ($relative in $extensionFiles) {
                if (!$zip.GetEntry($relative.Replace('\','/'))) { throw "Missing XPI entry: $relative" }
            }
            if ($zip.GetEntry('_locales/en/messages.json')) { throw 'Firefox package must not depend on unused locale files.' }
        } finally { $zip.Dispose() }
        Move-Item -LiteralPath $tempArchive -Destination $xpi -Force
    } finally { if (Test-Path -LiteralPath $tempArchive) { Remove-Item -LiteralPath $tempArchive } }
    $archiveName = 'Mephisto-Desktop-Firefox.zip'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FIREFOX.md') -Destination (Join-Path $out 'FIREFOX.md') -Force
}
# Keep application runtime and browser payload as separate deliverables.
$delivery = Join-Path $root "build/$Browser"
New-Item -ItemType Directory -Force $delivery | Out-Null
$archiveItems = Get-ChildItem -LiteralPath $out | Where-Object { $_.Name -notmatch '\.(obj|pdb|lib|exp|ilk|cmd|xpi)$' -and $_.Name -notin @('mephisto-settings.json','browser-core','startup-error.txt') } | Select-Object -ExpandProperty FullName
$programArchive = Join-Path $delivery 'Mephisto-Program.zip'
Compress-Archive -Path $archiveItems -DestinationPath $programArchive -Force
if($Browser -eq 'firefox') {
    Copy-Item -LiteralPath $xpi -Destination (Join-Path $delivery 'Mephisto-Firefox-addon.xpi') -Force
} else {
    $extension = Join-Path $delivery 'extension'
    New-Item -ItemType Directory -Force $extension | Out-Null
    Get-ChildItem -LiteralPath $core | Copy-Item -Destination $extension -Recurse -Force
}
Write-Host "Built: $out/Mephisto.exe"
Write-Host "Program: $programArchive"
Write-Host "Browser payload: $delivery"
