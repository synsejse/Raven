# Native Wayland

Build with `-DRAVEN_PLATFORM=wayland` to connect directly to a Wayland
compositor. This is a separate build choice: `auto` still selects X11 on
Linux. The Wayland library and examples do not link Xlib or use XWayland.
Rendering, including Raven's glass materials, uses Vulkan.

## Build and run

The repository's Nix shell includes all the dependencies:

```sh
nix-shell
cmake -S . -B build-wayland -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAVEN_PLATFORM=wayland
cmake --build build-wayland --parallel
./build-wayland/bin/raven_glass
# Other examples:
./build-wayland/bin/raven_editor
./build-wayland/bin/raven_studio
```

Run these inside a Wayland desktop session with a working Vulkan driver.
`WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` select the compositor; `DISPLAY` is
not used by this backend.

Without Nix, install CMake, a C11 compiler, pkg-config, `wayland-scanner`,
Wayland client/cursor headers and libraries (Wayland 1.20+),
`wayland-protocols` 1.31+ and `xkbcommon`. For example, on Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config git \
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev \
    libvulkan-dev
```

CMake generates the xdg-shell, xdg-decoration, viewporter and
fractional-scale bindings from the installed protocol XML. These generated
files stay in the build directory. Installed CMake consumers use the usual
`find_package(raven REQUIRED)` and `raven::raven` target; pkg-config resolves
their Wayland dependencies.

## Supported

- Native toplevel windows, close requests, titles, resize, minimum sizes,
  maximize/minimize requests and hide/show.
- Vulkan swapchains, transparent windows and all in-window glass materials.
- Pointer motion, buttons, scrolling and themed cursors.
- Keyboard layouts, modifiers, UTF-8 text, dead keys, compose and key repeat.
- Text clipboard through `wl_data_device`. Setting the clipboard before any
  input event keeps a local pending copy and publishes it after user input
  supplies the serial required by the compositor. Transfers are bounded to
  16 MiB, with timeouts for unresponsive peers.
- Integer output scaling, plus fractional scaling when the compositor
  advertises both fractional-scale and viewporter.
- Raven custom headers, including interactive move/resize and window buttons.
  Server decorations are requested for ordinary windows when xdg-decoration
  is available.
- Rendering on the render thread or UI thread, wakeups for posted tasks, and
  the existing Linux AT-SPI integration.

`Rwindow_get_native_handle` returns `R_WINDOW_SYSTEM_WAYLAND`, with a
`wl_display*` in `display` and a `wl_surface*` in `window`.

## Current limits

- External drag and drop, touch and text-input IME composition are not
  implemented. XKB dead keys and compose work independently of an IME.
- Native child viewports are not implemented. Use texture or draw-callback
  viewports; the X11/WGL OpenGL child-window example is not built.
- Desktop background blur is not requested from the compositor. Backdrop
  blur requests fall back to transparency; glass over Raven content still
  renders normally.
- On compositors without server decorations, use `R_WINDOW_CUSTOM_HEADER`
  and a Raven header bar (or XML `CustomHeader="true"`). There is no libdecor
  fallback for ordinary windows.
- Global position and centering are controlled by the compositor. Position
  setters are no-ops and the position getter returns `(0, 0)`. Always-on-top
  requests and whole-window opacity below 1 return unsupported. Restoring
  a minimized window or raising it without user activation is compositor
  policy; xdg-shell provides no portable request for that.
- One seat is used. App IDs currently use `org.raven.Raven`.

## Validation

```sh
# No X11 display is available to these processes:
env -u DISPLAY ./build-wayland/bin/raven_glass
env -u DISPLAY ctest --test-dir build-wayland -R '^(test_wayland|test_render)$' --output-on-failure
```

`test_wayland` exercises the real compositor and Vulkan: window creation,
pixel capture, application resize, hide/show, multiple windows and task
wakeups in both rendering modes. It skips when no Wayland display is
configured, and fails on connection/rendering errors when one is configured.
The initial implementation was checked on KDE Wayland; other compositors,
fractional scale transitions and interactive input still need broader testing.
