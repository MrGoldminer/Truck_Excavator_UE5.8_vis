@echo off
setlocal
pushd "%~dp0\.."
set "PROTOC=protoc.exe"
if exist "Build\Tools\Protoc\bin\protoc.exe" set "PROTOC=Build\Tools\Protoc\bin\protoc.exe"
mkdir Intermediate\ProtoGenerated 2>nul
"%PROTOC%" --proto_path=Proto --cpp_out=Intermediate/ProtoGenerated Proto/quarryworks.proto || exit /b 1
move /y Intermediate\ProtoGenerated\quarryworks.pb.h Source\QuarryWorks\Public\Protocol\ >nul
move /y Intermediate\ProtoGenerated\quarryworks.pb.cc Source\QuarryWorks\Private\Protocol\ >nul
python Proto\Postprocess.py || exit /b 1
popd
