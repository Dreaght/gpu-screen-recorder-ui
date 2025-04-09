![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder UI
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) in the style of ShadowPlay.\
The application is currently primarly designed for X11 but it can run on Wayland as well through XWayland, with some caveats because of Wayland limitations.

# Usage
You can start the overlay UI and make it start automatically on system startup by running `systemctl enable --now --user gpu-screen-recorder-ui`.
Alternatively you can run `gsr-ui` and go into settings and enable start on system startup setting.\
Press `Left Alt+Z` to show/hide the UI. Go into settings to view all of the different hotkeys configured.\
If you use a non-systemd distro and want to start the UI on system startup then you have to manually add `gsr-ui` to your system startup script.\
A program called `gsr-ui-cli` is also installed when installing this software. This can be used to remotely control the UI. Run `gsr-ui-cli --help` to list the available commands.

# Installation
If you are using an Arch Linux based distro then you can find gpu screen recorder ui on aur under the name gpu-screen-recorder-ui (`yay -S gpu-screen-recorder-ui`).\
If you are running another distro then you can run `sudo ./install.sh`, but you need to manually install the dependencies, as described below.\
You can also install gpu screen recorder from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes both this UI and gpu-screen-recorder so no need to install that first.

# Dependencies
GPU Screen Recorder UI uses meson build system so you need to install `meson` to build GPU Screen Recorder UI.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder UI:

* x11 (libx11, libxrandr, libxrender, libxcomposite, libxfixes, libxext, libxi)
* libxcursor
* libglvnd (which provides libgl, libglx and libegl)
* linux-api-headers
* libpulse (libpulse-simple)
* libdrm
* wayland-client
* wayland-scanner

## Runtime dependencies
There are also additional dependencies needed at runtime:

* [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/) (version 5.0.0 or later)
* [GPU Screen Recorder Notification](https://git.dec05eba.com/gpu-screen-recorder-notification/)

## Program behavior notes
This program has to grab all keyboards and create a virtual keyboard (`gsr-ui virtual keyboard`) to make global hotkeys work on all Wayland compositors.\
This might cause issues for you if you use input remapping software. To workaround this you can go into settings and select "Only grab virtual devices".\
If you use input remapping software such as keyd then make sure to make it ignore "gsr-ui virtual keyboard", otherwise your keyboard can get locked
as gpu screen recorder tries to grab keys and keyd grabs gpu screen recorder, leading to a lock.\
If you are stuck in such a lock where you cant press and keyboard keys you can press (left) ctrl+shift+alt+esc to close gpu screen recorder and remove it from system startup.

# License
This software is licensed under GPL3.0-only. Files under `fonts/` directory belong to the Noto Sans Google fonts project and they are licensed under `SIL Open Font License`.\
`images/default.cur` it part of the [Adwaita icon theme](https://gitlab.gnome.org/GNOME/adwaita-icon-theme/-/tree/master) which is licensed under `CC BY-SA 3.0`.\
The controller buttons under `images/` were created by [Julio Cacko](https://juliocacko.itch.io/free-input-prompts) and they are licensed under `CC0 1.0 Universal`.\
The PlayStation logo under `images/` was created by [ArksDigital](https://arks.itch.io/ps4-buttons) and it's are licensed under `CC BY 4.0`.

# Reporting bugs, contributing patches, questions or donation
See [https://git.dec05eba.com/?p=about](https://git.dec05eba.com/?p=about).\
I'm looking for somebody that can create sound effects for the notifications.

# Demo
[![Click here to watch a demo video on youtube](https://img.youtube.com/vi/SOqXusCTXXA/0.jpg)](https://www.youtube.com/watch?v=SOqXusCTXXA)

# Screenshots
![](https://dec05eba.com/images/front_page.jpg)
![](https://dec05eba.com/images/settings_page.jpg)

# Known issues
* When the UI is open the wallpaper is shown instead of the game on Hyprland. This is an issue with Hyprland. It cant be fixed until the UI is redesigned to not be a fullscreen overlay.
* Opening the UI when a game is fullscreened can mess up the game window a bit on Hyprland. I believe this is an issue with Hyprland.

# FAQ
## I get an error when trying to start the gpu-screen-recorder-ui.service systemd service
If you have previously used the flatpak version of GPU Screen Recorder with the new UI then the non-flatpak version of the systemd service will conflict with that. Run `gsr-ui` to fix that.
