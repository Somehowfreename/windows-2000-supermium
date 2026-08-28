@echo off
setlocal
cd /d "%~dp0"
if not exist W2KROOTS.EXE goto missing_tool
if not exist certs goto missing_certs
W2KROOTS.EXE verify certs
if errorlevel 1 goto failed
echo.
echo SUCCESS: every included root is present in the Local Computer store.
exit /b 0

:missing_tool
echo ERROR: W2KROOTS.EXE is missing.
exit /b 2

:missing_certs
echo ERROR: the certs directory is missing.
exit /b 3

:failed
echo ERROR: one or more included roots are missing.
exit /b 1
