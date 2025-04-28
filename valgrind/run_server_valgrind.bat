@echo off
REM filepath: P:\Cursor\Chat Server\valgrind\run_server_valgrind.bat
echo Running server through Valgrind...

REM Check if WSL is installed
wsl --list >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: WSL is not installed or enabled on this system.
    echo Please install WSL by running "wsl --install" in an administrator PowerShell.
    pause
    exit /b 1
)

REM Run Valgrind through WSL
wsl valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file="server_valgrind.log" ../chat_app/build/server/server
if %ERRORLEVEL% neq 0 (
    echo Error: Valgrind encountered issues during execution.
    echo Check if valgrind is installed in WSL by running "wsl sudo apt-get install valgrind".
)

echo Results saved to server_valgrind.log
pause
