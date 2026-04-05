require "rubygems/package_task"
require "rake/extensiontask"

# 1. gemspec を読み込む
spec = Gem::Specification.load("nxruby.gemspec")

# 2. Gem をパッケージ化するための標準タスクを定義
Gem::PackageTask.new(spec)

# 3. rake-compiler に spec を渡す
Rake::ExtensionTask.new("nxruby", spec) do |ext|
  ext.lib_dir = "lib/nxruby"
  ext.cross_compile = false
end