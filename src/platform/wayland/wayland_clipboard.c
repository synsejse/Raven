/** UTF-8 clipboard via wl_data_device, with bounded, nonblocking pipe I/O. */
#include "wayland_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define RVN_WL_CLIPBOARD_LIMIT (16u * 1024u * 1024u)

typedef struct rvn_wl_offer_t {
	struct rvn_wl_offer_t* next;
	struct wl_data_offer* proxy;
	const char* mime;
	int priority;
} rvn_wl_offer_t;

typedef struct rvn_wl_transfer_t {
	struct rvn_wl_transfer_t* next;
	int fd;
	char* text;
	size_t offset, length;
	double deadline;
} rvn_wl_transfer_t;

static struct {
	struct wl_data_device* device;
	struct wl_data_source* source;
	rvn_wl_offer_t* offers;
	rvn_wl_offer_t* selection;
	rvn_wl_offer_t* drag;
	rvn_wl_transfer_t* transfers;
	char* text;
	int pending;
} g_clipboard;

static void rvn_wl_offer_destroy(rvn_wl_offer_t* offer) {
	rvn_wl_offer_t** link = &g_clipboard.offers;
	if (!offer) return;
	while (*link && *link != offer) link = &(*link)->next;
	if (*link) *link = offer->next;
	if (g_clipboard.selection == offer) g_clipboard.selection = NULL;
	if (g_clipboard.drag == offer) g_clipboard.drag = NULL;
	wl_data_offer_destroy(offer->proxy);
	rvn_free(offer);
}
static void rvn_wl_offer_mime(void* data, struct wl_data_offer* proxy, const char* mime) {
	rvn_wl_offer_t* offer = data;
	int priority = 0;
	const char* chosen = NULL;
	RVN_UNUSED(proxy);
	if (strcmp(mime, "text/plain;charset=utf-8") == 0) { priority = 3; chosen = "text/plain;charset=utf-8"; }
	else if (strcmp(mime, "UTF8_STRING") == 0) { priority = 2; chosen = "UTF8_STRING"; }
	else if (strcmp(mime, "text/plain") == 0) { priority = 1; chosen = "text/plain"; }
	if (priority > offer->priority) { offer->priority = priority; offer->mime = chosen; }
}
static void rvn_wl_offer_actions(void* data, struct wl_data_offer* offer, uint32_t actions) { RVN_UNUSED(data); RVN_UNUSED(offer); RVN_UNUSED(actions); }
static const struct wl_data_offer_listener offer_listener = {
	.offer = rvn_wl_offer_mime, .source_actions = rvn_wl_offer_actions, .action = rvn_wl_offer_actions
};
static void rvn_wl_data_offer(void* data, struct wl_data_device* device, struct wl_data_offer* proxy) {
	rvn_wl_offer_t* offer = rvn_calloc(sizeof(*offer));
	RVN_UNUSED(data); RVN_UNUSED(device);
	offer->proxy = proxy;
	offer->next = g_clipboard.offers;
	g_clipboard.offers = offer;
	wl_data_offer_add_listener(proxy, &offer_listener, offer);
}
static void rvn_wl_data_selection(void* data, struct wl_data_device* device, struct wl_data_offer* proxy) {
	rvn_wl_offer_t* offer = proxy ? wl_data_offer_get_user_data(proxy) : NULL;
	RVN_UNUSED(data); RVN_UNUSED(device);
	if (g_clipboard.selection != offer) rvn_wl_offer_destroy(g_clipboard.selection);
	g_clipboard.selection = offer;
}
/* External drag-and-drop is explicitly declined until the native backend can
 * deliver it with the same acceptance/lifetime contract as Raven's other ports. */
static void rvn_wl_data_enter(void* data, struct wl_data_device* device, uint32_t serial,
	struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer* proxy) {
	RVN_UNUSED(data); RVN_UNUSED(device); RVN_UNUSED(surface); RVN_UNUSED(x); RVN_UNUSED(y);
	rvn_wl_offer_destroy(g_clipboard.drag);
	g_clipboard.drag = proxy ? wl_data_offer_get_user_data(proxy) : NULL;
	if (proxy) wl_data_offer_accept(proxy, serial, NULL);
}
static void rvn_wl_data_leave(void* data, struct wl_data_device* device) {
	RVN_UNUSED(data); RVN_UNUSED(device);
	rvn_wl_offer_destroy(g_clipboard.drag);
}
static void rvn_wl_data_motion(void* data, struct wl_data_device* device, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	RVN_UNUSED(data); RVN_UNUSED(device); RVN_UNUSED(time); RVN_UNUSED(x); RVN_UNUSED(y);
}
static void rvn_wl_data_drop(void* data, struct wl_data_device* device) { rvn_wl_data_leave(data, device); }
static const struct wl_data_device_listener device_listener = {
	.data_offer = rvn_wl_data_offer, .enter = rvn_wl_data_enter, .leave = rvn_wl_data_leave,
	.motion = rvn_wl_data_motion, .drop = rvn_wl_data_drop, .selection = rvn_wl_data_selection
};

/* A receiver may close its pipe at any time. Block SIGPIPE only on this thread;
 * do not change the application's signal handlers. */
static ssize_t rvn_wl_pipe_write(int fd, const char* text, size_t size) {
	sigset_t blocked, saved, pending;
	struct timespec no_wait = { 0, 0 };
	ssize_t written;
	int had_signal, saved_errno;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &blocked, &saved) != 0) { errno = EIO; return -1; }
	sigpending(&pending);
	had_signal = sigismember(&pending, SIGPIPE);
	written = write(fd, text, size);
	saved_errno = errno;
	if (written < 0 && saved_errno == EPIPE && !had_signal) {
		while (sigtimedwait(&blocked, NULL, &no_wait) < 0 && errno == EINTR) {}
	}
	pthread_sigmask(SIG_SETMASK, &saved, NULL);
	errno = saved_errno;
	return written;
}
void rvn_wl_clipboard_flush(void) {
	rvn_wl_transfer_t** link = &g_clipboard.transfers;
	while (*link) {
		rvn_wl_transfer_t* transfer = *link;
		int finished = rvn_clock_seconds() >= transfer->deadline;
		while (!finished && transfer->offset < transfer->length) {
			ssize_t sent = rvn_wl_pipe_write(transfer->fd, transfer->text + transfer->offset, transfer->length - transfer->offset);
			if (sent > 0) transfer->offset += (size_t)sent;
			else if (sent < 0 && errno == EINTR) continue;
			else { finished = sent == 0 || (errno != EAGAIN && errno != EWOULDBLOCK); break; }
		}
		if (finished || transfer->offset == transfer->length) {
			*link = transfer->next;
			close(transfer->fd);
			rvn_free(transfer->text);
			rvn_free(transfer);
		} else link = &transfer->next;
	}
}
int rvn_wl_clipboard_pending(void) { return g_clipboard.transfers != NULL; }

static void rvn_wl_source_target(void* data, struct wl_data_source* source, const char* mime) { RVN_UNUSED(data); RVN_UNUSED(source); RVN_UNUSED(mime); }
static void rvn_wl_source_send(void* data, struct wl_data_source* source, const char* mime, int32_t fd) {
	rvn_wl_transfer_t* transfer;
	RVN_UNUSED(data); RVN_UNUSED(source); RVN_UNUSED(mime);
	if (!rvn_wl_fd_flags(fd)) { close(fd); return; }
	transfer = rvn_calloc(sizeof(*transfer));
	transfer->fd = fd;
	transfer->text = rvn_strdup(g_clipboard.text ? g_clipboard.text : "");
	transfer->length = strlen(transfer->text);
	transfer->deadline = rvn_clock_seconds() + 5.0;
	transfer->next = g_clipboard.transfers;
	g_clipboard.transfers = transfer;
	rvn_wl_clipboard_flush();
}
static void rvn_wl_source_cancelled(void* data, struct wl_data_source* source) {
	RVN_UNUSED(data);
	if (source == g_clipboard.source) g_clipboard.source = NULL;
	wl_data_source_destroy(source);
}
static const struct wl_data_source_listener source_listener = {
	.target = rvn_wl_source_target, .send = rvn_wl_source_send, .cancelled = rvn_wl_source_cancelled
};

void rvn_wl_clipboard_init(void) {
	if (!g_clipboard.device && g_wl.data_manager && g_wl.seat) {
		g_clipboard.device = wl_data_device_manager_get_data_device(g_wl.data_manager, g_wl.seat);
		wl_data_device_add_listener(g_clipboard.device, &device_listener, NULL);
	}
}
void rvn_wl_clipboard_serial(uint32_t serial) {
	if (!g_clipboard.pending || !g_clipboard.device || !serial) return;
	if (g_clipboard.source) wl_data_source_destroy(g_clipboard.source);
	g_clipboard.source = wl_data_device_manager_create_data_source(g_wl.data_manager);
	wl_data_source_add_listener(g_clipboard.source, &source_listener, NULL);
	wl_data_source_offer(g_clipboard.source, "text/plain;charset=utf-8");
	wl_data_source_offer(g_clipboard.source, "UTF8_STRING");
	wl_data_source_offer(g_clipboard.source, "text/plain");
	wl_data_device_set_selection(g_clipboard.device, g_clipboard.source, serial);
	g_clipboard.pending = 0;
}
void rvn_wl_clipboard_set(const char* text) {
	if (!text) text = "";
	if (strlen(text) > RVN_WL_CLIPBOARD_LIMIT) {
		rvn_error(R_ERROR_INVALID_VALUE, R_SEVERITY_WARNING, "Wayland: clipboard text exceeds 16 MiB");
		return;
	}
	rvn_str_set(&g_clipboard.text, text);
	g_clipboard.pending = 1;
	rvn_wl_clipboard_serial(g_wl.input_serial);
	if (g_wl.display) wl_display_flush(g_wl.display);
}
char* rvn_wl_clipboard_get(void) {
	int fds[2];
	char* text;
	size_t size = 0, capacity = 4096;
	double deadline;
	if (g_clipboard.source || g_clipboard.pending) return rvn_strdup(g_clipboard.text ? g_clipboard.text : "");
	if (!g_clipboard.selection || !g_clipboard.selection->mime || pipe(fds) != 0) return NULL;
	if (!rvn_wl_fd_flags(fds[0]) || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) { close(fds[0]); close(fds[1]); return NULL; }
	wl_data_offer_receive(g_clipboard.selection->proxy, g_clipboard.selection->mime, fds[1]);
	close(fds[1]);
	text = rvn_alloc(capacity);
	deadline = rvn_clock_seconds() + 1.0;
	while (rvn_clock_seconds() < deadline) {
		ssize_t received;
		if (size + 1 == capacity) {
			if (capacity >= RVN_WL_CLIPBOARD_LIMIT) break;
			capacity *= 2;
			text = rvn_realloc(text, capacity);
		}
		received = read(fds[0], text + size, capacity - size - 1);
		if (received > 0) { size += (size_t)received; continue; }
		if (received == 0) { text[size] = '\0'; close(fds[0]); return text; }
		if (errno == EINTR) continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) break;
		/* Also services send requests when the clipboard manager asks us to
		 * transfer a previous selection. User callbacks remain deferred. */
		if (!rvn_wl_dispatch(0.01, fds[0])) break;
	}
	close(fds[0]);
	rvn_free(text);
	return NULL;
}
void rvn_wl_clipboard_shutdown(void) {
	while (g_clipboard.offers) rvn_wl_offer_destroy(g_clipboard.offers);
	if (g_clipboard.source) wl_data_source_destroy(g_clipboard.source);
	if (g_clipboard.device) {
		if (wl_data_device_get_version(g_clipboard.device) >= 2) wl_data_device_release(g_clipboard.device);
		else wl_data_device_destroy(g_clipboard.device);
	}
	while (g_clipboard.transfers) {
		rvn_wl_transfer_t* transfer = g_clipboard.transfers;
		g_clipboard.transfers = transfer->next;
		close(transfer->fd);
		rvn_free(transfer->text);
		rvn_free(transfer);
	}
	rvn_free(g_clipboard.text);
	memset(&g_clipboard, 0, sizeof(g_clipboard));
}
