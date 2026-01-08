project('simp', 'c', 'cpp')

x11deps = [dependency('x11', required: true), dependency('xtst', required: true), dependency('xi', required: true)]
jsonDeps = dependency('jansson', required: true)

executable('simp', 'src/main.cpp',
    dependencies: x11deps + [jsonDeps])
    
