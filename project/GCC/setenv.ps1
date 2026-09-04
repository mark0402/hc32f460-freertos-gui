# setenv.ps1 - add GCC toolchain (with make) to current PowerShell session PATH
# Usage (in project/GCC):  . .\setenv.ps1
# Effect: only the current terminal; closing the terminal loses it.

$ToolchainRoots = @(
    $env:ARM_GNU_TOOLCHAIN,
    "D:\develop tools\arm-gnu-toolchain",
    "C:\Tools\arm-gnu-toolchain",
    "D:\Tools\arm-gnu-toolchain"
)

$ToolBin = $null
foreach ($r in $ToolchainRoots) {
    if ($r -and (Test-Path "$r\bin\arm-none-eabi-gcc.exe")) {
        $ToolBin = "$r\bin"
        break
    }
}

if (-not $ToolBin) {
    Write-Warning "Toolchain not found (arm-none-eabi-gcc.exe). Set ARM_GNU_TOOLCHAIN or check install path."
    return
}

if ($env:PATH -notlike "*$ToolBin*") {
    $env:PATH = "$ToolBin;$env:PATH"
    Write-Host "PATH += $ToolBin"
} else {
    Write-Host "PATH already has: $ToolBin"
}

$gcc = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
$mk  = Get-Command make -ErrorAction SilentlyContinue
Write-Host ("arm-none-eabi-gcc : " + $(if ($gcc) { "OK -> $($gcc.Source)" } else { "MISSING" }))
Write-Host ("make             : " + $(if ($mk)  { "OK -> $($mk.Source)"  } else { "MISSING"  }))
