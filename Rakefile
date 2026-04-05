# Rakefile
require "rake/extensiontask"

Rake::ExtensionTask.new("nxruby") do |ext|
  ext.lib_dir = "lib/nxruby"
  # Windows, Linux, macOS のターゲットを指定
  ext.cross_compile = true
  ext.cross_platform = ['x64-mingw-ucrt', 'x86_64-linux', 'arm64-darwin']
end