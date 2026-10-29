# Builds the browser version of the published pieces into docs/ (ready for GitHub Pages / Netlify).
# Every piece gets its own folder (docs/<name>/); docs/home/ is the showcase page that shows
# each piece in a small frame, and docs/ itself just redirects there.
# Needs the Emscripten SDK; set $env:EMSDK_DIR if it is not in C:\Users\Work\emsdk.
$env:EMSDK_QUIET = '1'
$emsdk = if ($env:EMSDK_DIR) { $env:EMSDK_DIR } else { 'C:\Users\Work\emsdk' }
. "$emsdk\emsdk_env.ps1" *> $null

$pieces = 'signrec', 'gridpath', 'spideye'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build-web'
emcmake cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
cmake --build $build --target $pieces
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

$docs = Join-Path $root 'docs'
$out = Join-Path $build 'bin'
$stamp = Get-Date -Format 'yyyyMMddHHmmss'
# Older builds put signrec at the site root; those files are replaced by the redirect below.
Remove-Item "$docs\signrec.js", "$docs\signrec.wasm" -ErrorAction SilentlyContinue
foreach ($name in $pieces) {
    $dir = Join-Path $docs $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    $html = [IO.File]::ReadAllText("$out\$name.html", [Text.Encoding]::UTF8)
    $html = $html.Replace('__BUILD__', $stamp).Replace('__PIECE__', $name) -replace "src=`"?$name\.js`"?", "src=`"$name.js?v=$stamp`""
    if ($html -notmatch "$name\.js\?v=$stamp") { throw "could not stamp script url for $name" }
    [IO.File]::WriteAllText("$dir\index.html", $html, (New-Object Text.UTF8Encoding $false))
    Copy-Item "$out\$name.js", "$out\$name.wasm" $dir -Force
}

$homeDir = Join-Path $docs 'home'
New-Item -ItemType Directory -Force $homeDir | Out-Null
Copy-Item (Join-Path $root 'web\home.html') "$homeDir\index.html" -Force
[IO.File]::WriteAllText("$docs\index.html", '<!doctype html><meta http-equiv="refresh" content="0; url=home/">', (New-Object Text.UTF8Encoding $false))
Write-Host "Done: $docs  (serve with: python -m http.server 8000 --directory docs)"

