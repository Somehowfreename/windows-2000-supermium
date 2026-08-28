@echo off
cd /d "%~dp0"
start "" "Supermium W2K RC1.exe" --disable-encryption --disable-machine-id --user-data-dir=portable_data --classic-omnibox --classic-omnibox-border --compact-ui --enable-features=SupermiumCustomTabs --disable-features=DownloadBubble,TabHoverCards,PowerBookmarksSidePanel
