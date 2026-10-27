param(
    [string]$ContractFile = (Join-Path $PSScriptRoot 'common-vrss-provider\driver-contracts\616.92.txt'),
    [string]$ProviderExe = (Join-Path $PSScriptRoot 'common-vrss-provider\common_vrss_provider.exe')
)
$ErrorActionPreference = 'Stop'
$contract = (Resolve-Path -LiteralPath $ContractFile).Path
$provider = (Resolve-Path -LiteralPath $ProviderExe).Path
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = $provider
$start.Arguments = '--driver-check-only 1'
$start.WorkingDirectory = Split-Path $provider
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
# Only this read-only child's environment changes; not the shell or normal provider.
$start.EnvironmentVariables['VRSS_EXPERIMENTAL_DRIVER_CONTRACT'] = $contract
$process = New-Object Diagnostics.Process
$process.StartInfo = $start
try {
    if (-not $process.Start()) { throw 'Cannot start read-only driver check.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    Write-Output $text.Trim()
    if ($process.ExitCode -ne 0 -or -not $text.Contains('DRIVER_CHECK_PASS')) {
        throw 'Exact driver contract check failed.'
    }
    if (-not (($text -split '\r?\n') -contains 'driverContract=experimental')) {
        throw 'The child used a built-in contract, not the requested experimental contract.'
    }
    Write-Output 'EXPERIMENTAL_DRIVER_CHECK_PASS child_only no_registration'
} finally {
    $process.Dispose()
}
