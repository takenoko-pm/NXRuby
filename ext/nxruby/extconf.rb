require "mkmf"

if RUBY_PLATFORM =~ /mingw|mswin/
  # Windows (MSYS2 UCRT64) の標準パスを追加
  $INCFLAGS << " -I/ucrt64/include -I/ucrt64/include/SDL2"
  $LDFLAGS  << " -L/ucrt64/lib"
else
  # Mac / Linux は pkg-config に任せる
  require "pkg-config" rescue nil
  pkg_config("sdl2")
  pkg_config("SDL2_image")
  pkg_config("SDL2_mixer")
  pkg_config("SDL2_ttf")
end

# ライブラリの存在チェック
unless have_library("SDL2") && have_library("SDL2_image") && have_library("SDL2_mixer") && have_library("SDL2_ttf")
  abort "\n[ERROR] SDL2 libraries not found!\n" \
        "Please install SDL2 via your package manager:\n" \
        "  Windows: ridk exec pacman -S --noconfirm mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_image mingw-w64-ucrt-x86_64-SDL2_mixer mingw-w64-ucrt-x86_64-SDL2_ttf\n" \
        "  Mac:     brew install sdl2 sdl2_image sdl2_mixer sdl2_ttf\n" \
        "  Linux:   sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev\n\n"
end

create_makefile("nxruby/nxruby")