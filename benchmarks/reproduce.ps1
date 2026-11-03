param(
    [Parameter(Mandatory)]
    [ValidateSet('full', 'sample')]
    [string] $Mode,
    [string] $Corpus,
    [string] $PngDeps,
    [string] $Out,
    [string] $ClangCl = 'clang-cl',
    [switch] $Resume,
    [switch] $Smoke
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build'
if (!$Out) {
    $name = if ($Smoke) { "benchmark-$Mode-smoke" } else { "benchmark-$Mode" }
    $Out = Join-Path $build $name
}

$runner = @((Join-Path $PSScriptRoot 'run.py'), '--out', $Out, '--clang-cl', $ClangCl)
if ($Corpus) { $runner += @('--corpus', $Corpus) }
if ($Resume) { $runner += '--resume' }
if ($Smoke) { $runner += @('--limit', '2') }

if ($Mode -eq 'sample') {
    if (!$PngDeps) { throw 'Pass -PngDeps with the static libpng and zlib install.' }
    $target = Join-Path $build 'benchmark-oxi'
    $cargoManifest = Join-Path $PSScriptRoot 'oxipng-ffi/Cargo.toml'
    $dll = Join-Path $target 'release/oxi_bench.dll'
    if (!$Resume) {
        & cargo build --manifest-path $cargoManifest --target-dir $target --release --locked
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    } else {
        $dll = Join-Path $Out 'oxi_bench.dll'
    }
    $runner += @('--reps', '1', '--png-deps', $PngDeps, '--oxipng-dll', $dll)
    if (!$Smoke) { $runner += @('--sample', '640') }
}
& python @runner
exit $LASTEXITCODE
