project('gsr-ui', ['c', 'cpp'], version : '1.8.2', default_options : ['warning_level=2', 'cpp_std=c++17'], subproject_dir : 'depends')

add_project_arguments('-D_FILE_OFFSET_BITS=64', language : ['c', 'cpp'])

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c', 'cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c', 'cpp'])
endif

src = [
    'src/window_texture.c',
    'src/Theme.cpp',
    'src/gui/Widget.cpp',
    'src/gui/ScrollablePage.cpp',
    'src/gui/Button.cpp',
    'src/gui/RadioButton.cpp',
    'src/gui/Entry.cpp',
    'src/gui/CheckBox.cpp',
    'src/gui/ComboBox.cpp',
    'src/gui/Page.cpp',
    'src/gui/StaticPage.cpp',
    'src/gui/PageStack.cpp',
    'src/gui/List.cpp',
    'src/gui/Utils.cpp',
    'src/gui/DropdownButton.cpp',
    'src/gui/Label.cpp',
    'src/gui/Image.cpp',
    'src/gui/LineSeparator.cpp',
    'src/gui/CustomRendererWidget.cpp',
    'src/gui/FileChooser.cpp',
    'src/gui/SettingsPage.cpp',
    'src/gui/ScreenshotSettingsPage.cpp',
    'src/gui/GlobalSettingsPage.cpp',
    'src/gui/GsrPage.cpp',
    'src/gui/Subsection.cpp',
    'src/GlobalHotkeys/GlobalHotkeysX11.cpp',
    'src/GlobalHotkeys/GlobalHotkeysLinux.cpp',
    'src/GlobalHotkeys/GlobalHotkeysJoystick.cpp',
    'src/CursorTracker/CursorTrackerX11.cpp',
    'src/CursorTracker/CursorTrackerWayland.cpp',
    'src/Utils.cpp',
    'src/WindowUtils.cpp',
    'src/RegionSelector.cpp',
    'src/WindowSelector.cpp',
    'src/Config.cpp',
    'src/GsrInfo.cpp',
    'src/Process.cpp',
    'src/Overlay.cpp',
    'src/AudioPlayer.cpp',
    'src/Hotplug.cpp',
    'src/ClipboardFile.cpp',
    'src/LedIndicator.cpp',
    'src/Rpc.cpp',
    'src/main.cpp',
]

subdir('protocol')
src += protocol_src

mglpp_proj = subproject('mglpp')
mglpp_dep = mglpp_proj.get_variable('mglpp_dep')

prefix = get_option('prefix')
datadir = get_option('datadir')
gsr_ui_resources_path = join_paths(prefix, datadir, 'gsr-ui')

add_project_arguments('-DGSR_UI_VERSION="' + meson.project_version() + '"', language: ['c', 'cpp'])
add_project_arguments('-DGSR_FLATPAK_VERSION="5.9.1"', language: ['c', 'cpp'])

executable(
    meson.project_name(),
    src,
    install : true,
    dependencies : [
        mglpp_dep,
        dependency('threads'),
        dependency('xcomposite'),
        dependency('xfixes'),
        dependency('xext'),
        dependency('xi'),
        dependency('xcursor'),
        dependency('xrandr'),
        dependency('libpulse-simple'),
        dependency('libdrm'),
        dependency('wayland-client'),
    ],
    cpp_args : '-DGSR_UI_RESOURCES_PATH="' + gsr_ui_resources_path + '"',
)

executable(
    'gsr-global-hotkeys',
    [
        'tools/gsr-global-hotkeys/hotplug.c',
        'tools/gsr-global-hotkeys/keyboard_event.c',
        'tools/gsr-global-hotkeys/keys.c',
        'tools/gsr-global-hotkeys/leds.c',
        'tools/gsr-global-hotkeys/main.c'
    ],
    c_args : '-fstack-protector-all',
    install : true
)

executable(
    'gsr-ui-cli',
    [
        'tools/gsr-ui-cli/main.c'
    ],
    install : true
)

install_subdir('images', install_dir : gsr_ui_resources_path)
install_subdir('fonts', install_dir : gsr_ui_resources_path)

if get_option('systemd') == true
    install_data(files('extra/gpu-screen-recorder-ui.service'), install_dir : 'lib/systemd/user')
endif

if get_option('capabilities') == true
    meson.add_install_script('meson_post_install.sh')
endif
