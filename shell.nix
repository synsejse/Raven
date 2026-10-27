{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  name = "raven";

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = with pkgs; [
    libx11
    libGL
    vulkan-headers
    vulkan-loader
    wayland
    wayland-protocols
    libxkbcommon
    libffi
  ];

  shellHook = ''
    # Prefer the toolchain's libc over /usr/lib in inherited FHS environments.
    # FindPkgConfig resolves Wayland's -lm to an absolute build dependency.
    export CMAKE_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pkgs.stdenv.cc.libc ]}''${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"

    # Raven loads Vulkan with dlopen rather than linking it at build time.
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ]}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  '';
}
