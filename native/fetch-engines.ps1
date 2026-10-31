$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($version in @('17','16')) {
    $target = Join-Path $root "bin/engines/stockfish-$version"
    if (Test-Path -LiteralPath (Join-Path $target 'stockfish.exe')) { continue }
    $cache = Join-Path $PSScriptRoot "engine-cache/download-$version"
    New-Item -ItemType Directory -Force $cache | Out-Null
    $release = Invoke-RestMethod -Headers @{'User-Agent'='Mephisto-build'} -Uri "https://api.github.com/repos/official-stockfish/Stockfish/releases/tags/sf_$version"
    $asset = $release.assets | Where-Object { $_.name -eq 'stockfish-windows-x86-64.zip' } | Select-Object -First 1
    if (!$asset) { throw "Official Stockfish $version x64 asset not found" }
    $downloadUrl = (& curl.exe -I -L --silent --show-error --connect-timeout 10 --max-time 30 --output NUL --write-out '%{url_effective}' $asset.browser_download_url).Trim()
    if ($LASTEXITCODE -ne 0 -or !$downloadUrl.StartsWith('https://release-assets.githubusercontent.com/')) { throw 'Could not resolve the official engine download URL' }
    $chunkSize = 1048576
    $parts = @(); $requests = @(); $assemblies = @()
    for ($offset = 0L; $offset -lt $asset.size; $offset += $chunkSize) {
        $end = [Math]::Min($offset + $chunkSize - 1, $asset.size - 1)
        $part = Join-Path $cache ("part-{0:D4}" -f $parts.Count)
        $length = $end - $offset + 1
        $parts += [pscustomobject]@{ Path=$part; Length=$length }
        if ((Test-Path -LiteralPath $part) -and (Get-Item -LiteralPath $part).Length -eq $length) { continue }
        $segments=@()
        for($start=$offset;$start -le $end;$start+=262144) {
            $segmentEnd=[Math]::Min($start+262143,$end)
            $segment="$part-segment-$start"
            $segments += [pscustomobject]@{Path=$segment;Length=$segmentEnd-$start+1}
            if((Test-Path -LiteralPath $segment) -and (Get-Item -LiteralPath $segment).Length -eq ($segmentEnd-$start+1)){continue}
            $requests += @(('url = "' + $downloadUrl + '&mephisto_part=' + $start + '"'),
                ('range = "' + $start + '-' + $segmentEnd + '"'), ('output = "' + $segment.Replace('\','/') + '"'),
                'location', 'fail', 'http1.1', 'connect-timeout = 15', 'max-time = 90', 'retry = 2', 'next')
        }
        $assemblies += [pscustomobject]@{Path=$part;Segments=$segments}
    }
    if ($requests.Count) {
        $config = Join-Path $cache 'curl.cfg'
        [IO.File]::WriteAllLines($config, $requests[0..($requests.Count-2)], [Text.Encoding]::ASCII)
        Write-Host "Downloading official Stockfish $version ($($asset.size) bytes)..."
        & curl.exe --parallel --parallel-immediate --parallel-max 16 --silent --show-error --config $config
        if ($LASTEXITCODE -ne 0) { throw "Stockfish $version download failed. Run again to resume completed parts." }
    }
    foreach($assembly in $assemblies) {
        $partOutput=[IO.File]::Create($assembly.Path)
        try {foreach($segment in $assembly.Segments){
            if((Get-Item -LiteralPath $segment.Path).Length -ne $segment.Length){throw "Invalid engine segment: $($segment.Path)"}
            $segmentInput=[IO.File]::OpenRead($segment.Path);try{$segmentInput.CopyTo($partOutput)}finally{$segmentInput.Dispose()}
        }}finally{$partOutput.Dispose()}
    }
    $archive = Join-Path $cache 'engine.zip'
    $output = [IO.File]::Create($archive)
    try {
        foreach ($part in $parts) {
            if ((Get-Item -LiteralPath $part.Path).Length -ne $part.Length) { throw "Invalid engine download part: $($part.Path)" }
            $inputFile = [IO.File]::OpenRead($part.Path)
            try { $inputFile.CopyTo($output) } finally { $inputFile.Dispose() }
        }
    } finally { $output.Dispose() }
    $unpacked = Join-Path $cache 'unpacked'
    Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
    $engine = Get-ChildItem -LiteralPath $unpacked -Recurse -Filter '*.exe' | Select-Object -First 1
    if (!$engine) { throw "No native executable in Stockfish $version archive" }
    New-Item -ItemType Directory -Force $target | Out-Null
    Get-ChildItem -LiteralPath $engine.Directory.FullName | Where-Object { $_.FullName -ne $engine.FullName } | Copy-Item -Destination $target -Recurse -Force
    Copy-Item -LiteralPath $engine.FullName -Destination (Join-Path $target 'stockfish.exe') -Force
    [IO.File]::WriteAllText((Join-Path $target 'SOURCE.txt'), "Official Stockfish sf_$version`r`n$($asset.browser_download_url)`r`nhttps://github.com/official-stockfish/Stockfish/tree/sf_$version`r`n")
}
