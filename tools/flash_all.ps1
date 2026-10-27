<#
.SYNOPSIS
    First-time programming: bootloader + slot A app + factory metadata.

.DESCRIPTION
    Uses ST-Link via OpenOCD.  After this runs once, all further upgrades go
    through tools/iap_gui.py or tools/iap_update.py over the serial port.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\flash_all.ps1
    powershell -ExecutionPolicy Bypass -File tools\flash_all.ps1 -Version 1.0.2
#>
param(
    [string]$Version = "1.0.1",
    [string]$OpenOcd = "openocd"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$boot = (Join-Path $root "bootloader\MDK\f407_iap_boot\f407_iap_boot.hex") -replace '\\', '/'
$app = (Join-Path $root "app\MDK\app_slot_a\app_slot_a.hex") -replace '\\', '/'
$meta = (Join-Path $root "app\MDK\app_slot_a\meta_a.bin") -replace '\\', '/'

foreach ($file in @($boot, $app)) {
    if (-not (Test-Path $file)) {
        throw "missing $file - build the Keil projects first"
    }
}

Write-Host "generating factory metadata ..."
& python (Join-Path $PSScriptRoot "iap_update.py") provision $app -a --version $Version -o $meta
if ($LASTEXITCODE -ne 0) { throw "provision failed" }

Write-Host "flashing bootloader + slot A + metadata via ST-Link ..."
& $OpenOcd -f interface/stlink.cfg -f target/stm32f4x.cfg `
    -c "program `"$boot`" verify" `
    -c "program `"$app`" verify" `
    -c "program `"$meta`" 0x08008000 verify" `
    -c "reset" -c "exit"
if ($LASTEXITCODE -ne 0) { throw "openocd failed" }

Write-Host ""
Write-Host "done. next:"
Write-Host "  1. keep BOOT0 = 0 and press reset"
Write-Host "  2. python tools/iap_gui.py   (or iap_update.py status -p COMx)"
