@echo off
setlocal
cd /d "%~dp0"
echo Windows 2000 Modern Root Certificate Installer
echo.
echo This will add the included public CA roots to the Local Computer
echo Trusted Root Certification Authorities store.
echo Administrator rights are required.
echo.
if not exist W2KROOTS.EXE goto missing_tool
if not exist certs goto missing_certs
W2KROOTS.EXE install certs
if errorlevel 1 goto failed
echo.
echo SUCCESS: every included root is installed and verified.
echo Close and restart Supermium before testing HTTPS sites.
exit /b 0

:missing_tool
echo ERROR: W2KROOTS.EXE is missing.
exit /b 2

:missing_certs
echo ERROR: the certs directory is missing.
exit /b 3

:failed
echo.
echo ERROR: one or more certificates could not be installed or verified.
echo Review the messages above and confirm you are running as Administrator.
exit /b 1
