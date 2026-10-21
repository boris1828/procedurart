# Builds the browser version of the published pieces into docs/ (ready for GitHub Pages / Netlify).
# signrec is the site root (docs/); every other piece gets its own folder (docs/<name>/).
# Needs the Emscripten SDK; set $env:EMSDK_DIR if it is not in C:\Users\Work\emsdk.
$env:EMSDK_QUIET = '1'
$emsdk = if ($env:EMSDK_DIR) { $env:EMSDK_DIR } else { 'C:\Users\Work\emsdk' }
. "$emsdk\emsdk_env.ps1" *> $null

$pieces = 'signrec', 'gridpath'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build-web'
emcmake cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
cmake --build $build --target $pieces
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

$docs = Join-Path $root 'docs'
$out = Join-Path $build 'bin'
$stamp = Get-Date -Format 'yyyyMMddHHmmss'
foreach ($name in $pieces) {
    $dir = if ($name -eq 'signrec') { $docs } else { Join-Path $docs $name }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $html = [IO.File]::ReadAllText("$out\$name.html", [Text.Encoding]::UTF8)
    $html = $html.Replace('__BUILD__', $stamp).Replace('__PIECE__', $name) -replace "src=`"?$name\.js`"?", "src=`"$name.js?v=$stamp`""
    if ($html -notmatch "$name\.js\?v=$stamp") { throw "could not stamp script url for $name" }
    [IO.File]::WriteAllText("$dir\index.html", $html, (New-Object Text.UTF8Encoding $false))
    Copy-Item "$out\$name.js", "$out\$name.wasm" $dir -Force
}
Write-Host "Done: $docs  (serve with: python -m http.server 8000 --directory docs)"
