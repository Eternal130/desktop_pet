@REM Maven Wrapper for Desktop Pet Controller
@REM Delegates to cached Maven or falls back to MAVEN_HOME/PATH
@echo off
setlocal

set "JAVA_HOME=C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot"

set "CACHED_MVN=C:\Users\Eternal130\.m2\wrapper\dists\apache-maven-3.8.5-bin\5i5jha092a3i37g0paqnfr15e0\apache-maven-3.8.5\bin\mvn.cmd"

if exist "%CACHED_MVN%" (
    "%CACHED_MVN%" %*
    exit /b %ERRORLEVEL%
)

where mvn >nul 2>nul
if %ERRORLEVEL% equ 0 (
    mvn %*
    exit /b %ERRORLEVEL%
)

echo ERROR: Maven not found.
echo   - Cached Maven not at: %CACHED_MVN%
echo   - mvn not on PATH
echo   Install Maven or update CACHED_MVN path in this script.
exit /b 1
