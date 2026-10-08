@echo off
rem Twilight Princess HD setup in a console window (Windows): the fallback for the window of "Twilight Princess HD.exe".
rem The same program runs the setup in this console instead (--console-setup): it runs
rem tools\installer\setup.py with the Python shipped in tools\python.
setlocal
cd /d "%~dp0.."
"%~dp0..\Twilight Princess HD.exe" --console-setup %*
if errorlevel 1 pause
