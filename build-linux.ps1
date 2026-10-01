param(
	[string]$BuildDirectory = 'build-linux-release'
)

$ErrorActionPreference = 'Stop'

if ($BuildDirectory -notmatch '^[A-Za-z0-9_.-]+$') {
	throw 'The build folder name may only contain letters, numbers, dots, underscores, and dashes.'
}
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
	throw 'WSL 2 and Docker are required for the Linux build.'
}

& (Join-Path $PSScriptRoot 'bootstrap.ps1')

$image = 'registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce'
$sourcePath = (& wsl.exe --cd $PSScriptRoot -- pwd).Trim()
$amBuildPath = (& wsl.exe --cd (Join-Path $PSScriptRoot '.tools\ambuild') -- pwd).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sourcePath -or -not $amBuildPath) {
	throw 'The project paths could not be translated for WSL.'
}

& wsl.exe -- docker version --format '{{.Server.Version}}' | Out-Null
if ($LASTEXITCODE -ne 0) {
	throw 'Docker is not available inside the default WSL distribution.'
}

$build = ('set -eu; export DEBIAN_FRONTEND=noninteractive; apt-get update >/dev/null; ' +
	'apt-get install -y --no-install-recommends lld >/dev/null; export PYTHONPATH=/opt/ambuild; ' +
	"test -d $BuildDirectory/.ambuild2 || python3 configure.py --enable-optimize --out $BuildDirectory; " +
	"python3 -c 'from ambuild2.run import cli_run; cli_run()' $BuildDirectory")

& wsl.exe -- docker run --rm `
	-v "${sourcePath}:/workspace" `
	-v "${amBuildPath}:/opt/ambuild:ro" `
	-w /workspace $image bash -lc $build
if ($LASTEXITCODE -ne 0) {
	throw 'The Linux SteamRT3 build failed.'
}

$plugin = Join-Path $PSScriptRoot "$BuildDirectory\package\game\csgo\addons\csvilka\bin\linuxsteamrt64\csvilka.so"
if (-not (Test-Path $plugin)) {
	throw 'The build finished without producing csvilka.so.'
}
Write-Host "Linux package ready: $plugin"
