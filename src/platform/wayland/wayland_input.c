/** Pointer, cursors, xkb keymaps, compose sequences and timed key repeat. */
#include "wayland_internal.h"
#include <sys/mman.h>
#include <unistd.h>

static uint32_t rvn_wl_modifiers(void) {
	uint32_t mods = 0;
	if (!g_wl.xkb_state) return 0;
	if (xkb_state_mod_name_is_active(g_wl.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) mods |= R_MOD_SHIFT;
	if (xkb_state_mod_name_is_active(g_wl.xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) mods |= R_MOD_CTRL;
	if (xkb_state_mod_name_is_active(g_wl.xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) mods |= R_MOD_ALT;
	if (xkb_state_mod_name_is_active(g_wl.xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) mods |= R_MOD_SUPER;
	return mods;
}

static void rvn_wl_text(const char* text) {
	const char* end = text + strlen(text);
	rvn_pevent_t event = { 0 };
	event.type = RVN_PEVENT_TEXT;
	while (text < end) {
		event.codepoint = rvn_utf8_decode(&text, end);
		if (event.codepoint >= 32 && event.codepoint != 127) rvn_wl_emit(g_wl.keyboard_focus, &event);
	}
}

static void rvn_wl_key(uint32_t key, int pressed, int repeat) {
	xkb_keycode_t code = key + 8;
	xkb_keysym_t symbol;
	const xkb_keysym_t* syms = NULL;
	xkb_layout_index_t group;
	rvn_pevent_t event = { 0 };
	char text[256];
	int length;
	if (!g_wl.xkb_state || !g_wl.keyboard_focus) return;
	group = xkb_state_key_get_layout(g_wl.xkb_state, code);
	xkb_keymap_key_get_syms_by_level(g_wl.keymap, code, group, 0, &syms);
	symbol = xkb_state_key_get_one_sym(g_wl.xkb_state, code);
	event.type = RVN_PEVENT_KEY;
	event.key = rvn_wl_translate_keysym(syms ? syms[0] : symbol);
	event.pressed = pressed;
	event.repeat = repeat;
	event.modifiers = g_wl.modifiers;
	rvn_wl_emit(g_wl.keyboard_focus, &event);
	if (!pressed || (g_wl.modifiers & (R_MOD_CTRL | R_MOD_SUPER))) return;
	if (g_wl.compose_state && xkb_compose_state_feed(g_wl.compose_state, symbol) == XKB_COMPOSE_FEED_ACCEPTED) {
		switch (xkb_compose_state_get_status(g_wl.compose_state)) {
			case XKB_COMPOSE_COMPOSING: return;
			case XKB_COMPOSE_CANCELLED: xkb_compose_state_reset(g_wl.compose_state); return;
			case XKB_COMPOSE_COMPOSED:
				length = xkb_compose_state_get_utf8(g_wl.compose_state, text, sizeof(text));
				xkb_compose_state_reset(g_wl.compose_state);
				if (length > 0 && length < (int)sizeof(text)) rvn_wl_text(text);
				return;
			case XKB_COMPOSE_NOTHING: break;
		}
	}
	length = xkb_state_key_get_utf8(g_wl.xkb_state, code, text, sizeof(text));
	if (length > 0 && length < (int)sizeof(text)) rvn_wl_text(text);
}

static void rvn_wl_keyboard_keymap(void* data, struct wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size) {
	char* map;
	struct xkb_keymap* keymap;
	struct xkb_state* state;
	RVN_UNUSED(data); RVN_UNUSED(keyboard);
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0 || size > 16 * 1024 * 1024) { close(fd); return; }
	map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return;
	/* The wire format includes a terminating NUL; do not read past the map. */
	keymap = map[size - 1] == '\0' ? xkb_keymap_new_from_string(g_wl.xkb_context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
	munmap(map, size);
	if (!keymap) return;
	state = xkb_state_new(keymap);
	if (!state) { xkb_keymap_unref(keymap); return; }
	if (g_wl.xkb_state) xkb_state_unref(g_wl.xkb_state);
	if (g_wl.keymap) xkb_keymap_unref(g_wl.keymap);
	g_wl.keymap = keymap;
	g_wl.xkb_state = state;
	g_wl.repeating = 0;
}
static void rvn_wl_keyboard_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(keyboard); RVN_UNUSED(serial); RVN_UNUSED(keys);
	g_wl.keyboard_focus = rvn_wl_find(surface);
	event.type = RVN_PEVENT_FOCUS;
	event.focused = 1;
	rvn_wl_emit(g_wl.keyboard_focus, &event);
}
static void rvn_wl_keyboard_leave(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(keyboard); RVN_UNUSED(serial); RVN_UNUSED(surface);
	event.type = RVN_PEVENT_FOCUS;
	rvn_wl_emit(g_wl.keyboard_focus, &event);
	g_wl.keyboard_focus = NULL;
	g_wl.repeating = 0;
	if (g_wl.compose_state) xkb_compose_state_reset(g_wl.compose_state);
}
static void rvn_wl_keyboard_key(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	int pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	RVN_UNUSED(data); RVN_UNUSED(keyboard); RVN_UNUSED(time);
	g_wl.input_serial = serial;
	rvn_wl_clipboard_serial(serial);
	rvn_wl_key(key, pressed, 0);
	if (pressed && g_wl.keymap && g_wl.repeat_rate > 0 && xkb_keymap_key_repeats(g_wl.keymap, key + 8)) {
		g_wl.repeat_key = key;
		g_wl.repeat_at = rvn_clock_seconds() + (double)g_wl.repeat_delay / 1000.0;
		g_wl.repeating = 1;
	} else if (!pressed && key == g_wl.repeat_key) g_wl.repeating = 0;
}
static void rvn_wl_keyboard_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial,
	uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
	RVN_UNUSED(data); RVN_UNUSED(keyboard); RVN_UNUSED(serial);
	if (g_wl.xkb_state) xkb_state_update_mask(g_wl.xkb_state, depressed, latched, locked, 0, 0, group);
	g_wl.modifiers = rvn_wl_modifiers();
}
static void rvn_wl_keyboard_repeat(void* data, struct wl_keyboard* keyboard, int32_t rate, int32_t delay) {
	RVN_UNUSED(data); RVN_UNUSED(keyboard);
	g_wl.repeat_rate = rvn_clampi(rate, 0, 1000);
	g_wl.repeat_delay = rvn_maxi(delay, 0);
	if (rate == 0) g_wl.repeating = 0;
}
static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = rvn_wl_keyboard_keymap, .enter = rvn_wl_keyboard_enter, .leave = rvn_wl_keyboard_leave,
	.key = rvn_wl_keyboard_key, .modifiers = rvn_wl_keyboard_modifiers, .repeat_info = rvn_wl_keyboard_repeat
};

void rvn_wl_input_repeat(void) {
	int emitted = 0;
	double now = rvn_clock_seconds();
	while (g_wl.repeating && g_wl.repeat_rate > 0 && now >= g_wl.repeat_at && emitted++ < 32) {
		rvn_wl_key(g_wl.repeat_key, 1, 1);
		g_wl.repeat_at += 1.0 / (double)g_wl.repeat_rate;
	}
	if (g_wl.repeating && now >= g_wl.repeat_at) g_wl.repeat_at = now + 1.0 / (double)rvn_maxi(g_wl.repeat_rate, 1);
}

void rvn_wl_set_cursor(rvn_pwindow_t* window, RCursor cursor) {
	static const char* const names[] = { "left_ptr", "xterm", "hand2", "crosshair", "sb_h_double_arrow",
		"sb_v_double_arrow", "bd_double_arrow", "fd_double_arrow", "fleur", "not-allowed" };
	struct wl_cursor* shape;
	struct wl_cursor_image* image;
	struct wl_buffer* buffer;
	int index = (int)cursor - R_CURSOR_ARROW, scale = (int)ceilf(window->scale), size = 24;
	const char* size_env = getenv("XCURSOR_SIZE");
	window->cursor = cursor;
	if (!g_wl.pointer || g_wl.pointer_focus != window || !g_wl.pointer_serial) return;
	if (cursor == R_CURSOR_NONE) { wl_pointer_set_cursor(g_wl.pointer, g_wl.pointer_serial, NULL, 0, 0); return; }
	if (size_env) { int configured = atoi(size_env); if (configured > 0 && configured <= 256) size = configured; }
	if (!g_wl.cursor_theme || g_wl.cursor_scale != scale) {
		struct wl_cursor_theme* theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"), size * scale, g_wl.shm);
		if (!theme) return;
		if (g_wl.cursor_theme) wl_cursor_theme_destroy(g_wl.cursor_theme);
		g_wl.cursor_theme = theme;
		g_wl.cursor_scale = scale;
	}
	if (!g_wl.cursor_surface) g_wl.cursor_surface = wl_compositor_create_surface(g_wl.compositor);
	if (!g_wl.cursor_surface) return;
	if (index < 0 || index >= (int)RVN_COUNT_OF(names)) index = 0;
	shape = wl_cursor_theme_get_cursor(g_wl.cursor_theme, names[index]);
	if (!shape) shape = wl_cursor_theme_get_cursor(g_wl.cursor_theme, "left_ptr");
	if (!shape || !shape->image_count) return;
	image = shape->images[0];
	buffer = wl_cursor_image_get_buffer(image);
	if (!buffer) return;
	wl_pointer_set_cursor(g_wl.pointer, g_wl.pointer_serial, g_wl.cursor_surface, (int)image->hotspot_x / scale, (int)image->hotspot_y / scale);
	wl_surface_set_buffer_scale(g_wl.cursor_surface, scale);
	wl_surface_attach(g_wl.cursor_surface, buffer, 0, 0);
	wl_surface_damage(g_wl.cursor_surface, 0, 0, (int)image->width, (int)image->height);
	wl_surface_commit(g_wl.cursor_surface);
}

static void rvn_wl_pointer_event(rvn_pevent_t* event) {
	if (!g_wl.pointer_focus) return;
	event->x = (int)round(g_wl.pointer_x * g_wl.pointer_focus->scale);
	event->y = (int)round(g_wl.pointer_y * g_wl.pointer_focus->scale);
	event->modifiers = g_wl.modifiers;
	rvn_wl_emit(g_wl.pointer_focus, event);
}
static void rvn_wl_pointer_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer);
	g_wl.pointer_focus = rvn_wl_find(surface);
	g_wl.pointer_serial = serial;
	g_wl.pointer_x = wl_fixed_to_double(x);
	g_wl.pointer_y = wl_fixed_to_double(y);
	event.type = RVN_PEVENT_MOUSE_MOVE;
	rvn_wl_pointer_event(&event);
	if (g_wl.pointer_focus) rvn_wl_set_cursor(g_wl.pointer_focus, g_wl.pointer_focus->cursor);
}
static void rvn_wl_pointer_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(serial); RVN_UNUSED(surface);
	event.type = RVN_PEVENT_MOUSE_LEAVE;
	rvn_wl_pointer_event(&event);
	g_wl.pointer_focus = NULL;
	g_wl.pointer_serial = 0;
}
static void rvn_wl_pointer_motion(void* data, struct wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(time);
	g_wl.pointer_x = wl_fixed_to_double(x);
	g_wl.pointer_y = wl_fixed_to_double(y);
	event.type = RVN_PEVENT_MOUSE_MOVE;
	rvn_wl_pointer_event(&event);
}
static void rvn_wl_pointer_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(time);
	/* Linux input-event button codes are part of the Wayland wire protocol. */
	switch (button) {
		case 0x110: event.button = R_MOUSE_LEFT; break;
		case 0x111: event.button = R_MOUSE_RIGHT; break;
		case 0x112: event.button = R_MOUSE_MIDDLE; break;
		case 0x113: event.button = R_MOUSE_X1; break;
		case 0x114: event.button = R_MOUSE_X2; break;
		default: return;
	}
	g_wl.input_serial = serial;
	rvn_wl_clipboard_serial(serial);
	event.type = RVN_PEVENT_MOUSE_BUTTON;
	event.pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
	rvn_wl_pointer_event(&event);
}
static void rvn_wl_pointer_axis(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(time);
	/* a wheel already gave its notches (axis_discrete comes first in the frame) */
	if (axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL && (g_wl.wheel_discrete & (1u << axis))) return;
	event.type = RVN_PEVENT_MOUSE_WHEEL;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) event.wheel_y = (float)-wl_fixed_to_double(value) / 10.0f;
	else event.wheel_x = (float)-wl_fixed_to_double(value) / 10.0f;
	rvn_wl_pointer_event(&event);
}
static void rvn_wl_pointer_frame(void* data, struct wl_pointer* pointer) { RVN_UNUSED(data); RVN_UNUSED(pointer); g_wl.wheel_discrete = 0; }
static void rvn_wl_pointer_source(void* data, struct wl_pointer* pointer, uint32_t source) { RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(source); }
static void rvn_wl_pointer_stop(void* data, struct wl_pointer* pointer, uint32_t time, uint32_t axis) { RVN_UNUSED(data); RVN_UNUSED(pointer); RVN_UNUSED(time); RVN_UNUSED(axis); }
/* Mouse wheels: one notch is one step, as on the other platforms. The axis value
 * that follows is in surface units and its size per notch depends on the compositor. */
static void rvn_wl_pointer_discrete(void* data, struct wl_pointer* pointer, uint32_t axis, int32_t discrete) {
	rvn_pevent_t event = { 0 };
	RVN_UNUSED(data); RVN_UNUSED(pointer);
	if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
	g_wl.wheel_discrete |= 1u << axis;
	event.type = RVN_PEVENT_MOUSE_WHEEL;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) event.wheel_y = (float)-discrete;
	else event.wheel_x = (float)-discrete;
	rvn_wl_pointer_event(&event);
}
static const struct wl_pointer_listener pointer_listener = {
	.enter = rvn_wl_pointer_enter, .leave = rvn_wl_pointer_leave, .motion = rvn_wl_pointer_motion,
	.button = rvn_wl_pointer_button, .axis = rvn_wl_pointer_axis, .frame = rvn_wl_pointer_frame,
	.axis_source = rvn_wl_pointer_source, .axis_stop = rvn_wl_pointer_stop, .axis_discrete = rvn_wl_pointer_discrete
};

static void rvn_wl_seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities) {
	RVN_UNUSED(data);
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !g_wl.pointer) {
		g_wl.pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(g_wl.pointer, &pointer_listener, NULL);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && g_wl.pointer) {
		rvn_wl_pointer_leave(NULL, g_wl.pointer, 0, NULL);
		wl_pointer_release(g_wl.pointer);
		g_wl.pointer = NULL;
	}
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !g_wl.keyboard) {
		g_wl.keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(g_wl.keyboard, &keyboard_listener, NULL);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && g_wl.keyboard) {
		rvn_wl_keyboard_leave(NULL, g_wl.keyboard, 0, NULL);
		wl_keyboard_release(g_wl.keyboard);
		g_wl.keyboard = NULL;
	}
}
static void rvn_wl_seat_name(void* data, struct wl_seat* seat, const char* name) { RVN_UNUSED(data); RVN_UNUSED(seat); RVN_UNUSED(name); }
static const struct wl_seat_listener seat_listener = { .capabilities = rvn_wl_seat_capabilities, .name = rvn_wl_seat_name };

void rvn_wl_bind_seat(struct wl_registry* registry, uint32_t name, uint32_t version) {
	RVN_UNUSED(version);
	g_wl.seat_name = name;
	g_wl.seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
	wl_seat_add_listener(g_wl.seat, &seat_listener, NULL);
	rvn_wl_clipboard_init();
}
void rvn_wl_input_init(void) {
	/* The compose locale comes from the environment, as xkbcommon advises:
	 * calling setlocale here would change the application's own locale. */
	const char* locale = getenv("LC_ALL");
	if (!locale || !*locale) locale = getenv("LC_CTYPE");
	if (!locale || !*locale) locale = getenv("LANG");
	if (!locale || !*locale) locale = "C";
	g_wl.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (g_wl.xkb_context) g_wl.compose_table = xkb_compose_table_new_from_locale(g_wl.xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (g_wl.compose_table) g_wl.compose_state = xkb_compose_state_new(g_wl.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
}
void rvn_wl_input_forget(rvn_pwindow_t* window) {
	if (g_wl.pointer_focus == window) g_wl.pointer_focus = NULL;
	if (g_wl.keyboard_focus == window) { g_wl.keyboard_focus = NULL; g_wl.repeating = 0; }
}
void rvn_wl_input_shutdown(void) {
	if (g_wl.pointer_focus) rvn_wl_pointer_leave(NULL, g_wl.pointer, 0, NULL);
	if (g_wl.keyboard_focus) rvn_wl_keyboard_leave(NULL, g_wl.keyboard, 0, NULL);
	if (g_wl.pointer) wl_pointer_release(g_wl.pointer);
	if (g_wl.keyboard) wl_keyboard_release(g_wl.keyboard);
	if (g_wl.seat) wl_seat_release(g_wl.seat);
	if (g_wl.cursor_surface) wl_surface_destroy(g_wl.cursor_surface);
	if (g_wl.cursor_theme) wl_cursor_theme_destroy(g_wl.cursor_theme);
	if (g_wl.compose_state) xkb_compose_state_unref(g_wl.compose_state);
	if (g_wl.compose_table) xkb_compose_table_unref(g_wl.compose_table);
	if (g_wl.xkb_state) xkb_state_unref(g_wl.xkb_state);
	if (g_wl.keymap) xkb_keymap_unref(g_wl.keymap);
	if (g_wl.xkb_context) xkb_context_unref(g_wl.xkb_context);
	g_wl.pointer = NULL; g_wl.keyboard = NULL; g_wl.seat = NULL;
	g_wl.cursor_surface = NULL; g_wl.cursor_theme = NULL;
	g_wl.compose_state = NULL; g_wl.compose_table = NULL;
	g_wl.xkb_state = NULL; g_wl.keymap = NULL; g_wl.xkb_context = NULL;
	g_wl.pointer_focus = g_wl.keyboard_focus = NULL;
	g_wl.repeating = 0; g_wl.modifiers = 0; g_wl.input_serial = 0;
}
