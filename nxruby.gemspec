Gem::Specification.new do |spec|
  spec.name          = "nxruby"
  spec.version       = "0.1.0"
  spec.authors       = ["takenoko-pm"]
  spec.email         = ["229354118+takenoko-pm@users.noreply.github.com"]
  spec.summary       = "A 2D game framework based on SDL3."
  spec.description   = "NXRuby is a 2D game framework."
  spec.homepage      = "https://github.com/takenoko-pm/NXRuby"
  spec.license       = "MIT"
  
  spec.extensions    = ["ext/nxruby/extconf.rb"]
  spec.platform      = Gem::Platform::CURRENT

  if ENV['NXRUBY_PRECOMPILED'] == "true"
    spec.extensions = []
  else
    spec.extensions = ["ext/nxruby/extconf.rb"]
  end
  
  spec.files = Dir.glob("{lib,ext}/**/*").select { |f| File.file?(f) } + ["nxruby.gemspec", "LICENSE.txt","README.md"]
  spec.require_paths = ["lib"]
  spec.add_development_dependency "rake-compiler"
end