$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$base = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6100/qt6_6100'
$cache = Join-Path $root 'build/qt-cpp-downloads'
$target = Join-Path $root '.toolchains/qt'
New-Item -ItemType Directory -Force $cache,$target | Out-Null
$index = Join-Path $cache 'Updates.xml'
Invoke-WebRequest -UseBasicParsing -Uri "$base/Updates.xml" -OutFile $index
[xml]$metadata = Get-Content $index -Raw
$package = $metadata.Updates.PackageUpdate | Where-Object Name -EQ 'qt.qt6.6100.win64_msvc2022_64'
if (-not $package) { throw 'Official Qt MSVC package not found' }
foreach ($archive in ($package.DownloadableArchives -split ',\s*' | Where-Object { $_ -match '^qt(base|tools)-' })) {
    $url = "$base/$($package.Name)/$($package.Version)$archive"
    $path = Join-Path $cache $archive
    if (-not (Test-Path $path)) { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $path }
    Invoke-WebRequest -UseBasicParsing -Uri "$url.sha1" -OutFile "$path.sha1"
    $expected = (Get-Content "$path.sha1" -Raw).Trim().Split(' ')[0]
    if ((Get-FileHash $path -Algorithm SHA1).Hash -ine $expected) { throw "Qt archive checksum mismatch: $archive" }
    Push-Location $target
    try { & cmake -E tar xf $path; if ($LASTEXITCODE) { throw 'Qt extraction failed' } } finally { Pop-Location }
    Write-Output "Installed verified official Qt archive: $archive"
}
