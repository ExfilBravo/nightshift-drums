@echo off
rem Nightshift Drums built with Clang (clang-cl), the compiler family Apple's Xcode uses: what MSVC lets
rem through and Clang doesn't shows up here before the Mac sees it. Its own build folder; keeps going past errors.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\ClaudeCode\tools\llvm\clang+llvm-23.1.5-x86_64-pc-windows-msvc\bin;%PATH%"
if not exist "%~dp0build\Clang\build.ninja" cmake -S "%~dp0." -B "%~dp0build\Clang" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link "-DCMAKE_CXX_FLAGS=/D_USE_STD_VECTOR_ALGORITHMS=0 /EHsc" || exit /b 1
cmake --build "%~dp0build\Clang" --target drumstool -- -k 0
