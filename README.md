![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder UI
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) in the style of ShadowPlay.\
The application is currently primarly designed for X11 but it can run on Wayland as well through XWayland, with some caveats because of Wayland limitations.\
Note: This software is still in early alpha. Expect bugs, and please report any if you experience them. Some are already known, but it doesn't hurt to report them anyways.\
You can report an issue by emailing the issue to dec05eba@protonmail.com.

# Usage
Run `gsr-ui` and press `Left Alt+Z` to show/hide the UI. You can start the overlay UI at system startup by running `systemctl enable --now --user gpu-screen-recorder-ui`.
There is also an option in the settings to enable/disable starting the program on system startup. This option only works on systems that use systemd.
You have to manually add `gsr-ui` to system startup on systems that uses another init system.\
A program called `gsr-ui-cli` is also installed when installing this software. This can be used to remotely control the UI. Run `gsr-ui-cli --help` to list the available commands.

# Installation
If you are using an Arch Linux based distro then you can find gpu screen recorder ui on aur under the name gpu-screen-recorder-ui (`yay -S gpu-screen-recorder-ui`).\
If you are running another distro then you can run `sudo ./install.sh`, but you need to manually install the dependencies, as described below.\
You can also install gpu screen recorder (the gtk gui version) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder). This flatpak includes both this UI and gpu-screen-recorder so no need to install that first.

# Dependencies
GPU Screen Recorder UI uses meson build system so you need to install `meson` to build GPU Screen Recorder UI.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder UI:

* x11 (libx11, libxrandr, libxrender, libxcomposite, libxfixes, libxi)
* libxcursor
* libglvnd (which provides libgl, libglx and libegl)
* linux-api-headers

## Runtime dependencies
There are also additional dependencies needed at runtime:

* [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/)
* [GPU Screen Recorder Notification](https://git.dec05eba.com/gpu-screen-recorder-notification/)

## Program behavior notes
This program has to grab all keyboards and create a virtual keyboard (`gsr-ui virtual keyboard`) to make global hotkeys work on all Wayland compositors.
This might cause issues for you if you use input remapping software. To workaround this you can go into settings and select "Only grab virtual devices"

# License
This software is licensed under GPL3.0-only. Files under `fonts/` directory belong to the Noto Sans Google fonts project and they are licensed under `SIL Open Font License`.

# Demo
[![Click here to watch a demo video on youtube](https://img.youtube.com/vi/SOqXusCTXXA/0.jpg)](https://www.youtube.com/watch?v=SOqXusCTXXA)

# Screenshots
![](https://dec05eba.com/images/gsr-overlay-screenshot-front.webp)
![](https://dec05eba.com/images/gsr-overlay-screenshot-settings.webp)

# Donations
If you want to donate you can donate via bitcoin or monero.
* Bitcoin: bc1qqvuqnwrdyppf707ge27fqz2n9y9gu7lf5ypyuf
* Monero: 4An9kp2qW1C9Gah7ewv4JzcNFQ5TAX7ineGCqXWK6vQnhsGGcRpNgcn8r9EC3tMcgY7vqCKs3nSRXhejMHBaGvFdN2egYet

# Known issues
* When the UI is open the wallpaper is shown instead of the game on Hyprland. This is an issue with Hyprland. It cant be fixed until the UI is redesigned to not be a fullscreen overlay.
* Opening the UI when a game is fullscreened can mess up the game window a bit on Hyprland. I believe this is an issue with Hyprland.

# FAQ
## I get an error when trying to start the gpu-screen-recorder-ui.service systemd service
If you have previously used the flatpak version of GPU Screen Recorder with the new UI then non-flatpak version of the systemd service will conflict with that.
Run these commands to first remove the flatpak version of the systemd service:
```
systemctl stop --user gpu-screen-recorder-ui
rm ~/.local/share/systemd/user/gpu-screen-recorder-ui.service
systemctl --user daemon-reload
```
and then start and enable the non-flatpak systemd service:
```
systemctl enable --now --user gpu-screen-recorder-ui`
```