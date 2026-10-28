param(
    [string]$JoinCode = '',
    [string]$ServerAddress = '',
    [switch]$AutoMatch,
    [string]$PlayerName = 'Player',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.6'
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskProjectFile = Join-Path $taskProjectRoot 'CX.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "UnrealEditor.exe not found: $taskEditor" }
if ($ServerAddress -and $ServerAddress -notmatch '^\d{1,3}(\.\d{1,3}){3}:\d{1,5}$') { throw 'ServerAddress must be IPv4:Port.' }
if ($JoinCode -and $JoinCode -notmatch '^[A-Za-z0-9.:-]+$') { throw 'JoinCode contains unsupported characters.' }
if ($PlayerName -notmatch '^[\p{L}\p{N}_ -]{1,16}$') { throw 'PlayerName must be 1-16 letters, digits, spaces, underscores or hyphens.' }
$taskArguments = @(
    ('"{0}"' -f $taskProjectFile), '/Game/CX/Maps/L_CXMainMenu',
    '-game', '-windowed', '-ResX=1280', '-ResY=720', '-nop4', '-nosplash'
)
if ($ServerAddress) { $taskArguments += ('-CXServerAddress={0}' -f $ServerAddress) }
if ($AutoMatch) { $taskArguments += '-CXAutoMatch' }
if ($AutoMatch -or $JoinCode) { $taskArguments += ('-CXPlayerName="{0}"' -f $PlayerName) }
if ($JoinCode) {
    $taskArguments += ('-CXJoinCode={0}' -f $JoinCode)
}
Start-Process -FilePath $taskEditor -ArgumentList $taskArguments -WorkingDirectory $taskProjectRoot -PassThru |
    Select-Object Id, ProcessName
