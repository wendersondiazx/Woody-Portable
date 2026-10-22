param([switch]$ConfigureOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$qt=Join-Path $root '.toolchains/qt'
if (-not (Test-Path "$qt/lib/cmake/Qt6/Qt6Config.cmake")) { throw 'Run converter/setup-qt.ps1 first' }
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $vs) { throw 'MSVC C++ build tools are required' }
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$build=Join-Path $root 'build/converter-1.1.0'
New-Item -ItemType Directory -Force $build | Out-Null
$commands=@('@echo off',"call `"$vcvars`" >nul",'if errorlevel 1 exit /b 1',
    "cmake -S `"$root/converter`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$qt`" -DCMAKE_MAKE_PROGRAM=`"$ninja`"",'if errorlevel 1 exit /b 1')
if (-not $ConfigureOnly) {
    $commands+=@("cmake --build `"$build`" --parallel 2",'if errorlevel 1 exit /b 1',
        "`"$qt/bin/windeployqt.exe`" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime `"$build/WoodyConverter.exe`"",'if errorlevel 1 exit /b 1',
        "ctest --test-dir `"$build`" --output-on-failure --timeout 30",'if errorlevel 1 exit /b 1')
}
$batch=Join-Path $build 'build.cmd'
[IO.File]::WriteAllLines($batch,$commands,[Text.Encoding]::Default)
$buildProcess=Start-Process -FilePath $env:ComSpec -ArgumentList '/d','/c',"`"$batch`"" -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$build/build.log" -RedirectStandardError "$build/build-errors.log"
Get-Content "$build/build.log" -Tail 12
if ($buildProcess.ExitCode) {Get-Content "$build/build-errors.log" -Tail 20;throw "Converter build failed ($($buildProcess.ExitCode))" }
if ($ConfigureOnly) { exit 0 }
$package=Join-Path $root 'dist/WoodyPSP-Studio-1.1.0'
New-Item -ItemType Directory -Force $package | Out-Null
Copy-Item "$build/WoodyConverter.exe" "$package/WoodyPSP-Studio.exe" -Force
$redist=Get-ChildItem "$vs/VC/Redist/MSVC" -Directory | Where-Object Name -Match '^\d' | Sort-Object Name -Descending | Select-Object -First 1
$crt=Get-ChildItem "$($redist.FullName)/x64" -Directory -Filter '*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'MSVC redistributable DLLs not found' }
Copy-Item "$($crt.FullName)/*.dll" -Destination $package -Force
Copy-Item "$($crt.FullName)/*.dll" -Destination $build -Force
$deploy=Start-Process -FilePath "$qt/bin/windeployqt.exe" -ArgumentList '--release','--no-translations','--no-opengl-sw','--no-system-d3d-compiler','--no-compiler-runtime',"`"$package/WoodyPSP-Studio.exe`"" -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$build/deploy.log" -RedirectStandardError "$build/deploy-errors.log"
if ($deploy.ExitCode) {Get-Content "$build/deploy-errors.log";throw 'Qt deployment failed' }
foreach ($dll in 'dxcompiler.dll','dxil.dll') {
    $shader=Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Redist/D3D/x64/$dll"
    if(Test-Path $shader) {Copy-Item $shader -Destination $package -Force}
}
Copy-Item "$root/LICENSE","$root/NOTICE" -Destination $package -Force
Write-Output "Converter: $package/WoodyPSP-Studio.exe"
exit 0
