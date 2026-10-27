#ifndef RVN_WAYLAND_INTERNAL_H
#define RVN_WAYLAND_INTERNAL_H

#include "../platform.h"
#include "../../core/thread.h"
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

typedef struct rvn_wl_output_t {
	struct rvn_wl_output_t* next;
	struct wl_output* proxy;
	uint32_t name;
	int scale;
} rvn_wl_output_t;

struct rvn_pwindow_t {
	struct rvn_pwindow_t* next;
	struct wl_surface* surface;
	struct xdg_surface* shell_surface;
	struct xdg_toplevel* toplevel;
	struct zxdg_toplevel_decoration_v1* decoration;
	struct wp_viewport* viewport;
	struct wp_fractional_scale_v1* fractional;
	void* user;
	char* title;
	char* app_id;
	uint32_t flags, configure_serial, fractional_scale;
	int width, height, pending_width, pending_height;
	int requested_width, requested_height, constrained;
	int width_px, height_px, configure_pending, scale_dirty, hidden;
	int min_width, min_height, pending_activated;
	float scale;
	rvn_pwindow_state_t state, pending_state;
	RCursor cursor;
	rvn_wl_output_t* outputs[16];
};

typedef struct rvn_wl_event_t {
	struct rvn_wl_event_t* next;
	rvn_pwindow_t* window;
	uint32_t input_serial;
	rvn_pevent_t event;
} rvn_wl_event_t;

typedef struct rvn_wayland_t {
	struct wl_display* display;
	struct wl_registry* registry;
	struct wl_compositor* compositor;
	struct wl_shm* shm;
	struct xdg_wm_base* shell;
	struct zxdg_decoration_manager_v1* decorations;
	struct wp_viewporter* viewporter;
	struct wp_fractional_scale_manager_v1* fractional;
	struct wl_seat* seat;
	struct wl_pointer* pointer;
	struct wl_keyboard* keyboard;
	struct wl_data_device_manager* data_manager;
	struct wl_surface* cursor_surface;
	struct wl_cursor_theme* cursor_theme;
	int cursor_scale;
	uint32_t seat_name, input_serial, pointer_serial;
	rvn_pwindow_t* windows;
	rvn_pwindow_t* pointer_focus;
	rvn_pwindow_t* keyboard_focus;
	rvn_wl_output_t* outputs;
	rvn_wl_event_t* events_head;
	rvn_wl_event_t* events_tail;
	rvn_pevent_handler_t handler;
	struct xkb_context* xkb_context;
	struct xkb_keymap* keymap;
	struct xkb_state* xkb_state;
	struct xkb_compose_table* compose_table;
	struct xkb_compose_state* compose_state;
	uint32_t modifiers, repeat_key;
	int repeat_rate, repeat_delay, repeating;
	double repeat_at;
	double pointer_x, pointer_y;
	uint32_t wheel_discrete; /* axes that received axis_discrete in this pointer frame */
	int wake_pipe[2], failed;
} rvn_wayland_t;

extern rvn_wayland_t g_wl;

void rvn_wl_emit(rvn_pwindow_t* window, const rvn_pevent_t* event);
rvn_pwindow_t* rvn_wl_find(struct wl_surface* surface);
/* Reads/dispatches protocol events, never calls Raven/application callbacks. */
int rvn_wl_dispatch(double timeout, int extra_fd);
int rvn_wl_fd_flags(int fd);
void rvn_wl_input_init(void);
void rvn_wl_input_shutdown(void);
void rvn_wl_bind_seat(struct wl_registry* registry, uint32_t name, uint32_t version);
void rvn_wl_input_repeat(void);
void rvn_wl_set_cursor(rvn_pwindow_t* window, RCursor cursor);
void rvn_wl_input_forget(rvn_pwindow_t* window);
RKey rvn_wl_translate_keysym(xkb_keysym_t symbol);
void rvn_wl_clipboard_init(void);
void rvn_wl_clipboard_shutdown(void);
void rvn_wl_clipboard_set(const char* text);
char* rvn_wl_clipboard_get(void);
void rvn_wl_clipboard_serial(uint32_t serial);
void rvn_wl_clipboard_flush(void);
int rvn_wl_clipboard_pending(void);

#endif
