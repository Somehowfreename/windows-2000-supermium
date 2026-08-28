@echo off
cscript //nologo C:\W2KVALID\w2k_clean_inventory.vbs > C:\W2KVALID\clean-inventory.txt 2>&1
exit /b %ERRORLEVEL%
