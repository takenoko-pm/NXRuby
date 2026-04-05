if RUBY_PLATFORM =~ /mingw|mswin/
  require 'ruby_installer'
  # 区切り文字を Windows 仕様 (\) に変換してDLLパスを登録
  dll_dir = File.expand_path("nxruby", __dir__).tr("/", "\\")
  RubyInstaller::Runtime.add_dll_directory(dll_dir)
end

require_relative "nxruby/nxruby"