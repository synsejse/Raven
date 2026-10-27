/** Native Wayland lifecycle regression tests. Requires a compositor and Vulkan.
 * Both threaded and synchronous rendering run against the actual native port.
 */
#define _POSIX_C_SOURCE 200809L
#include "test.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

static void draw_frames(Rid window, int count) {
	int i;
	for (i = 0; i < count; i++) {
		Rwindow_request_redraw(window);
		CHECK(Rdraw_frame());
	}
}

static void check_capture(Rid window) {
	int width = 0, height = 0, expected_width = 0, expected_height = 0;
	uint8_t* pixels;
	Rwindow_get_framebuffer_size(window, &expected_width, &expected_height);
	pixels = Rwindow_capture(window, &width, &height);
	CHECK(pixels != NULL);
	CHECK(width == expected_width && height == expected_height);
	if (pixels && width > 0 && height > 0) {
		/* The content is solid green, so this also catches empty/black buffers. */
		const uint8_t* center = pixels + ((size_t)(height / 2) * (size_t)width + (size_t)(width / 2)) * 4;
		CHECK(center[1] > 240 && center[0] < 10 && center[2] < 10);
	}
	Rpixels_free(pixels);
}

static void posted(void* data) { *(int*)data = 1; }
static void* post_later(void* data) {
	struct timespec delay = { 0, 30000000 };
	nanosleep(&delay, NULL);
	Rtask_post(posted, data);
	return NULL;
}

static void test_native_lifecycle(void) {
	Rid window, other, panel;
	RNativeWindow native;
	pthread_t thread;
	int woke = 0, created, width, height;
	double deadline;
	/* Before any input serial: verify the local pending copy without replacing
	 * the user's compositor clipboard. Publication waits for a user action. */
	Rclipboard_set_text("Wayland clipboard: caf\xc3\xa9");
	CHECK_STR(Rclipboard_get_text(), "Wayland clipboard: caf\xc3\xa9");
	window = Rwindow_create("Raven Wayland regression", 321, 241, R_WINDOW_DEFAULT);
	CHECK(Rwindow_is_valid(window));
	if (!Rwindow_is_valid(window)) return;
	CHECK(Rwindow_get_native_handle(window, &native));
	CHECK(native.system == R_WINDOW_SYSTEM_WAYLAND && native.display != NULL && native.window != 0);
	panel = Rwidget_create(R_WIDGET_BORDER);
	Rborder_set_background(panel, 0, 1, 0, 1);
	Rwindow_set_content(window, panel);
	draw_frames(window, 5);
	check_capture(window);

	Rwindow_set_size(window, 413, 287);
	draw_frames(window, 5);
	Rwindow_get_size(window, &width, &height);
	CHECK_NEAR(width, 413);
	CHECK_NEAR(height, 287);
	check_capture(window);

	Rwindow_hide(window);
	CHECK(Rdraw_frame());
	Rwindow_show(window);
	draw_frames(window, 5);
	check_capture(window);

	/* Destroy another window with configure/input events potentially pending. */
	other = Rwindow_create("Wayland temporary window", 200, 120, R_WINDOW_HIDDEN | R_WINDOW_NO_RESIZE);
	CHECK(Rwindow_is_valid(other));
	if (Rwindow_is_valid(other)) {
		Rwindow_show(other);
		draw_frames(other, 2);
		Rwindow_destroy(other);
	}
	draw_frames(window, 2);
	check_capture(window);

	created = pthread_create(&thread, NULL, post_later, &woke) == 0;
	CHECK(created);
	if (created) {
		deadline = Rget_time() + 2.0;
		while (!woke && Rget_time() < deadline) Rwait_frame(0.5);
		pthread_join(thread, NULL);
		CHECK(woke);
	}
	Rwindow_destroy(window);
}

int main(void) {
	int synchronous;
	if (!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")) return 77;
	for (synchronous = 0; synchronous <= 1; synchronous++) {
		RInitDesc desc = { 0 };
		desc.backend = R_BACKEND_VULKAN;
		desc.disable_render_thread = synchronous;
		desc.disable_vsync = R_TRUE;
		desc.worker_threads = 1;
		Rerror_set_callback(test_error_callback);
		if (!Rinit_ex(&desc)) return 1;
		Rset_animations_enabled(R_FALSE);
		printf("Wayland lifecycle (%s rendering)\n", synchronous ? "synchronous" : "threaded");
		RUN(test_native_lifecycle);
		Rshutdown();
	}
	return test_report("test_wayland");
}
