$ErrorActionPreference = 'Stop'

$amBuildCommit = 'd89ec91a7ac2607da07b50bb62346f9a10e9a998'
$toolsDirectory = Join-Path $PSScriptRoot '.tools'
$amBuildDirectory = Join-Path $toolsDirectory 'ambuild'

function Confirm-Command {
	param([string]$Message)
	if ($LASTEXITCODE -ne 0) {
		throw $Message
	}
}

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) {
	throw 'Git is required. Install Git, reopen the terminal, and run this script again.'
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) {
	throw 'Python 3 is required. Install Python, reopen the terminal, and run this script again.'
}
& $python.Source -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)'
Confirm-Command 'Python 3.8 or newer is required.'

Push-Location $PSScriptRoot
try {
	if (Test-Path (Join-Path $PSScriptRoot '.git')) {
		& $git.Source submodule update --init --recursive
		Confirm-Command 'The CS2 SDK or Metamod dependency could not be downloaded.'
	}
	elseif (-not (Test-Path (Join-Path $PSScriptRoot 'hl2sdk-cs2\public')) -or
		-not (Test-Path (Join-Path $PSScriptRoot 'metamod-source\core'))) {
		throw 'This source copy is missing the bundled CS2 SDK or Metamod dependency.'
	}

	New-Item -ItemType Directory -Force $toolsDirectory | Out-Null
	if (-not (Test-Path (Join-Path $amBuildDirectory '.git')) -and
		-not (Test-Path (Join-Path $amBuildDirectory 'ambuild2'))) {
		& $git.Source clone --filter=blob:none https://github.com/alliedmodders/ambuild.git $amBuildDirectory
		Confirm-Command 'AMBuild could not be downloaded.'
	}

	if (Test-Path (Join-Path $amBuildDirectory '.git')) {
		$currentCommit = (& $git.Source -C $amBuildDirectory rev-parse HEAD).Trim()
		Confirm-Command 'The downloaded AMBuild checkout could not be read.'
		if ($currentCommit -ne $amBuildCommit) {
			$localChanges = & $git.Source -C $amBuildDirectory status --porcelain
			Confirm-Command 'The downloaded AMBuild checkout could not be checked.'
			if ($localChanges) {
				throw 'The local .tools/ambuild folder has changes. Remove or save them before bootstrapping again.'
			}
			& $git.Source -C $amBuildDirectory fetch --depth=1 origin $amBuildCommit
			Confirm-Command 'The pinned AMBuild revision could not be downloaded.'
			& $git.Source -C $amBuildDirectory checkout --detach $amBuildCommit
			Confirm-Command 'The pinned AMBuild revision could not be selected.'
		}
	}
}
finally {
	Pop-Location
}

Write-Host 'CSVILKA build dependencies are ready.'
