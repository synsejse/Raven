# Native Wayland; generate protocol bindings from the installed official XML.
find_package(PkgConfig REQUIRED)
pkg_check_modules(RAVEN_WAYLAND REQUIRED IMPORTED_TARGET wayland-client>=1.20 wayland-cursor xkbcommon)
pkg_check_modules(RAVEN_WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.31)
pkg_get_variable(RAVEN_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(RAVEN_WAYLAND_SCANNER NAMES wayland-scanner)
if(NOT RAVEN_WAYLAND_SCANNER)
	message(FATAL_ERROR "Raven: the Wayland backend requires wayland-scanner")
endif()

set(RAVEN_WAYLAND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
file(MAKE_DIRECTORY "${RAVEN_WAYLAND_GENERATED_DIR}")
set(RAVEN_WAYLAND_PROTOCOL_SOURCES "")
foreach(protocol
	stable/xdg-shell/xdg-shell.xml
	unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	stable/viewporter/viewporter.xml
	staging/fractional-scale/fractional-scale-v1.xml
)
	get_filename_component(name "${protocol}" NAME_WE)
	set(xml "${RAVEN_WAYLAND_PROTOCOLS_DIR}/${protocol}")
	set(header "${RAVEN_WAYLAND_GENERATED_DIR}/${name}-client-protocol.h")
	set(code "${RAVEN_WAYLAND_GENERATED_DIR}/${name}-protocol.c")
	add_custom_command(OUTPUT "${header}" "${code}"
		COMMAND "${RAVEN_WAYLAND_SCANNER}" client-header "${xml}" "${header}"
		COMMAND "${RAVEN_WAYLAND_SCANNER}" private-code "${xml}" "${code}"
		DEPENDS "${xml}"
		VERBATIM
	)
	list(APPEND RAVEN_WAYLAND_PROTOCOL_SOURCES "${header}" "${code}")
endforeach()
