param(
  [string]$Script = "scripts/default.lua",
  [string]$Output = "build/Sleepus-MK3-Latest.hex",
  [switch]$UsbDiagnostics,
  [switch]$UsbVerboseDiagnostics,
  [switch]$RumbleOledTrace
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$toolchain = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-gccarmnoneeabi-teensy\bin"
$gcc = Join-Path $toolchain "arm-none-eabi-gcc.exe"
$objcopy = Join-Path $toolchain "arm-none-eabi-objcopy.exe"
$size = Join-Path $toolchain "arm-none-eabi-size.exe"
$objectDirectory = Join-Path $root "build\lua-firmware\obj"
$temporaryDirectory = Join-Path $root "build\lua-firmware\temp"
$firmwareElf = Join-Path $root "build\lua-firmware\Sleepus-MK3-Lua-Runtime.elf"
$firmwareHex = Join-Path $root "build\lua-firmware\Sleepus-MK3-Lua-Runtime.hex"
$mapFile = Join-Path $root "build\lua-firmware\Sleepus-MK3-Lua-Runtime.map"

foreach($tool in @($gcc, $objcopy, $size))
{
  if(!(Test-Path $tool))
  {
    throw "Required ARM tool not found: $tool"
  }
}

New-Item -ItemType Directory -Force $objectDirectory,$temporaryDirectory |
  Out-Null
$env:TEMP = $temporaryDirectory
$env:TMP = $temporaryDirectory

$commonFlags = @(
  "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
  "-O2", "-g", "-ffunction-sections", "-fdata-sections", "-std=c99",
  "-DAT32F435RGT7", "-DUSE_STDPERIPH_DRIVER",
  "-I$(Join-Path $root 'libraries\drivers\inc')",
  "-I$(Join-Path $root 'libraries\cmsis\cm4\core_support')",
  "-I$(Join-Path $root 'libraries\cmsis\cm4\device_support')",
  "-I$(Join-Path $root 'middlewares\usb_drivers\inc')",
  "-I$(Join-Path $root 'middlewares\usbh_class\usbh_msc')",
  "-I$(Join-Path $root 'middlewares\3rd_party\fatfs')",
  "-I$(Join-Path $root 'project\inc')",
  "-I$(Join-Path $root 'libraries\lua\src')"
)

if($UsbDiagnostics -or $UsbVerboseDiagnostics)
{
  $commonFlags += "-DUSB_DIAGNOSTICS"
}
if($UsbVerboseDiagnostics)
{
  $commonFlags += "-DUSB_VERBOSE_DIAGNOSTICS"
}
if($RumbleOledTrace)
{
  $commonFlags += "-DRUMBLE_OLED_TRACE"
}

$driverNames = @(
  "at32f435_437_acc.c", "at32f435_437_crm.c", "at32f435_437_debug.c",
  "at32f435_437_exint.c", "at32f435_437_flash.c", "at32f435_437_gpio.c",
  "at32f435_437_i2c.c", "at32f435_437_misc.c", "at32f435_437_pwc.c",
  "at32f435_437_usart.c", "at32f435_437_usb.c"
)
$usbNames = @(
  "usb_core.c", "usbd_core.c", "usbd_int.c", "usbd_sdr.c",
  "usbh_core.c", "usbh_ctrl.c", "usbh_int.c"
)
$luaNames = @(
  "lapi.c", "lcode.c", "lctype.c", "ldebug.c", "ldo.c", "ldump.c",
  "lfunc.c", "lgc.c", "llex.c", "lmem.c", "lobject.c", "lopcodes.c",
  "lparser.c", "lstate.c", "lstring.c", "ltable.c", "ltm.c", "lundump.c",
  "lvm.c", "lzio.c", "lauxlib.c", "lbaselib.c", "lmathlib.c",
  "lstrlib.c", "ltablib.c"
)

$sources = @(
  Join-Path $root "libraries\cmsis\cm4\device_support\system_at32f435_437.c"
)
$sources += $driverNames | ForEach-Object {
  Join-Path $root "libraries\drivers\src\$_"
}
$sources += $usbNames | ForEach-Object {
  Join-Path $root "middlewares\usb_drivers\src\$_"
}
$sources += @(
  Join-Path $root "middlewares\usbh_class\usbh_msc\usbh_msc_class.c"
  Join-Path $root "middlewares\usbh_class\usbh_msc\usbh_msc_bot_scsi.c"
  Join-Path $root "middlewares\3rd_party\fatfs\ff.c"
)
$sources += Get-ChildItem (Join-Path $root "project\src\*.c") |
  Where-Object { $_.Name -notin @("hardware.c", "mod_engine.c") } |
  Select-Object -ExpandProperty FullName
$sources += $luaNames | ForEach-Object {
  Join-Path $root "libraries\lua\src\$_"
}

$objects = @()
$index = 0
foreach($source in $sources)
{
  if(!(Test-Path $source))
  {
    throw "Source file not found: $source"
  }
  $object = Join-Path $objectDirectory ("{0:D3}-{1}.o" -f $index,
    [IO.Path]::GetFileNameWithoutExtension($source))
  & $gcc @commonFlags -c $source -o $object
  if($LASTEXITCODE -ne 0)
  {
    throw "Compilation failed: $source"
  }
  $objects += $object
  $index++
}

$startup = Join-Path $root "project\AT32_IDE\startup_at32f435_437.s"
$startupObject = Join-Path $objectDirectory "startup_at32f435_437.o"
& $gcc @commonFlags -x assembler-with-cpp -c $startup -o $startupObject
if($LASTEXITCODE -ne 0)
{
  throw "Startup assembly failed"
}
$objects += $startupObject

$linkerScript = Join-Path $root "project\AT32_IDE\ldscripts\AT32F435xG_FLASH.ld"
$linkArguments = @(
  "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
  "-O2", "-T", $linkerScript, "-Wl,--gc-sections", "-Wl,-Map,$mapFile",
  "--specs=nano.specs", "-u", "_printf_float", "--specs=nosys.specs",
  "-o", $firmwareElf
) + $objects + @("-lm")
& $gcc @linkArguments
if($LASTEXITCODE -ne 0)
{
  throw "Firmware link failed"
}

& $objcopy -O ihex $firmwareElf $firmwareHex
if($LASTEXITCODE -ne 0)
{
  throw "Intel HEX generation failed"
}
& $size --format=berkeley $firmwareElf

$scriptPath = Join-Path $root $Script
$outputPath = Join-Path $root $Output
$archivePath = Join-Path $root "build\lua-firmware\default.sleepus-pack"
& node (Join-Path $root "tools\package-lua.js") --firmware $firmwareHex `
  --script $scriptPath --archive $archivePath --output $outputPath
if($LASTEXITCODE -ne 0)
{
  throw "Lua archive packaging failed"
}

Write-Output "Combined firmware: $outputPath"