<#
  Assembles the KlipIt app folder for the installer (OBS-compatible layout, so the
  capture engine finds its parts):

    dist\KlipIt\
      bin\64bit\KlipIt.exe     + Qt, engine and Visual C++ runtime DLLs
      obs-plugins\64bit\*.dll   capture + encoder parts only (nothing for streaming)
      data\libobs\*             shaders
      data\obs-plugins\<part>\  part data (incl. the game capture hook)
#>
param(
  [Parameter(Mandatory)] [string] $ObsInstall,
  [Parameter(Mandatory)] [string] $AppExe,
  [Parameter(Mandatory)] [string] $QtBin,
  [string] $Out = "dist\KlipIt",
  [string] $ObsVersion = ""
)
$ErrorActionPreference = "Stop"

# Engine parts KlipIt loads (must match the list in src/engine/Recorder.cpp)
$Modules = @("obs-ffmpeg","obs-x264","obs-nvenc","obs-qsv11","win-capture","win-wasapi","image-source")

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
$bin = New-Item -ItemType Directory -Force "$Out\bin\64bit"
New-Item -ItemType Directory -Force "$Out\obs-plugins\64bit","$Out\data" | Out-Null

# 1) our executable + Qt runtime (Qt's own FFmpeg playback plugin is left out; KlipIt uses
#    Windows' player and the engine's FFmpeg)
Copy-Item $AppExe $bin
& "$QtBin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --exclude-plugins ffmpegmediaplugin "$bin\KlipIt.exe"
if ($LASTEXITCODE -ne 0) {
  Write-Warning "windeployqt --exclude-plugins not supported; deploying all plugins"
  & "$QtBin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$bin\KlipIt.exe"
  if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
  Remove-Item -Force -ErrorAction SilentlyContinue "$bin\multimedia\ffmpegmediaplugin.dll"
}

# 2) engine runtime: copied after Qt so the engine's FFmpeg DLLs are the ones that ship
Get-ChildItem "$ObsInstall\bin\64bit" -File | Where-Object {
    $_.Name -notmatch '^(obs64\.exe|obs-frontend-api\.dll|Qt6.*\.dll|.*\.pdb)$'
} | Copy-Item -Destination $bin -Force
if (-not (Test-Path "$bin\obs-ffmpeg-mux.exe")) { throw "obs-ffmpeg-mux.exe missing (needed for clips and recordings)" }

foreach ($m in $Modules) {
  $dll = "$ObsInstall\obs-plugins\64bit\$m.dll"
  if (Test-Path $dll) { Copy-Item $dll "$Out\obs-plugins\64bit\" } else { Write-Warning "engine part $m not found" }
  if (Test-Path "$ObsInstall\data\obs-plugins\$m") { Copy-Item -Recurse "$ObsInstall\data\obs-plugins\$m" "$Out\data\obs-plugins\$m" }
}
Copy-Item -Recurse "$ObsInstall\data\libobs" "$Out\data\libobs"

# 3) the game capture hook: use OBS's officially signed copy of the same version when available,
#    because some anti-cheat systems only allow signed hooks
if ($ObsVersion) {
  try {
    $assets = gh api "repos/obsproject/obs-studio/releases/tags/$ObsVersion" --jq '.assets[].name' 2>$null
    $zip = $assets | Where-Object { $_ -match 'Windows' -and $_ -match 'x64' -and $_ -match '\.zip$' -and $_ -notmatch 'PDB|Debug' } | Select-Object -First 1
    if ($zip) {
      $tmp = Join-Path $env:RUNNER_TEMP "obs-release"
      gh release download $ObsVersion --repo obsproject/obs-studio --pattern $zip --dir $tmp --clobber
      Expand-Archive -Force (Join-Path $tmp $zip) (Join-Path $tmp "x")
      $hook = Get-ChildItem -Recurse (Join-Path $tmp "x") -Filter "graphics-hook64.dll" | Select-Object -First 1
      if ($hook) {
        Copy-Item -Force "$($hook.DirectoryName)\*" "$Out\data\obs-plugins\win-capture\"
        Write-Host "Using the signed game capture hook from $zip"
      }
    } else { Write-Warning "No official OBS $ObsVersion Windows zip found; using the hook built from source" }
  } catch { Write-Warning "Couldn't fetch the signed game capture hook: $_" }
}

# 4) Visual C++ runtime next to the exe, so no administrator install is needed
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC" -Directory | Sort-Object Name -Descending |
       ForEach-Object { Get-ChildItem "$($_.FullName)\x64" -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue } | Select-Object -First 1
if (-not $crt) { throw "Visual C++ runtime DLLs not found" }
Copy-Item "$($crt.FullName)\*.dll" $bin -Force
Write-Host "Visual C++ runtime from $($crt.FullName)"

Write-Host "Packaged to $Out"
Get-ChildItem -Recurse $Out | Measure-Object -Property Length -Sum | ForEach-Object { "{0:N1} MB in {1} files" -f ($_.Sum/1MB), $_.Count }
