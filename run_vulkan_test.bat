@echo off
echo ===================================
echo Vulkan RGB→NV12 Conversion Test
echo ===================================
echo.

cd build\Debug

echo Running test...
CapCutClone.exe --test-vulkan

if %ERRORLEVEL% == 0 (
    echo.
    echo ================================
    echo Test PASSED!
    echo ================================
    echo.
    echo Output file: test_output.nv12
    echo.
    echo View with FFplay:
    echo   ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 test_output.nv12
) else (
    echo.
    echo ================================
    echo Test FAILED - Check output above
    echo ================================
)

pause
