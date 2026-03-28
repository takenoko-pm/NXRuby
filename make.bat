@echo off
setlocal
:: ==========================================
:: 設定エリア
:: ==========================================
:: ⚠️ 注意: \18\ の部分はご自身の環境に合わせて \2022\ などに修正してください
set "VS_VARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "MRUBY_ROOT=C:\Dev\mruby-mruby-04f9982"
set "GEM_ROOT=C:\Dev\mruby-nxruby"
set "APP_DIR=C:\Dev\nxruby_app"

:: ==========================================
:: ビルド処理
:: ==========================================
:: vcvars64.bat が存在するかチェック
if not exist "%VS_VARS%" (
    echo [Error] vcvars64.bat not found. Please check VS_VARS path.
    pause
    exit /b 1
)

call "%VS_VARS%"
set "MRUBY_CONFIG=%GEM_ROOT%\nxruby_build.rb"

echo [Build] Starting mruby build...
pushd "%MRUBY_ROOT%"
:: call をつけて rake を実行
call rake
:: rake の結果を変数に保存してから popd する
set BUILD_STATUS=%ERRORLEVEL%
popd

:: ==========================================
:: 成功時のデプロイ＆実行
:: ==========================================
:: 保存した結果で判定する
if %BUILD_STATUS% equ 0 (
    echo [Success] Copying files to %APP_DIR%...
    
    :: フォルダがなければ作成
    if not exist "%APP_DIR%" mkdir "%APP_DIR%"

    :: 1. exeをコピー
    copy /y "%MRUBY_ROOT%\build\host\bin\nxruby.exe" "%APP_DIR%\"

    :: 2. 実行（カレントディレクトリをnxruby_appに移動してから叩く）
    echo [Run] Launching NXRuby...
    pushd "%APP_DIR%"
    nxruby.exe app.rb
    popd
) else (
    echo [Error] Build failed. Check the logs above.
    pause
)

endlocal