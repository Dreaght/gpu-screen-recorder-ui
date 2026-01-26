![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder UI
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) in the style of ShadowPlay.\
The application is currently primarly designed for X11 but it can run on Wayland as well through XWayland, with some caveats because of Wayland limitations.

# Installation
If you are using an Arch Linux based distro then you can find gpu screen recorder ui on aur under the name gpu-screen-recorder-ui (`yay -S gpu-screen-recorder-ui`).\
If you are running another distro then you can run `sudo ./install.sh`, but you need to manually install the dependencies, as described below.\
You can also install gpu screen recorder from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder) which includes this UI.

# Usage
Press `Left Alt+Z` to show/hide the UI. Go into settings (the icon on the right) to view all of the different hotkeys configured.\
You can start the overlay UI and make it start automatically on system startup by running `systemctl enable --now --user gpu-screen-recorder-ui`.
Alternatively you can run `gsr-ui` and go into settings and enable start on system startup setting.\
If you use a non-systemd distro and want to start the UI on system startup then you have to manually add `gsr-ui launch-daemon` to your system startup script.\
A program called `gsr-ui-cli` is also installed when installing this software. This can be used to remotely control the UI. Run `gsr-ui-cli --help` to list the available commands.

# Dependencies
GPU Screen Recorder UI uses meson build system so you need to install `meson` to build GPU Screen Recorder UI.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder UI:

* x11 (libx11, libxrandr, libxrender, libxcomposite, libxfixes, libxext, libxi, libxcursor)
* libglvnd (which provides libgl, libglx and libegl)
* linux-api-headers
* libpulse (libpulse-simple)
* libdrm
* libdbus
* wayland (wayland-client, wayland-egl, wayland-scanner)
* setcap (libcap)

## Runtime dependencies
There are also additional dependencies needed at runtime:

* [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/) (version 5.0.0 or later)
* [GPU Screen Recorder Notification](https://git.dec05eba.com/gpu-screen-recorder-notification/)

## Translation guide
GPU Screen Recorder UI uses its own translation system for it. See the `translations/template.txt` file for the translation template.

You can even translate the program without building it from source code by just creating a translation file in `/usr/share/gsr-ui/translations/` folder (if installed as a system package).

## Program behavior notes
By default this program has to grab all keyboards and creates a virtual keyboard (`gsr-ui virtual keyboard`) to make global hotkeys work on all Wayland compositors.\
This might cause issues for you if you use keyboard remapping software. To workaround this you can go into settings and select "Yes, but only grab virtual devices" or "Yes, but don't grab devices".\
If you use keyboard remapping software such as keyd then make sure to make it ignore "gsr-ui virtual keyboard" (dec0:5eba device id), otherwise your keyboard can get locked
as gpu screen recorder tries to grab keys and keyd grabs gpu screen recorder, leading to a lock.\
If you are stuck in such a lock where you cant press and keyboard keys you can press (left) ctrl+shift+alt+esc to close gpu screen recorder and remove it from system startup.

# License
This software is licensed under GPL-3.0-only, see the LICENSE file for more information. Files under `fonts/` directory belong to the Noto Sans Google fonts project and they are licensed under `SIL Open Font License`.\
`images/default.cur` it part of the [Adwaita icon theme](https://gitlab.gnome.org/GNOME/adwaita-icon-theme/-/tree/master) which is licensed under `CC BY-SA 3.0`.\
The controller buttons under `images/` were created by [Julio Cacko](https://juliocacko.itch.io/free-input-prompts) and they are licensed under `CC0 1.0 Universal`.\
The PlayStation logo under `images/` was created by [ArksDigital](https://arks.itch.io/ps4-buttons) and it's licensed under `CC BY 4.0`.

# Reporting bugs, contributing patches, questions or donation
See [https://git.dec05eba.com/?p=about](https://git.dec05eba.com/?p=about).\
I'm looking for somebody that can create sound effects for the notifications.

# Demo
[![Click here to watch a demo video on youtube](https://img.youtube.com/vi/SOqXusCTXXA/0.jpg)](https://www.youtube.com/watch?v=SOqXusCTXXA)

# Screenshots
![](https://dec05eba.com/images/front_page.jpg)
![](https://dec05eba.com/images/settings_page.jpg)

# Known issues
* When the UI is open the wallpaper is shown instead of the game on Hyprland. This is an issue with Hyprland. It cant be fixed until the UI is redesigned to not be a fullscreen overlay. Change your waybar dock mode to "dock" in its config to fix this.
* Opening the UI when a game is fullscreen can mess up the game window a bit on Hyprland. This is an issue with Hyprland. Change your waybar dock mode to "dock" in its config to fix this.
* The background of the UI is black when opening the UI while a Wayland application is focused on COSMIC. This is an issue with COSMIC.
* Unable to close the region selection with escape key while a Wayland application is focused on COSMIC. This is an issue with COSMIC.

# FAQ
## I get an error when trying to start the gpu-screen-recorder-ui.service systemd service
If you have previously used the flatpak version of GPU Screen Recorder with the new UI then the non-flatpak version of the systemd service will conflict with that. Run `gsr-ui` to fix that.
## I use a non-qwerty keyboard layout and I have an issue with incorrect keys registered in the software
This is a KDE Plasma Wayland issue. Use `setxkbmap <language>` command, for example `setxkbmap se` to make sure X11 applications (such as this one) gets updated to use your languages keyboard layout.
## "Save to clipboard" option doesn't work for screenshots
Some Wayland compositors don't support copying images on the clipboard between X11 and Wayland applications. GPU Screen Recorder UI is an X11 application. It can't be done properly on Wayland
since Wayland doesn't support a non-focused application from setting the clipboard, so it can't work with GPU Screen Recorder hotkey usage. Use X11 if you want a functioning desktop.
## The controller hotkey and steam overlap (home button brings up steam overlay)
You can either disable the steam overlay or in steam click Steam->Settings->Controller and then click "Begin Test" under "Test Device Inputs". Click on "Setup Device Inputs" and configure controller buttons there and when you get to the home button press X to unbind it from steam.
