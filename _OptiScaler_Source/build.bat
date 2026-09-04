@echo off
set "VS_PATH="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_PATH=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if "%VS_PATH%"=="" (
    echo [ERRO] Nao foi possivel localizar o Visual Studio.
    exit /b 1
)

call "%VS_PATH%"
cl.exe /Zi /std:c++17 /EHsc /I "%~dp0include" "%~dp0TSREngine.cpp" /link /MACHINE:X64 /LIBPATH:"%~dp0lib" onnxruntime.lib d3d12.lib dxgi.lib /OUT:"%~dp0TSREngine.dll" /DLL