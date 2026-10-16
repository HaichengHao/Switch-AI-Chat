$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$candidates = @(
    @($env:DEVKITPRO, 'D:\devkitPro', 'C:\devkitPro') |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) }
)

if (-not $candidates) {
    throw 'devkitPro was not found. Set DEVKITPRO to its Windows installation directory.'
}

$devkitRoot = $candidates[0]
$bash = Join-Path $devkitRoot 'msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash)) {
    throw "devkitPro bash was not found at $bash"
}

$env:SWITCH_AI_CHAT_ROOT = $projectRoot
$env:SWITCH_DEVKIT_ROOT = $devkitRoot
& $bash -lc 'project_root=$(cygpath -u "$SWITCH_AI_CHAT_ROOT"); devkit_root=$(cygpath -u "$SWITCH_DEVKIT_ROOT"); export DEVKITPRO="$devkit_root"; export DEVKITA64="$devkit_root/devkitA64"; export PATH="$DEVKITA64/bin:$PATH"; make -C "$project_root"'
exit $LASTEXITCODE
