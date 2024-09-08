# GPU Screen Recorder Overlay
A fullscreen overlay UI for [GPU Screen Recorder](https://git.dec05eba.com/gpu-screen-recorder/about/), in the style of ShadowPlay.

# Dependencies
GPU Screen Recorder overlay uses meson build system so you need to install `meson` to build GPU Screen Recorder overlay.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder overlay:

* x11 (libx11, libxrandr, libxrender, libxfixes)
* libglvnd (which provides libgl, libglx and libegl)

## Runtime dependencies
There are also additional dependencies needed at runtime:

* Noto fonts

# Installation
Run `sudo ./install.sh`. This will install gsr-overlay to `/usr/bin/gsr-overlay`. You can run meson commands manually to install gsr-overlay to another directory.
