# ext/nxruby/extconf.rb
require "mkmf"
require "fileutils"
require "mini_portile2"

$recipes = {}

def copy_shared_libraries(recipe)
  ext = if RUBY_PLATFORM =~ /mingw|mswin/
          "dll"
        elsif RUBY_PLATFORM =~ /darwin/
          "dylib"
        else
          "so*"
        end

  # コピー先を lib/nxruby に指定
  lib_dir = File.expand_path("../../lib/nxruby", __dir__)
  FileUtils.mkdir_p(lib_dir)

  Dir.glob("#{recipe.path}/**/*.{#{ext}}").each do |lib_file|
    next if lib_file.end_with?(".a")
    FileUtils.cp(lib_file, lib_dir, preserve: true)
  end
end

def build_sdl_component(repo_name, lib_name, version, depends_on = [])
  MiniPortileCMake.new(repo_name, version).tap do |recipe|
    archive_name = "#{lib_name}-#{version}.tar.gz"
    recipe.files = ["https://github.com/libsdl-org/#{repo_name}/releases/download/release-#{version}/#{archive_name}"]
    
    recipe.configure_options << "-DBUILD_SHARED_LIBS=ON"
    recipe.configure_options << "-DSDL_STATIC=OFF"
    
    if RUBY_PLATFORM =~ /mingw|mswin/
      recipe.configure_options << "-DSDL_OPENGLES=OFF"

      if lib_name == "SDL3_ttf"
        recipe.configure_options << "-DSDLTTF_VENDORED=ON"
      end
    end

    depends_on.each do |dep_name|
      dep_path = $recipes[dep_name].path
      recipe.configure_options << "-DCMAKE_PREFIX_PATH=#{dep_path}"
    end

    recipe.cook
    
    $INCFLAGS << " -I#{recipe.path}/include"
    $LDFLAGS  << " -L#{recipe.path}/lib"
    $libs = append_library($libs, lib_name)
    
    # Macの場合は実行ファイルと同じ場所を探すようにする
    if RUBY_PLATFORM =~ /darwin/
      $LDFLAGS << " -Wl,-rpath,@loader_path/."
    end
    
    copy_shared_libraries(recipe)
    $recipes[repo_name] = recipe
  end
end

build_sdl_component("SDL", "SDL3", "3.4.4")
build_sdl_component("SDL_image", "SDL3_image", "3.4.2", ["SDL"])
build_sdl_component("SDL_mixer", "SDL3_mixer", "3.2.0", ["SDL"])
build_sdl_component("SDL_ttf",   "SDL3_ttf",   "3.2.2", ["SDL"])

create_makefile("nxruby/nxruby")