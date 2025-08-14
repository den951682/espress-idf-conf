@echo off
setlocal

cd /d "%~dp0"
echo [INFO] Current dir: %cd%

set "PROTO_DIR=protobufModel\definitions"
set "OUT_DIR=main\proto-model"
set "NANOPB_DIR=%cd%\managed_components\nikas-belogolov__nanopb"
set "PROTOC_EXE=C:\protoc-31.1\bin\protoc.exe"

set "PATH=%NANOPB_DIR%\generator;%PATH%"

if not exist "%OUT_DIR%" (
    echo [INFO] Creating %OUT_DIR%
    mkdir "%OUT_DIR%"
) else (
    echo [INFO] %OUT_DIR% exists
)

echo [INFO] Clear folder: %OUT_DIR%...
del /Q "%OUT_DIR%\*.pb.c" >nul 2>&1
del /Q "%OUT_DIR%\*.pb.h" >nul 2>&1

if not exist "third_party\protobuf\google\protobuf\descriptor.proto" (
    mkdir third_party\protobuf\google\protobuf
    echo [INFO] Downloading descriptor.proto...
    curl -L -o "third_party\protobuf\google\protobuf\descriptor.proto" ^
        https://raw.githubusercontent.com/nanopb/nanopb/refs/heads/master/generator/proto/google/protobuf/descriptor.proto
)


echo [INFO] Generation of .pb files...
"%PROTOC_EXE%" ^
    -I"%PROTO_DIR%" ^
    -I"third_party\protobuf" ^
    --nanopb_out="%OUT_DIR%" ^
    "%PROTO_DIR%\*.proto" ^
    "third_party\protobuf\google\protobuf\descriptor.proto"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Error!
) else (
    echo [SUCCESS] Success!
)

pause
