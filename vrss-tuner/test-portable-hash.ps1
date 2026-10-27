$ErrorActionPreference = 'Stop'
$fixture = Join-Path $PSScriptRoot ('build\portable-hash-smoke-' + (Get-Date -Format 'yyyyMMddTHHmmssfff') + '-' + $PID)
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$sample = Join-Path $fixture 'sample.txt'
[IO.File]::WriteAllText($sample, 'abc', [Text.Encoding]::ASCII)
$expected = 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD'
function Get-FileHash { throw 'The portable runtime must not call Get-FileHash.' }
foreach ($name in @('Test-Setup.ps1','Start-Provider.ps1')) {
    $path = Join-Path $PSScriptRoot "portable\$name"
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw "$name has parser errors." }
    $definitions = @($ast.FindAll({param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-PortableSha256'
    }, $true))
    if ($definitions.Count -ne 1 -or
        (Get-Content -LiteralPath $path -Raw) -match 'Get-FileHash') {
        throw "$name does not use the portable SHA-256 helper exclusively."
    }
    . ([ScriptBlock]::Create($definitions[0].Extent.Text))
    if ((Get-PortableSha256 $sample) -ne $expected) {
        throw "$name SHA-256 result differs from the known abc digest."
    }
}
Write-Output 'PORTABLE_HASH_PASS WindowsPowerShell sha256_without_Get-FileHash'
