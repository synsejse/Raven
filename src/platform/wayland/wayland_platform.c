/** Native Wayland windows. Vulkan owns the buffers and presentation. */
#include "wayland_internal.h"
#include "../atspi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

rvn_wayland_t g_wl;

int rvn_wl_fd_flags(int fd) {
	int flags = fcntl(fd, F_GETFL);
	return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) >= 0;
}

rvn_pwindow_t* rvn_wl_find(struct wl_surface* surface) {
	rvn_pwindow_t* window;
	for (window = g_wl.windows; window; window = window->next) if (window->surface == surface) return window;
	return NULL;
}

/* Protocol callbacks may run while the UI thread waits on the renderer. Never
 * reenter Raven here: defer application events until the normal event pump. */
void rvn_wl_emit(rvn_pwindow_t* window, const rvn_pevent_t* event) {
	rvn_wl_event_t* pending;
	if (!window) return;
	pending = (rvn_wl_event_t*)rvn_calloc(sizeof(*pending));
	pending->window = window;
	pending->input_serial = g_wl.input_serial;
	pending->event = *event;
	pending->event.user = window->user;
	if (g_wl.events_tail) g_wl.events_tail->next = pending;
	else g_wl.events_head = pending;
	g_wl.events_tail = pending;
}

static void rvn_wl_connection_error(void) {
	if (g_wl.failed) return;
	g_wl.failed = 1;
	rvn_error(R_ERROR_PLATFORM, R_SEVERITY_ERROR, "Wayland: compositor connection failed (%s)",
		strerror(wl_display_get_error(g_wl.display)));
	Rquit(1);
}

int rvn_wl_dispatch(double timeout, int extra_fd) {
	struct pollfd fds[4];
	int milliseconds, result, flush, prepared = 0;
	if (!g_wl.display || g_wl.failed) return R_FALSE;
	/* The prepare/read/cancel protocol is essential: Vulkan has its own event
 	 * queue and may also read from this display on the render thread. */
	while (wl_display_prepare_read(g_wl.display) != 0) {
		if (wl_display_dispatch_pending(g_wl.display) < 0) goto failed;
		timeout = 0;
	}
	prepared = 1;
	rvn_wl_clipboard_flush();
	if (rvn_wl_clipboard_pending() && (timeout < 0 || timeout > 0.01)) timeout = 0.01;
	flush = wl_display_flush(g_wl.display);
	if (flush < 0 && errno != EAGAIN) goto failed;
	memset(fds, 0, sizeof(fds));
	fds[0].fd = wl_display_get_fd(g_wl.display);
	fds[0].events = POLLIN | (flush < 0 ? POLLOUT : 0);
	fds[1].fd = g_wl.wake_pipe[0];
	fds[1].events = POLLIN;
	fds[2].fd = rvn_atspi_fd();
	fds[2].events = POLLIN;
	fds[3].fd = extra_fd;
	fds[3].events = POLLIN;
	milliseconds = timeout < 0 ? -1 : (timeout > INT_MAX / 1000.0 ? INT_MAX : (int)ceil(timeout * 1000.0));
	result = poll(fds, 4, milliseconds);
	if (result > 0 && (fds[0].revents & POLLIN)) {
		prepared = 0;
		if (wl_display_read_events(g_wl.display) < 0) goto failed;
	} else {
		wl_display_cancel_read(g_wl.display);
		prepared = 0;
	}
	if ((result < 0 && errno != EINTR) || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) goto failed;
	if (fds[0].revents & POLLOUT) {
		if (wl_display_flush(g_wl.display) < 0 && errno != EAGAIN) goto failed;
	}
	if (fds[1].revents & POLLIN) {
		char bytes[128];
		while (read(g_wl.wake_pipe[0], bytes, sizeof(bytes)) > 0) {}
	}
	if (wl_display_dispatch_pending(g_wl.display) < 0) goto failed;
	rvn_wl_clipboard_flush();
	return R_TRUE;
failed:
	if (prepared) wl_display_cancel_read(g_wl.display);
	rvn_wl_connection_error();
	return R_FALSE;
}

static void rvn_wl_apply_configure(rvn_pwindow_t* window) {
	rvn_pevent_t event = { 0 };
	int i, scale = 1, old_width = window->width_px, old_height = window->height_px;
	float old_scale = window->scale;
	if (!window->configure_pending && !window->scale_dirty) return;
	if (window->configure_pending) {
		xdg_surface_ack_configure(window->shell_surface, window->configure_serial);
		window->configure_pending = 0;
		if (window->pending_width > 0) window->width = window->pending_width;
		if (window->pending_height > 0) window->height = window->pending_height;
		window->pending_width = window->pending_height = 0;
		if (window->state != window->pending_state) {
			window->state = window->pending_state;
			event.type = RVN_PEVENT_STATE;
			event.state = window->state;
			rvn_wl_emit(window, &event);
		}
	}
	/* An activation configure may still describe the previous dimensions when
	 * the application requests a size. Apply that request after acknowledging
	 * the queued configure, unless the compositor constrains the geometry. */
	if (window->requested_width && !window->constrained) {
		window->width = window->requested_width;
		window->height = window->requested_height;
		window->requested_width = window->requested_height = 0;
	}
	for (i = 0; i < (int)RVN_COUNT_OF(window->outputs); i++) {
		if (window->outputs[i]) scale = rvn_maxi(scale, window->outputs[i]->scale);
	}
	window->scale = window->viewport && window->fractional_scale ? (float)window->fractional_scale / 120.0f : (float)scale;
	/* fractional-scale-v1 specifies nearest rounding, halfway away from zero. */
	window->width_px = (int)roundf((float)window->width * window->scale);
	window->height_px = (int)roundf((float)window->height * window->scale);
	window->scale_dirty = 0;
	if (window->viewport) {
		wl_surface_set_buffer_scale(window->surface, 1);
		wp_viewport_set_destination(window->viewport, window->width, window->height);
	} else {
		wl_surface_set_buffer_scale(window->surface, scale);
	}
	xdg_surface_set_window_geometry(window->shell_surface, 0, 0, window->width, window->height);
	if (old_width != window->width_px || old_height != window->height_px || old_scale != window->scale) {
		event.type = RVN_PEVENT_RESIZE;
		event.width = window->width_px;
		event.height = window->height_px;
		event.scale = window->scale;
		rvn_wl_emit(window, &event);
		if (window == g_wl.pointer_focus) rvn_wl_set_cursor(window, window->cursor);
	}
}

static void rvn_wl_surface_configure(void* data, struct xdg_surface* surface, uint32_t serial) {
	rvn_pwindow_t* window = data;
	RVN_UNUSED(surface);
	window->configure_serial = serial;
	window->configure_pending = 1;
}
static const struct xdg_surface_listener surface_listener = { .configure = rvn_wl_surface_configure };

static void rvn_wl_toplevel_configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states) {
	rvn_pwindow_t* window = data;
	uint32_t* state;
	RVN_UNUSED(toplevel);
	window->pending_width = width;
	window->pending_height = height;
	window->pending_state = RVN_PWINDOW_NORMAL;
	window->pending_activated = 0;
	window->constrained = 0;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED) window->pending_state = RVN_PWINDOW_MAXIMIZED;
		if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) window->pending_activated = 1;
		if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED || *state == XDG_TOPLEVEL_STATE_FULLSCREEN || *state == XDG_TOPLEVEL_STATE_RESIZING ||
			*state == XDG_TOPLEVEL_STATE_TILED_LEFT || *state == XDG_TOPLEVEL_STATE_TILED_RIGHT ||
			*state == XDG_TOPLEVEL_STATE_TILED_TOP || *state == XDG_TOPLEVEL_STATE_TILED_BOTTOM) window->constrained = 1;
	}
	if (window->state == RVN_PWINDOW_MINIMIZED && !window->pending_activated) window->pending_state = RVN_PWINDOW_MINIMIZED;
}

static void rvn_wl_toplevel_close(void* data, struct xdg_toplevel* toplevel) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(toplevel);
	event.type = RVN_PEVENT_CLOSE;
	rvn_wl_emit(data, &event);
}
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = rvn_wl_toplevel_configure, .close = rvn_wl_toplevel_close
};

static void rvn_wl_surface_enter(void* data, struct wl_surface* surface, struct wl_output* proxy) {
	rvn_pwindow_t* window = data;
	rvn_wl_output_t* output = wl_output_get_user_data(proxy);
	int i;
	RVN_UNUSED(surface);
	for (i = 0; i < (int)RVN_COUNT_OF(window->outputs); i++) {
		if (window->outputs[i] == output) return;
		if (!window->outputs[i]) { window->outputs[i] = output; break; }
	}
	window->scale_dirty = 1;
}
static void rvn_wl_surface_leave(void* data, struct wl_surface* surface, struct wl_output* proxy) {
	rvn_pwindow_t* window = data;
	int i;
	RVN_UNUSED(surface);
	for (i = 0; i < (int)RVN_COUNT_OF(window->outputs); i++) {
		if (window->outputs[i] && window->outputs[i]->proxy == proxy) window->outputs[i] = NULL;
	}
	window->scale_dirty = 1;
}
static const struct wl_surface_listener wl_surface_listener = { .enter = rvn_wl_surface_enter, .leave = rvn_wl_surface_leave };

static void rvn_wl_fractional_scale(void* data, struct wp_fractional_scale_v1* proxy, uint32_t scale) {
	rvn_pwindow_t* window = data;
	RVN_UNUSED(proxy);
	if (scale > 0 && scale <= 120 * 16) { window->fractional_scale = scale; window->scale_dirty = 1; }
}
static const struct wp_fractional_scale_v1_listener fractional_listener = { .preferred_scale = rvn_wl_fractional_scale };

static void rvn_wl_decoration_configure(void* data, struct zxdg_toplevel_decoration_v1* decoration, uint32_t mode) {
	RVN_UNUSED(data); RVN_UNUSED(decoration); RVN_UNUSED(mode);
}
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = { .configure = rvn_wl_decoration_configure };

static void rvn_wl_output_geometry(void* data, struct wl_output* output, int32_t x, int32_t y, int32_t width, int32_t height,
	int32_t subpixel, const char* make, const char* model, int32_t transform) {
	RVN_UNUSED(data); RVN_UNUSED(output); RVN_UNUSED(x); RVN_UNUSED(y); RVN_UNUSED(width); RVN_UNUSED(height);
	RVN_UNUSED(subpixel); RVN_UNUSED(make); RVN_UNUSED(model); RVN_UNUSED(transform);
}
static void rvn_wl_output_mode(void* data, struct wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	RVN_UNUSED(data); RVN_UNUSED(output); RVN_UNUSED(flags); RVN_UNUSED(width); RVN_UNUSED(height); RVN_UNUSED(refresh);
}
static void rvn_wl_output_done(void* data, struct wl_output* output) { RVN_UNUSED(data); RVN_UNUSED(output); }
static void rvn_wl_output_scale(void* data, struct wl_output* proxy, int32_t factor) {
	rvn_wl_output_t* output = data;
	rvn_pwindow_t* window;
	RVN_UNUSED(proxy);
	output->scale = rvn_clampi(factor, 1, 16);
	for (window = g_wl.windows; window; window = window->next) window->scale_dirty = 1;
}
static const struct wl_output_listener output_listener = {
	.geometry = rvn_wl_output_geometry, .mode = rvn_wl_output_mode, .done = rvn_wl_output_done, .scale = rvn_wl_output_scale
};
static void rvn_wl_ping(void* data, struct xdg_wm_base* shell, uint32_t serial) { RVN_UNUSED(data); xdg_wm_base_pong(shell, serial); }
static const struct xdg_wm_base_listener shell_listener = { .ping = rvn_wl_ping };

static void rvn_wl_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	RVN_UNUSED(data);
	if (strcmp(interface, wl_compositor_interface.name) == 0 && version >= 3) {
		g_wl.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, rvn_minu(version, 4));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		g_wl.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		g_wl.shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, rvn_minu(version, 3));
		xdg_wm_base_add_listener(g_wl.shell, &shell_listener, NULL);
	} else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		g_wl.decorations = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		g_wl.viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	} else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		g_wl.fractional = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && !g_wl.seat && version >= 5) {
		rvn_wl_bind_seat(registry, name, version);
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		g_wl.data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, rvn_minu(version, 3));
	} else if (strcmp(interface, wl_output_interface.name) == 0 && version >= 2) {
		rvn_wl_output_t* output = rvn_calloc(sizeof(*output));
		output->name = name;
		output->scale = 1;
		output->proxy = wl_registry_bind(registry, name, &wl_output_interface, 2);
		output->next = g_wl.outputs;
		g_wl.outputs = output;
		wl_output_add_listener(output->proxy, &output_listener, output);
	}
}

static void rvn_wl_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
	rvn_wl_output_t** link = &g_wl.outputs;
	RVN_UNUSED(data); RVN_UNUSED(registry);
	while (*link) {
		rvn_wl_output_t* output = *link;
		if (output->name == name) {
			rvn_pwindow_t* window;
			for (window = g_wl.windows; window; window = window->next) rvn_wl_surface_leave(window, window->surface, output->proxy);
			*link = output->next;
			wl_output_destroy(output->proxy);
			rvn_free(output);
			break;
		}
		link = &output->next;
	}
	if (g_wl.seat && name == g_wl.seat_name) {
		rvn_wl_clipboard_shutdown();
		rvn_wl_input_shutdown();
		rvn_wl_input_init();
	}
}
static const struct wl_registry_listener registry_listener = { .global = rvn_wl_global, .global_remove = rvn_wl_global_remove };

static void rvn_wl_window_destroy(rvn_pwindow_t* window) {
	rvn_pwindow_t** link = &g_wl.windows;
	rvn_wl_event_t** pending = &g_wl.events_head;
	while (*link && *link != window) link = &(*link)->next;
	if (*link) *link = window->next;
	g_wl.events_tail = NULL;
	while (*pending) {
		rvn_wl_event_t* event = *pending;
		if (event->window == window) { *pending = event->next; rvn_free(event); }
		else { g_wl.events_tail = event; pending = &event->next; }
	}
	rvn_wl_input_forget(window);
	if (window->fractional) wp_fractional_scale_v1_destroy(window->fractional);
	if (window->viewport) wp_viewport_destroy(window->viewport);
	if (window->decoration) zxdg_toplevel_decoration_v1_destroy(window->decoration);
	if (window->toplevel) xdg_toplevel_destroy(window->toplevel);
	if (window->shell_surface) xdg_surface_destroy(window->shell_surface);
	if (window->surface) wl_surface_destroy(window->surface);
	rvn_free(window->title);
	rvn_free(window);
}

static void rvn_wl_shutdown(void) {
	while (g_wl.windows) rvn_wl_window_destroy(g_wl.windows);
	rvn_atspi_shutdown();
	rvn_wl_clipboard_shutdown();
	rvn_wl_input_shutdown();
	while (g_wl.outputs) {
		rvn_wl_output_t* output = g_wl.outputs;
		g_wl.outputs = output->next;
		wl_output_destroy(output->proxy);
		rvn_free(output);
	}
	if (g_wl.data_manager) wl_data_device_manager_destroy(g_wl.data_manager);
	if (g_wl.fractional) wp_fractional_scale_manager_v1_destroy(g_wl.fractional);
	if (g_wl.viewporter) wp_viewporter_destroy(g_wl.viewporter);
	if (g_wl.decorations) zxdg_decoration_manager_v1_destroy(g_wl.decorations);
	if (g_wl.shell) xdg_wm_base_destroy(g_wl.shell);
	if (g_wl.shm) wl_shm_destroy(g_wl.shm);
	if (g_wl.compositor) wl_compositor_destroy(g_wl.compositor);
	if (g_wl.registry) wl_registry_destroy(g_wl.registry);
	if (g_wl.display) { wl_display_flush(g_wl.display); wl_display_disconnect(g_wl.display); }
	if (g_wl.wake_pipe[0] >= 0) close(g_wl.wake_pipe[0]);
	if (g_wl.wake_pipe[1] >= 0) close(g_wl.wake_pipe[1]);
	memset(&g_wl, 0, sizeof(g_wl));
	g_wl.wake_pipe[0] = g_wl.wake_pipe[1] = -1;
}

static int rvn_wl_init(rvn_pevent_handler_t handler, rvn_pregion_handler_t regions) {
	RVN_UNUSED(regions);
	memset(&g_wl, 0, sizeof(g_wl));
	g_wl.wake_pipe[0] = g_wl.wake_pipe[1] = -1;
	g_wl.handler = handler;
	g_wl.display = wl_display_connect(NULL);
	if (!g_wl.display) {
		rvn_error(R_ERROR_PLATFORM, R_SEVERITY_ERROR, "Wayland: cannot connect (check WAYLAND_DISPLAY and XDG_RUNTIME_DIR)");
		return R_FALSE;
	}
	if (pipe(g_wl.wake_pipe) != 0 || !rvn_wl_fd_flags(g_wl.wake_pipe[0]) || !rvn_wl_fd_flags(g_wl.wake_pipe[1])) goto failed;
	rvn_wl_input_init();
	g_wl.registry = wl_display_get_registry(g_wl.display);
	wl_registry_add_listener(g_wl.registry, &registry_listener, NULL);
	if (wl_display_roundtrip(g_wl.display) < 0 || wl_display_roundtrip(g_wl.display) < 0) goto failed;
	if (!g_wl.compositor || !g_wl.shell || !g_wl.shm || !g_wl.xkb_context) {
		rvn_error(R_ERROR_PLATFORM, R_SEVERITY_ERROR, "Wayland: wl_compositor v3, wl_shm, xdg-shell and xkbcommon are required");
		goto failed;
	}
	rvn_wl_clipboard_init();
	rvn_atspi_init();
	return R_TRUE;
failed:
	rvn_wl_shutdown();
	return R_FALSE;
}

static int rvn_wl_wait_configure(rvn_pwindow_t* window) {
	double deadline = rvn_clock_seconds() + 5.0;
	while (!window->configure_pending && !g_wl.failed && rvn_clock_seconds() < deadline) {
		if (!rvn_wl_dispatch(0.05, -1)) break;
	}
	if (!window->configure_pending) {
		rvn_error(R_ERROR_PLATFORM, R_SEVERITY_ERROR, "Wayland: timed out waiting for initial xdg-shell configure");
		return R_FALSE;
	}
	rvn_wl_apply_configure(window);
	return R_TRUE;
}

static rvn_pwindow_t* rvn_wl_window_create(const rvn_pwindow_desc_t* desc) {
	rvn_pwindow_t* window = rvn_calloc(sizeof(*window));
	window->width = window->width_px = desc->width;
	window->height = window->height_px = desc->height;
	window->scale = 1;
	window->scale_dirty = 1;
	window->flags = desc->flags;
	window->user = desc->user;
	window->title = rvn_strdup(desc->title ? desc->title : "");
	window->cursor = R_CURSOR_ARROW;
	window->surface = wl_compositor_create_surface(g_wl.compositor);
	window->next = g_wl.windows;
	g_wl.windows = window;
	if (!window->surface) goto failed;
	wl_surface_add_listener(window->surface, &wl_surface_listener, window);
	window->shell_surface = xdg_wm_base_get_xdg_surface(g_wl.shell, window->surface);
	if (!window->shell_surface) goto failed;
	xdg_surface_add_listener(window->shell_surface, &surface_listener, window);
	window->toplevel = xdg_surface_get_toplevel(window->shell_surface);
	if (!window->toplevel) goto failed;
	xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
	xdg_toplevel_set_title(window->toplevel, desc->title ? desc->title : "");
	xdg_toplevel_set_app_id(window->toplevel, "org.raven.Raven");
	if (desc->flags & R_WINDOW_NO_RESIZE) {
		xdg_toplevel_set_min_size(window->toplevel, desc->width, desc->height);
		xdg_toplevel_set_max_size(window->toplevel, desc->width, desc->height);
	}
	if (desc->flags & R_WINDOW_MAXIMIZED) xdg_toplevel_set_maximized(window->toplevel);
	if (g_wl.decorations) {
		window->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(g_wl.decorations, window->toplevel);
		zxdg_toplevel_decoration_v1_add_listener(window->decoration, &decoration_listener, window);
		zxdg_toplevel_decoration_v1_set_mode(window->decoration,
			(desc->flags & (R_WINDOW_BORDERLESS | R_WINDOW_CUSTOM_HEADER)) ?
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (g_wl.viewporter && g_wl.fractional) {
		window->viewport = wp_viewporter_get_viewport(g_wl.viewporter, window->surface);
		window->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(g_wl.fractional, window->surface);
		wp_fractional_scale_v1_add_listener(window->fractional, &fractional_listener, window);
	}
	/* No buffer may be attached until the first configure has been acked. */
	wl_surface_commit(window->surface);
	if (!rvn_wl_wait_configure(window)) goto failed;
	return window;
failed:
	rvn_wl_window_destroy(window);
	return NULL;
}

static void* rvn_wl_native_display(void) { return g_wl.display; }
static void rvn_wl_set_title(rvn_pwindow_t* window, const char* title) {
	rvn_str_set(&window->title, title);
	xdg_toplevel_set_title(window->toplevel, title);
}
static void rvn_wl_get_size(rvn_pwindow_t* window, int* width, int* height) { *width = window->width_px; *height = window->height_px; }
static float rvn_wl_get_scale(rvn_pwindow_t* window) { return window->scale; }
static void rvn_wl_set_size(rvn_pwindow_t* window, int width, int height) {
	/* The core passes pixel dimensions; xdg-shell geometry is in surface units. */
	window->requested_width = rvn_maxi((int)roundf((float)width / window->scale), 1);
	window->requested_height = rvn_maxi((int)roundf((float)height / window->scale), 1);
	if (window->flags & R_WINDOW_NO_RESIZE) {
		xdg_toplevel_set_min_size(window->toplevel, window->requested_width, window->requested_height);
		xdg_toplevel_set_max_size(window->toplevel, window->requested_width, window->requested_height);
	}
	window->scale_dirty = 1;
}
static void rvn_wl_set_min_size(rvn_pwindow_t* window, int width, int height) {
	window->min_width = rvn_maxi((int)ceilf((float)width / window->scale), 0);
	window->min_height = rvn_maxi((int)ceilf((float)height / window->scale), 0);
	xdg_toplevel_set_min_size(window->toplevel, window->min_width, window->min_height);
}
/* Wayland deliberately does not expose global window coordinates. */
static void rvn_wl_set_position(rvn_pwindow_t* window, int x, int y) { RVN_UNUSED(window); RVN_UNUSED(x); RVN_UNUSED(y); }
static void rvn_wl_get_position(rvn_pwindow_t* window, int* x, int* y) { RVN_UNUSED(window); *x = *y = 0; }
static void rvn_wl_center(rvn_pwindow_t* window) { RVN_UNUSED(window); }
static int rvn_wl_set_topmost(rvn_pwindow_t* window, int enabled) { RVN_UNUSED(window); RVN_UNUSED(enabled); return R_FALSE; }
static int rvn_wl_set_opacity(rvn_pwindow_t* window, float opacity) { RVN_UNUSED(window); return opacity == 1.0f; }
static int rvn_wl_set_backdrop(rvn_pwindow_t* window, int backdrop, int dark) {
	RVN_UNUSED(dark);
	return (window->flags & R_WINDOW_TRANSPARENT) && backdrop != R_BACKDROP_NONE ? R_BACKDROP_TRANSPARENT : R_BACKDROP_NONE;
}
static void rvn_wl_show(rvn_pwindow_t* window, int visible) {
	if (visible && window->hidden) {
		window->hidden = 0;
		/* Unmapping resets xdg_toplevel state, including its title and hints. */
		xdg_toplevel_set_title(window->toplevel, window->title);
		xdg_toplevel_set_app_id(window->toplevel, "org.raven.Raven");
		xdg_toplevel_set_min_size(window->toplevel, window->min_width, window->min_height);
		if (window->flags & R_WINDOW_NO_RESIZE) {
			xdg_toplevel_set_min_size(window->toplevel, window->width, window->height);
			xdg_toplevel_set_max_size(window->toplevel, window->width, window->height);
		}
		if (window->state == RVN_PWINDOW_MAXIMIZED) xdg_toplevel_set_maximized(window->toplevel);
		wl_surface_commit(window->surface);
		rvn_wl_wait_configure(window);
	} else if (!visible && !window->hidden) {
		window->hidden = 1;
		window->configure_pending = 0;
		wl_surface_attach(window->surface, NULL, 0, 0);
		wl_surface_commit(window->surface);
	}
}
static rvn_pwindow_state_t rvn_wl_get_state(rvn_pwindow_t* window) { return window->state; }
static void rvn_wl_set_state(rvn_pwindow_t* window, rvn_pwindow_state_t state) {
	if (state == RVN_PWINDOW_MAXIMIZED) xdg_toplevel_set_maximized(window->toplevel);
	else if (state == RVN_PWINDOW_MINIMIZED) {
		rvn_pevent_t event = { 0 };
		xdg_toplevel_set_minimized(window->toplevel);
		window->state = state;
		event.type = RVN_PEVENT_STATE;
		event.state = state;
		rvn_wl_emit(window, &event);
	} else xdg_toplevel_unset_maximized(window->toplevel);
}
static void rvn_wl_begin_move(rvn_pwindow_t* window) {
	if (g_wl.seat && g_wl.input_serial) xdg_toplevel_move(window->toplevel, g_wl.seat, g_wl.input_serial);
}
static void rvn_wl_begin_resize(rvn_pwindow_t* window, rvn_pregion_t region) {
	static const uint32_t edges[] = { XDG_TOPLEVEL_RESIZE_EDGE_LEFT, XDG_TOPLEVEL_RESIZE_EDGE_RIGHT,
		XDG_TOPLEVEL_RESIZE_EDGE_TOP, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM, XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT,
		XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT };
	int index = (int)region - RVN_PREGION_LEFT;
	if (!(window->flags & R_WINDOW_NO_RESIZE) && g_wl.seat && g_wl.input_serial && index >= 0 && index < (int)RVN_COUNT_OF(edges)) {
		xdg_toplevel_resize(window->toplevel, g_wl.seat, g_wl.input_serial, edges[index]);
	}
}
static void rvn_wl_get_native(rvn_pwindow_t* window, RNativeWindow* native) {
	memset(native, 0, sizeof(*native));
	native->system = R_WINDOW_SYSTEM_WAYLAND;
	native->display = g_wl.display;
	native->window = (uintptr_t)window->surface;
	native->transparent = (window->flags & R_WINDOW_TRANSPARENT) != 0;
}
static rvn_pwindow_t* rvn_wl_child_create(rvn_pwindow_t* parent, int x, int y, int width, int height, void* user) {
	RVN_UNUSED(parent); RVN_UNUSED(x); RVN_UNUSED(y); RVN_UNUSED(width); RVN_UNUSED(height); RVN_UNUSED(user);
	rvn_error(R_ERROR_PLATFORM, R_SEVERITY_WARNING, "Wayland: native child viewports are unavailable; use texture or draw-callback viewports");
	return NULL;
}
static void rvn_wl_child_rect(rvn_pwindow_t* child, int x, int y, int width, int height, int visible) {
	RVN_UNUSED(child); RVN_UNUSED(x); RVN_UNUSED(y); RVN_UNUSED(width); RVN_UNUSED(height); RVN_UNUSED(visible);
}
static void rvn_wl_pump(double timeout) {
	rvn_pwindow_t* window;
	if (g_wl.events_head) timeout = 0;
	for (window = g_wl.windows; window; window = window->next) {
		if (window->configure_pending || window->scale_dirty) { timeout = 0; break; }
	}
	if (g_wl.repeating) {
		double remaining = fmax(0.0, g_wl.repeat_at - rvn_clock_seconds());
		if (timeout < 0 || remaining < timeout) timeout = remaining;
	}
	if (!rvn_wl_dispatch(timeout, -1)) return;
	for (window = g_wl.windows; window; window = window->next) rvn_wl_apply_configure(window);
	rvn_wl_input_repeat();
	while (g_wl.events_head) {
		rvn_wl_event_t* event = g_wl.events_head;
		g_wl.events_head = event->next;
		if (!g_wl.events_head) g_wl.events_tail = NULL;
		g_wl.input_serial = event->input_serial;
		g_wl.handler(&event->event);
		rvn_free(event);
	}
	rvn_atspi_dispatch();
	wl_display_flush(g_wl.display);
}
static void rvn_wl_wait_service(void) { rvn_wl_dispatch(0, -1); }
static void rvn_wl_wake(void) {
	char byte = 1;
	if (g_wl.wake_pipe[1] >= 0 && write(g_wl.wake_pipe[1], &byte, 1) < 0) {}
}

const rvn_platform_t* rvn_platform_wayland(void) {
	static const rvn_platform_t platform = {
		.name = "Wayland", .system = R_WINDOW_SYSTEM_WAYLAND,
		.init = rvn_wl_init, .shutdown = rvn_wl_shutdown, .native_display = rvn_wl_native_display,
		.window_create = rvn_wl_window_create, .window_destroy = rvn_wl_window_destroy,
		.window_set_title = rvn_wl_set_title, .window_set_size = rvn_wl_set_size, .window_get_size = rvn_wl_get_size,
		.window_set_min_size = rvn_wl_set_min_size, .window_set_position = rvn_wl_set_position, .window_get_position = rvn_wl_get_position,
		.window_get_scale = rvn_wl_get_scale, .window_show = rvn_wl_show, .window_set_state = rvn_wl_set_state, .window_get_state = rvn_wl_get_state,
		.window_begin_move = rvn_wl_begin_move, .window_begin_resize = rvn_wl_begin_resize,
		.window_set_backdrop = rvn_wl_set_backdrop, .window_set_opacity = rvn_wl_set_opacity, .window_set_topmost = rvn_wl_set_topmost,
		.window_center = rvn_wl_center, .window_set_cursor = rvn_wl_set_cursor, .window_get_native = rvn_wl_get_native,
		.child_create = rvn_wl_child_create, .child_set_rect = rvn_wl_child_rect,
		.pump = rvn_wl_pump, .wake = rvn_wl_wake, .wait_service = rvn_wl_wait_service,
		.clipboard_set = rvn_wl_clipboard_set, .clipboard_get = rvn_wl_clipboard_get,
		.a11y_event = rvn_atspi_event, .a11y_announce = rvn_atspi_announce, .a11y_active = rvn_atspi_active, .a11y_settings = rvn_atspi_settings
	};
	return &platform;
}
