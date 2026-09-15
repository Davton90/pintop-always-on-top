@echo off
REM Build PinTop.exe with built-in .NET Framework (no SDK needed) - tiny, ~15KB
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:winexe /optimize+ /win32icon:"%~dp0PinTop.ico" /out:"%~dp0PinTop.exe" "%~dp0PinTop.cs" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll
if exist "%~dp0PinTop.exe" (
  echo OK: %~dp0PinTop.exe
  dir "%~dp0PinTop.exe"
) else (
  echo BUILD FAILED
)
pause
