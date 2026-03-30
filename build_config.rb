# ==========================================
# 1. Windows用のビルド
# ==========================================
MRuby::Build.new do |conf|
  toolchain :visualcpp 
  conf.gembox 'default'
end
