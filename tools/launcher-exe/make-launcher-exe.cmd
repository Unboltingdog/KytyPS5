@echo off
rem Builds "KytyPS5 Launcher.exe" in the KytyPS5 folder (two levels up): a program that opens launcher.ps1 like
rem launcher.cmd, without a console window. Uses the .NET Framework C# compiler that ships with Windows 10 and 11;
rem nothing to install. Run it again after an update only if KytyLauncher.cs changed (launcher.ps1 changes need
rem nothing: the exe just starts it).
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%..\..\KytyPS5 Launcher.exe"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
	echo The .NET Framework 4 C# compiler was not found ^(csc.exe under %WINDIR%\Microsoft.NET^).
	exit /b 1
)
"%CSC%" /nologo /target:winexe /optimize+ /platform:anycpu "/win32icon:%HERE%kyty.ico" "/out:%OUT%" "%HERE%KytyLauncher.cs" || exit /b 1
echo Built: %OUT%
