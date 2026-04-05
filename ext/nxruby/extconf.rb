# ext/nxruby/extconf.rb
require "mkmf"
require "fileutils"
require "mini_portile2"

# ビルドしたレシピを保持しておくハッシュ（依存関係の解決に使う）
$recipes = {}

# 共有ライブラリ（DLL等）を nxruby.so と同じ階層にコピーする関数
def copy_shared_libraries(recipe)
  ext = if RUBY_PLATFORM =~ /mingw|mswin/
          "dll"
        elsif RUBY_PLATFORM =~ /darwin/
          "dylib"
        else
          "so*"
        end

  # bin や lib フォルダにある共有ライブラリをカレントディレクトリにコピー
  Dir.glob("#{recipe.path}/**/*.{#{ext}}").each do |lib_file|
    # .dll.a や .so.a などのインポートライブラリは除外
    next if lib_file.end_with?(".a")
    FileUtils.cp(lib_file, ".", preserve: true)
  end
end

def build_sdl_component(repo_name, lib_name, version, depends_on = [])
  MiniPortileCMake.new(repo_name, version).tap do |recipe|
    archive_name = "#{lib_name}-#{version}.tar.gz"
    recipe.files = ["https://github.com/libsdl-org/#{repo_name}/releases/download/release-#{version}/#{archive_name}"]
    
    recipe.configure_options << "-DBUILD_SHARED_LIBS=ON"
    recipe.configure_options << "-DSDL_STATIC=OFF"
    
    depends_on.each do |dep_name|
      dep_path = $recipes[dep_name].path
      recipe.configure_options << "-DCMAKE_PREFIX_PATH=#{dep_path}"
    end

    recipe.cook
    
    $INCFLAGS << " -I#{recipe.path}/include"
    $LDFLAGS  << " -L#{recipe.path}/lib"
    $libs = append_library($libs, lib_name)
    
    # LinuxやMacで、実行時に同じフォルダのライブラリを探せるようにする呪文(rpath)
    if RUBY_PLATFORM =~ /darwin|linux/
      $LDFLAGS << " -Wl,-rpath,."
    end
    
    # 完成したDLL等を nxruby.so の横に持ってくる
    copy_shared_libraries(recipe)
    
    $recipes[repo_name] = recipe
  end
end

# 1. 本体（依存なし）
build_sdl_component("SDL", "SDL3", "3.4.4")

# 2. 拡張（SDL本体に依存）
build_sdl_component("SDL_image", "SDL3_image", "3.4.2", ["SDL"])
build_sdl_component("SDL_mixer", "SDL3_mixer", "3.2.0", ["SDL"])
build_sdl_component("SDL_ttf",   "SDL3_ttf",   "3.2.2", ["SDL"])

create_makefile("nxruby/nxruby")
