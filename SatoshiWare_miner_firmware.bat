@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Install the shared firmware on one miner already on Wi-Fi.
REM From Command Prompt, pass that miner's IP:
REM   curl -fsSL -o %TEMP%\SatoshiWare_miner_firmware.bat https://raw.githubusercontent.com/satoshiware/minerOS/main/SatoshiWare_miner_firmware.bat && %TEMP%\SatoshiWare_miner_firmware.bat 192.168.38.75

if "%~1"=="" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="--help" goto usage

set "MINER_IP=%~1"
set "IP_OK="
for /f "tokens=1-5 delims=." %%a in ("%MINER_IP%") do (
  if "%%e"=="" if not "%%d"=="" (
    call :octet "%%a"
    if not errorlevel 1 call :octet "%%b"
    if not errorlevel 1 call :octet "%%c"
    if not errorlevel 1 call :octet "%%d"
    if not errorlevel 1 set "IP_OK=1"
  )
)
if not defined IP_OK (
  echo The miner IP must look like 192.168.38.75
  goto usage
)

where curl.exe >nul 2>&1
if errorlevel 1 (
  echo curl is missing. Installing it.
  where winget >nul 2>&1
  if errorlevel 1 (
    echo Install curl, then run this again.
    exit /b 1
  )
  winget install --id cURL.cURL -e --accept-package-agreements --accept-source-agreements
  where curl.exe >nul 2>&1
  if errorlevel 1 (
    echo curl is still missing. Close this window, open Command Prompt again, and retry.
    exit /b 1
  )
)

set "FW_URL=https://raw.githubusercontent.com/satoshiware/minerOS/main/firmware/esp-miner.bin"
set "WWW_URL=https://raw.githubusercontent.com/satoshiware/minerOS/main/firmware/www.bin"
set "FW=%CD%\esp-miner.bin"
set "WWW=%CD%\www.bin"
if not exist "%FW%" set "FW=%~dp0esp-miner.bin"
if not exist "%WWW%" set "WWW=%~dp0www.bin"
if not exist "%FW%" goto download_files
if not exist "%WWW%" goto download_files
goto have_files

:download_files
set "DLDIR=%TEMP%\SatoshiWare-miner-firmware"
mkdir "%DLDIR%" 2>nul
echo Downloading esp-miner.bin
curl.exe -fL --retry 3 --max-time 300 -o "%DLDIR%\esp-miner.bin" "%FW_URL%"
if errorlevel 1 exit /b 1
echo Downloading www.bin
curl.exe -fL --retry 3 --max-time 300 -o "%DLDIR%\www.bin" "%WWW_URL%"
if errorlevel 1 exit /b 1
set "FW=%DLDIR%\esp-miner.bin"
set "WWW=%DLDIR%\www.bin"

:have_files

echo Miner: http://%MINER_IP%
echo Firmware: %FW%
echo Web page: %WWW%

echo Checking the miner is on Wi-Fi
curl.exe -fsS --max-time 5 "http://%MINER_IP%/api/system/info" >nul
if errorlevel 1 (
  echo Cannot reach http://%MINER_IP%. Join the miner to Wi-Fi first. Updates are refused in setup-AP mode.
  exit /b 1
)

echo Installing firmware on %MINER_IP%
curl.exe -fS --max-time 300 -X POST -H "Content-Type: application/octet-stream" --data-binary "@%FW%" "http://%MINER_IP%/api/system/OTA"
if errorlevel 1 exit /b 1
echo.
echo Waiting for the reboot
timeout /t 8 /nobreak >nul
call :wait_for_miner
if errorlevel 1 exit /b 1

echo Installing the web page on %MINER_IP%
curl.exe -fS --max-time 300 -X POST -H "Content-Type: application/octet-stream" --data-binary "@%WWW%" "http://%MINER_IP%/api/system/OTAWWW"
if errorlevel 1 exit /b 1
echo.

echo Restarting so the new page is the one that loads
curl.exe -fS --max-time 30 -X POST "http://%MINER_IP%/api/system/restart" >nul
timeout /t 8 /nobreak >nul
call :wait_for_miner
if errorlevel 1 exit /b 1

echo.
echo Installed on http://%MINER_IP%
echo Hard-refresh that page.
echo On the Pool page, set the host, port, user, and password, then Save.
echo Split stays off until you turn it on and restart.
exit /b 0

:octet
set "OCT=%~1"
if "%OCT%"=="" exit /b 1
for /f "delims=0123456789" %%n in ("%OCT%") do exit /b 1
if %OCT% GTR 255 exit /b 1
exit /b 0

:wait_for_miner
set /a N=0
:wait_loop
set /a N+=1
if !N! GTR 90 (
  echo The miner at %MINER_IP% did not come back.
  exit /b 1
)
curl.exe -fsS --max-time 3 "http://%MINER_IP%/api/system/info" >nul 2>&1
if not errorlevel 1 exit /b 0
timeout /t 2 /nobreak >nul
goto wait_loop

:usage
echo Usage: SatoshiWare_miner_firmware.bat ^<miner-ip^>
echo Example: curl -fsSL -o %%TEMP%%\SatoshiWare_miner_firmware.bat https://raw.githubusercontent.com/satoshiware/minerOS/main/SatoshiWare_miner_firmware.bat ^&^& %%TEMP%%\SatoshiWare_miner_firmware.bat 192.168.38.75
exit /b 1
