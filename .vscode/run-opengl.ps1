$exe1 = Join-Path $PWD "build/opengl_starter.exe"
$exe2 = Join-Path $PWD "build/Debug/opengl_starter.exe"
$mingwBin = Join-Path $env:LOCALAPPDATA "Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"

if (Test-Path $mingwBin) {
    $env:Path = "$mingwBin;$env:Path"
}

if (Test-Path $exe1) {
    Start-Process -FilePath $exe1 -WorkingDirectory (Split-Path $exe1)
    exit 0
}

if (Test-Path $exe2) {
    Start-Process -FilePath $exe2 -WorkingDirectory (Split-Path $exe2)
    exit 0
}

throw "Executable not found. Build first."
