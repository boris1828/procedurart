# Builds the browser version of signrec into docs/ (ready for GitHub Pages / Netlify).
# Needs the Emscripten SDK; set $env:EMSDK_DIR if it is not in C:\Users\Work\emsdk.
$env:EMSDK_QUIET = '1'
$emsdk = if ($env:EMSDK_DIR) { $env:EMSDK_DIR } else { 'C:\Users\Work\emsdk' }
. "$emsdk\emsdk_env.ps1" *> $null

$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build-web'
emcmake cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
cmake --build $build --target signrec
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

$docs = Join-Path $root 'docs'
New-Item -ItemType Directory -Force $docs | Out-Null
$out = Join-Path $build 'bin'
$stamp = Get-Date -Format 'yyyyMMddHHmmss'
$html = [IO.File]::ReadAllText("$out\signrec.html", [Text.Encoding]::UTF8)
$html = $html.Replace('__BUILD__', $stamp) -replace 'src="?signrec\.js"?', "src=`"signrec.js?v=$stamp`""
if ($html -notmatch "signrec\.js\?v=$stamp") { throw 'could not stamp script url' }
[IO.File]::WriteAllText("$docs\index.html", $html, (New-Object Text.UTF8Encoding $false))
Copy-Item "$out\signrec.js", "$out\signrec.wasm" $docs -Force
Write-Host "Done: $docs  (serve with: python -m http.server 8000 --directory docs)"
