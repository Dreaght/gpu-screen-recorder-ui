![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder UI
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) in the style of ShadowPlay.\
Note: This software is still in early alpha. Expect bugs, and please report any if you experience them. Some are already known, but it doesn't hurt to report them anyways.\
You can report an issue by emailing the issue to dec05eba@protonmail.com.

# Usage
Run `gsr-ui` and press `Alt+Z` to show/hide the UI. You can start the overlay UI at system startup by running `systemctl enable --now --user gpu-screen-recorder-ui`.

# Installation
If you are using an Arch Linux based distro then you can find gpu screen recorder ui on aur under the name gpu-screen-recorder-ui (`yay -S gpu-screen-recorder-ui`).\
If you are running another distro then you can run `sudo ./install.sh`, but you need to manually install the dependencies, as described below.

# Dependencies
GPU Screen Recorder UI uses meson build system so you need to install `meson` to build GPU Screen Recorder UI.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder UI:

* x11 (libx11, libxrandr, libxrender, libxfixes, libxcomposite)
* libglvnd (which provides libgl, libglx and libegl)

## Runtime dependencies
* [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/)
* [GPU Screen Recorder Notification](https://git.dec05eba.com/gpu-screen-recorder-notification/)

# License
This software is licensed under GPL3.0-only. Files under `fonts/` directory are licensed under `SIL Open Font License`.

# Demo
[![Click here to watch a demo video on youtube](https://img.youtube.com/vi/SOqXusCTXXA/0.jpg)](https://www.youtube.com/watch?v=SOqXusCTXXA)

# Screenshots
![](https://dec05eba.com/images/gsr-overlay-screenshot-front.webp)
![](https://dec05eba.com/images/gsr-overlay-screenshot-settings.webp)

# Donations
If you want to donate you can donate via bitcoin or monero.
* Bitcoin: bc1qqvuqnwrdyppf707ge27fqz2n9y9gu7lf5ypyuf
* Monero: 4An9kp2qW1C9Gah7ewv4JzcNFQ5TAX7ineGCqXWK6vQnhsGGcRpNgcn8r9EC3tMcgY7vqCKs3nSRXhejMHBaGvFdN2egYet