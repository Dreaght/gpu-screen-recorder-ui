project('gsr-overlay', ['c', 'cpp'], version : '1.0.0', default_options : ['warning_level=2', 'cpp_std=c++17'], subproject_dir : 'depends')

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c', 'cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c', 'cpp'])
endif

src = [
    'src/Theme.cpp',
    'src/gui/ScrollablePage.cpp',
    'src/gui/Button.cpp',
    'src/gui/RadioButton.cpp',
    'src/gui/Entry.cpp',
    'src/gui/CheckBox.cpp',
    'src/gui/ComboBox.cpp',
    'src/gui/Page.cpp',
    'src/gui/StaticPage.cpp',
    'src/gui/Widget.cpp',
    'src/gui/List.cpp',
    'src/gui/Utils.cpp',
    'src/gui/DropdownButton.cpp',
    'src/gui/Label.cpp',
    'src/Utils.cpp',
    'src/Config.cpp',
    'src/GsrInfo.cpp',
    'src/Process.cpp',
    'src/window_texture.c',
    'src/main.cpp',
]

mglpp_proj = subproject('mglpp')
mglpp_dep = mglpp_proj.get_variable('mglpp_dep')

dep = [
    mglpp_dep,
    dependency('xcomposite'),
]

prefix = get_option('prefix')
datadir = get_option('datadir')
gsr_overlay_resources_path = join_paths(prefix, datadir, 'gsr-overlay')

executable(
    meson.project_name(),
    src,
    install : true,
    dependencies : dep,
    cpp_args : '-DGSR_OVERLAY_RESOURCES_PATH="' + gsr_overlay_resources_path + '"',
)

install_subdir('images', install_dir : gsr_overlay_resources_path)