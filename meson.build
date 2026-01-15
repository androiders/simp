project('simp', 'c', 'cpp')

x11deps = [dependency('x11', required: true), dependency('xtst', required: true), dependency('xi', required: true)]
jsonDeps = dependency('jansson', required: true)

executable('simp', 'src/main.cpp','src/config.cpp',
    dependencies: x11deps + [jsonDeps])
    
configure_file(
  input: 'config/default.json',
  output: 'default.json',
  copy: true
)

configure_file(
  input: 'config/current.conf',
  output: 'current.conf',
  copy: true
)