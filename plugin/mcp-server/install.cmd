@echo off
rem Installs the MCP server's dependencies (Node.js 20+ and npm needed), exactly the versions in
rem package-lock.json. Run once after installing or updating the plugin.
cd /d "%~dp0"
where npm >nul 2>nul || (echo npm not found: install Node.js 20 or newer from https://nodejs.org & exit /b 1)
call npm ci --omit=dev --ignore-scripts --no-audit --no-fund || exit /b 1
echo.
echo MCP server ready: node "%~dp0dist\index.js"
