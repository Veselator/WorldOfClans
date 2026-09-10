@echo off
rem Compiles every GLSL shader in Shaders\ to SPIR-V next to the source.
rem Called as a pre-build step; %1 is the project directory.

setlocal
set ROOT=%~1
if "%ROOT%"=="" set ROOT=%~dp0..\

set GLSLANG=%ROOT%Tools\glslang.exe
if not exist "%GLSLANG%" (
    if defined VULKAN_SDK set GLSLANG=%VULKAN_SDK%\Bin\glslangValidator.exe
)
if not exist "%GLSLANG%" (
    echo [shaders] No GLSL compiler found; keeping the existing .spv files.
    exit /b 0
)

for %%F in ("%ROOT%Shaders\*.vert" "%ROOT%Shaders\*.frag" "%ROOT%Shaders\*.comp") do (
    "%GLSLANG%" -V "%%~fF" -o "%%~fF.spv" >nul
    if errorlevel 1 (
        echo [shaders] FAILED: %%~nxF
        "%GLSLANG%" -V "%%~fF" -o "%%~fF.spv"
        exit /b 1
    )
)

echo [shaders] SPIR-V up to date.
endlocal
exit /b 0
