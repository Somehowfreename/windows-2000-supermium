@echo off
cd /d "%~dp0"
start "" "Supermium W2K RC1.exe" --disable-encryption --disable-machine-id --user-data-dir=portable_data_swiftshader --use-angle=swiftshader --enable-unsafe-swiftshader
