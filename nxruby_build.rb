MRuby::Build.new do |conf|
  toolchain :visualcpp
  conf.cc.flags << '/utf-8'
  
  # 標準Gemを読み込む
  conf.gembox 'default'

  # あなたの Gem のパス（deps/SDL3 を探すためにも必要）
  nxruby_dir = File.expand_path(File.dirname(__FILE__))
  conf.gem nxruby_dir

  # ★ここが重要！ビルド全体のリンカに SDL3 を追加する
  # これにより、mrdb.exe などを作る際にも SDL3.lib がリンクされます
  conf.linker do |linker|
    linker.library_paths << "#{nxruby_dir}/deps/SDL3/lib/x64"
    linker.library_paths << "#{nxruby_dir}/deps/SDL3_image/lib/x64"
    linker.library_paths << "#{nxruby_dir}/deps/SDL3_ttf/lib/x64"
    linker.library_paths << "#{nxruby_dir}/deps/SDL3_mixer/lib/x64"

    linker.libraries << 'SDL3'
    linker.libraries << 'SDL3_image'
    linker.libraries << 'SDL3_ttf'
    linker.libraries << 'SDL3_mixer'

    linker.libraries << 'ws2_32'
    linker.libraries << 'user32'
    linker.libraries << 'shell32'
    linker.libraries << 'advapi32' # SDL3が必要とする場合があります
    linker.libraries << 'setupapi' # SDL3が必要とする場合があります
    linker.libraries << 'gdi32'    # SDL3が必要とする場合があります
    linker.libraries << 'imm32'    # SDL3が必要とする場合があります
    linker.libraries << 'ole32'    # SDL3が必要とする場合があります
    linker.libraries << 'oleaut32' # SDL3が必要とする場合があります
    linker.libraries << 'version'  # SDL3が必要とする場合があります
  end
end