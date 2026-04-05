require "mkmf"
require "mini_portile2"

# SDL3はCMakeでビルドするのが標準的なので MiniPortileCMake を使う
def build_sdl_library(name, version)
  MiniPortileCMake.new(name, version).tap do |recipe|
    recipe.files = ["https://github.com/libsdl-org/#{name}/releases/download/release-#{version}/#{name}-#{version}.tar.gz"]
    
    # 静的ライブラリとしてビルドし、余計な依存（Shared library）を消す設定
    recipe.configure_options << "-DBUILD_SHARED_LIBS=OFF"
    recipe.configure_options << "-DSDL_STATIC=ON"
    
    recipe.cook
    
    # ビルドされたパスを Ruby のコンパイル設定に追加
    $INCFLAGS << " -I#{recipe.path}/include"
    $LDFLAGS  << " -L#{recipe.path}/lib"
    # Windows の場合はライブラリの拡張子が .lib になることがあるため調整
    $libs = append_library($libs, name)
  end
end

# 依存ライブラリを順番にビルド
build_sdl_library("SDL3",       "3.4.4")
build_sdl_library("SDL3_image", "3.4.2")
build_sdl_library("SDL3_mixer", "3.2.0")
build_sdl_library("SDL3_ttf",   "3.2.2")

# Makefile を作成
create_makefile("nxruby/nxruby")
