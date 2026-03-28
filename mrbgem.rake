MRuby::Gem::Specification.new('mruby-nxruby') do |spec|
  spec.license = 'MIT'
  spec.author  = 'takenoko-pm'
  spec.summary = 'NXRuby framework for mruby'

  # パスをきれいに整理
  spec.cc.include_paths << "#{build.root}/src"
  spec.cc.include_paths << "#{dir}/include"
  spec.cc.include_paths << "#{dir}/deps/SDL3/include"
  spec.cc.include_paths << "#{dir}/deps/SDL3_image/include"

  # ライブラリ設定
  spec.linker.library_paths << "#{dir}/deps/SDL3/lib/x64"
  spec.linker.library_paths << "#{dir}/deps/SDL3_image/lib/x64"
  spec.linker.libraries << 'SDL3'
  spec.linker.libraries << 'SDL3_image'

  if RUBY_PLATFORM =~ /mingw|mswin/
    spec.linker.libraries << 'ws2_32'
  end

  spec.bins = %w(nxruby)
end