require "rake/extensiontask"

Rake::ExtensionTask.new("nxruby") do |ext|
  ext.lib_dir = "lib/nxruby"
  # GitHub Actionsの各OS環境でそのままネイティブビルドするため、falseにする
  ext.cross_compile = false
end