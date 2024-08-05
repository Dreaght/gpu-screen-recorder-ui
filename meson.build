project('gsr-overlay', ['cpp'], version : '1.0.0', default_options : ['warning_level=2', 'cpp_std=c++17'], subproject_dir : 'depends')

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['cpp'])
endif

src = [
    'src/Theme.cpp',
    'src/gui/ScrollablePage.cpp',
    'src/gui/Button.cpp',
    'src/gui/Entry.cpp',
    'src/gui/ComboBox.cpp',
    'src/gui/Page.cpp',
    'src/gui/StaticPage.cpp',
    'src/gui/Widget.cpp',
    'src/gui/List.cpp',
    'src/gui/Utils.cpp',
    'src/gui/DropdownButton.cpp',
    'src/gui/Label.cpp',
    'src/GsrInfo.cpp',
    'src/Process.cpp',
    'src/main.cpp',
]

mglpp_proj = subproject('mglpp')
mglpp_dep = mglpp_proj.get_variable('mglpp_dep')

dep = [
    mglpp_dep,
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