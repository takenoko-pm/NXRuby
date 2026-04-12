require "mkmf"

# 拡張ライブラリの名前
extension_name = "nxruby"

# OSを判定
is_windows = RUBY_PLATFORM =~ /mingw|mswin/

# pkg-config gem を使用してパスを取得
# (WindowsのMSYS2環境でも、ridk exec 下ならこれでパスが通ります)
require "pkg-config" rescue nil

def find_package(name)
  if pkg_config(name)
    return true
  end
  # pkg-config が失敗した時のための予備（主にWindows用）
  have_library(name) || have_library(name.downcase)
end

# 各ライブラリのチェック
# SDL2本体、SDL2_image, SDL2_mixer, SDL2_ttf を順番に確認
ok = find_package("sdl2") && 
     find_package("SDL2_image") && 
     find_package("SDL2_mixer") && 
     find_package("SDL2_ttf")

unless ok
  abort "\n[ERROR] SDL2 libraries not found!\n" \
        "Please install SDL2 via your package manager:\n" \
        "  Windows: ridk exec pacman -S --noconfirm mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_image mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-ucrt-x86_64-SDL2_ttf\n" \
        "  Mac:     brew install sdl2 sdl2_image sdl2_mixer sdl2_ttf\n" \
        "  Linux:   sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev\n\n"
end

# Makefileの作成
create_makefile("nxruby/#{extension_name}")