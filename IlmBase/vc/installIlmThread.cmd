@echo off
set deploypath=..\..\..\..\..\..\Deploy
set src=..\..\..\..\IlmThread

if not exist %deploypath% mkdir %deploypath%

set intdir=%1%
if %intdir%=="" set intdir=Release
echo Installing into %intdir%
set instpath=%deploypath%\lib\%intdir%
if not exist %instpath% mkdir %instpath%

copy ..\Win32\%intdir%\IlmThread.lib %instpath%
copy ..\Win32\%intdir%\IlmThread.exp %instpath%

set instpath=%deploypath%\bin\%intdir%
if not exist %instpath% mkdir %instpath%
copy ..\Win32\%intdir%\IlmThread.dll %instpath%

cd %src%
set instpath=..\..\..\Deploy\include
mkdir %instpath%
copy *.h %instpath%

copy ..\config.windows\*.h %instpath%
