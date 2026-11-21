# Writes bin\versao.txt on every build (build.cmd): line 1 the latest published version (build-N on GitHub),
# line 2 the commit compiled, line 3 the date. System Settings > Software Update and the update notice read it,
# so a PC that compiles and installs by hand no longer shows an old version (21/11/2026: build-22 installed,
# the label still said build-19). Without gh or the network it keeps the version already in the file.
$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$f = Join-Path $root 'bin\versao.txt'
$tag = ''
if (Get-Command gh) {
    $tags = @(gh release list -R kills1nt/WacOS --limit 100 --json tagName --jq '.[].tagName' 2>$null)
    $n = @($tags | Where-Object { $_ -match '^build-(\d+)$' } | ForEach-Object { [int]($_ -replace '^build-', '') } | Measure-Object -Maximum).Maximum
    if ($n) { $tag = "build-$n" }
}
if (-not $tag -and (Test-Path $f)) { $tag = (@(Get-Content $f))[0].Trim() }
if (-not $tag) { exit 0 }
$sha = (git -C $root rev-parse HEAD 2>$null)
if ($sha) { $sha = $sha.Trim() } else { $sha = '' }
Set-Content -Encoding utf8 $f @($tag, $sha, (Get-Date -Format 'yyyy-MM-dd HH:mm'))
