@REM Maven Wrapper for Desktop Pet Controller
@REM Uses JAVA_HOME from environment; delegates to MAVEN_HOME or mvn on PATH
@echo off
setlocal

REM --- JAVA_HOME resolution (prefer env, fallback to common install) ---
if not defined JAVA_HOME (
    if exist "C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot" (
        set "JAVA_HOME=C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot"
    ) else (
        echo ERROR: JAVA_HOME is not set and no fallback JDK found.
        echo   Set JAVA_HOME to your JDK 21 installation directory.
        exit /b 1
    )
)

REM --- Maven resolution (MAVEN_HOME > mvn on PATH > error) ---
if defined MAVEN_HOME (
    call "%MAVEN_HOME%\bin\mvn.cmd" %*
    exit /b %ERRORLEVEL%
)

where mvn >nul 2>nul
if %ERRORLEVEL% equ 0 (
    call mvn %*
    exit /b %ERRORLEVEL%
)

echo ERROR: Maven not found.
echo   Set MAVEN_HOME or ensure mvn is on PATH.
exit /b 1
