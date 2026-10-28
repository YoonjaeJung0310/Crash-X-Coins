param(
    [string]$PublicAddress = '',
    [ValidateRange(1, 65535)][int]$Port = 7777,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.6'
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskProjectFile = Join-Path $taskProjectRoot 'CX.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskLogDirectory = Join-Path $taskProjectRoot 'Saved\Logs'
$taskLog = Join-Path $taskLogDirectory 'CX_Dedicated.log'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "UnrealEditor-Cmd.exe not found: $taskEditor" }
if (-not (Test-Path -LiteralPath $taskProjectFile)) { throw "CX.uproject not found: $taskProjectFile" }
if ($PublicAddress -and $PublicAddress -notmatch '^\d{1,3}(\.\d{1,3}){3}:\d{1,5}$') {
    throw 'PublicAddress must be IPv4:Port. The game performs the final range validation.'
}
if (-not $PublicAddress) {
    $PublicAddress = '127.0.0.1:{0}' -f $Port
    Write-Output 'Local-only room address selected. For friends on another computer, pass a reachable LAN/public IPv4:Port.'
}
New-Item -ItemType Directory -Path $taskLogDirectory -Force | Out-Null
$taskArguments = @(
    ('"{0}"' -f $taskProjectFile),
    '/Game/CX/Maps/L_CXCityArena',
    '-server', '-unattended', '-nop4', '-nosplash', '-nosound',
    ('-port={0}' -f $Port),
    ('-abslog="{0}"' -f $taskLog)
)
if ($PublicAddress) { $taskArguments += ('-CXPublicAddress={0}' -f $PublicAddress) }
$taskServer = Start-Process -FilePath $taskEditor -ArgumentList $taskArguments -WorkingDirectory $taskProjectRoot -WindowStyle Hidden -PassThru
[PSCustomObject]@{ ProcessId = $taskServer.Id; Log = $taskLog; PublicAddress = $PublicAddress; Port = $Port }
Write-Output 'One dedicated process is one room. This command does not rent a server or open firewall/router ports.'
Write-Output 'The server log contains CX_ROOM_READY when a valid public address was supplied.'
