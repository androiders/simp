project('simp', 'c', 'cpp')

fs = import('fs')

# Get the project name
project_name = meson.project_name()
# Define the config installation directory
#config_dir = get_option('datadir') / project_name  # Default: /usr/local/share/my_app
#user_config_dir = get_option('prefix') / '..' / '.config' / project_name  # For user-specific config

config_dir = fs.expanduser('~/.config') / project_name
x11deps = [dependency('x11', required: true), dependency('xtst', required: true), dependency('xi', required: true)]
jsonDeps = dependency('jansson', required: true)

incdir = include_directories('3rd-party/plog/include')

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

install_subdir('config', install_dir: config_dir)
