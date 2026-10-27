<#
    首次烧录：bootloader + 应用 A + 出厂元数据
    需要：ST-Link V2、OpenOCD、BOOT0 = 0
#>
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$boot = (Join-Path $root "firmware\f407_iap_boot.hex") -replace '\\', '/'
$app  = (Join-Path $root "firmware\app_slot_a.hex") -replace '\\', '/'
$meta = (Join-Path $root "firmware\meta_a.bin") -replace '\\', '/'

foreach ($f in @($boot, $app, $meta)) {
    if (-not (Test-Path $f)) { throw "missing $f" }
}

Write-Host "flashing bootloader + slot A + metadata ..."
& openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
    -c "program `"$boot`" verify" `
    -c "program `"$app`" verify" `
    -c "program `"$meta`" 0x08008000 verify" `
    -c "reset" -c "exit"

if ($LASTEXITCODE -ne 0) { throw "openocd failed" }
Write-Host "done. 现在双击 AB-IAP升级工具.exe"
