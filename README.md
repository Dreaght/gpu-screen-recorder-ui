![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder UI
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) in the style of ShadowPlay.\
The application is currently primarly designed for X11 but it can run on Wayland as well through XWayland, with some caveats because of Wayland limitations.\
Note: This software is still in early alpha. Expect bugs, and please report any if you experience them. Some are already known, but it doesn't hurt to report them anyways.\
You can report an issue by emailing the issue to dec05eba@protonmail.com.

# Usage
Run `gsr-ui` and press `Alt+Z` to show/hide the UI. You can start the overlay UI at system startup by running `systemctl enable --now --user gpu-screen-recorder-ui`.
There is also an option in the settings to enable/disable starting the program on system startup. This option only works on systems that use systemd.
You have to manually add `gsr-ui` to system startup on systems that uses another init system.

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
At the moment different keyboard layouts are not supported. The physical layout of keys are used for global hotkeys. If your Z and Y keys are swapped for example then you need to press Alt+Y instead of Alt+Z to open/hide the UI.\
If you experience this issue then please email dec05eba@protonmail.com to get it fixed.\
This program has to grab all keyboards and create a virtual keyboard (`gsr-ui virtual keyboard`) to make global hotkeys work on all Wayland compositors.
This might cause issues for you if you use input remapping software. If this is an issue for you then please email dec05eba@protonmail.com and an option to workaround the issue could be added.

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
* When the UI is open the wallpaper is shown instead of the game on Hyprland and Sway. This is an issue with Hyprland and Sway. It cant be fixed until the UI is redesigned to not be a fullscreen overlay.
* Different keyboard layouts are not supported at the moment. The physical layout of keys are used for global hotkeys. If your Z and Y keys are swapped for example then you need to press Alt+Y instead of Alt+Z to open/hide the UI. If you experience this issue then please email dec05eba@protonmail.com to get it fixed.