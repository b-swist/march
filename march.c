#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <river-window-management-v1-client-protocol.h>
#include <river-xkb-bindings-v1-client-protocol.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "config.h"
#include "march.h"

struct Bindings binds;

struct WindowManager {
	struct wl_list outputs; // Output
	struct wl_list windows; // Window
	struct wl_list seats;   // Seat
} wm;

struct river_window_manager_v1 *window_manager_v1;
struct river_xkb_bindings_v1 *xkb_bindings_v1;

static void output_handle_removed(void *data, struct river_output_v1 *obj)
{
	Output *output = data;
	output->removed = true;
}

// Ignored events
static void output_handle_wl_output(void *data, struct river_output_v1 *obj, uint32_t name) {}
static void output_handle_position(void *data, struct river_output_v1 *obj, int32_t x, int32_t y) {}
static void output_handle_dimensions(void *data, struct river_output_v1 *obj, int32_t width, int32_t height) {}

const struct river_output_v1_listener river_output_listener = {
	.removed = output_handle_removed,
	.wl_output = output_handle_wl_output,
	.position = output_handle_position,
	.dimensions = output_handle_dimensions,
};

static void output_maybe_destroy(Output *output)
{
	if (!output->removed) {
		return;
	}
	river_output_v1_destroy(output->obj);
	wl_list_remove(&output->link);
	free(output);
}

static void window_handle_closed(void *data, struct river_window_v1 *obj)
{
	Window *window = data;
	window->closed = true;
}

static void window_handle_dimensions(void *data, struct river_window_v1 *obj, int32_t width, int32_t height)
{
	Window *window = data;
	window->width = width;
	window->height = height;
}

static void
window_handle_pointer_move_requested(void *data, struct river_window_v1 *obj, struct river_seat_v1 *river_seat)
{
	Window *window = data;
	window->pointer_move_requested = river_seat_v1_get_user_data(river_seat);
}

static void window_handle_pointer_resize_requested(void *data, struct river_window_v1 *obj, struct river_seat_v1 *river_seat, uint32_t edges)
{
	Window *window = data;
	window->pointer_resize_requested = river_seat_v1_get_user_data(river_seat);
	window->pointer_resize_requested_edges = edges;
}

// Ignored events
static void window_handle_dimensions_hint(void *data, struct river_window_v1 *obj, int32_t min_width, int32_t min_height, int32_t max_width, int32_t max_height) {}
static void window_handle_app_id(void *data, struct river_window_v1 *obj, const char *app_id) {}
static void window_handle_title(void *data, struct river_window_v1 *obj, const char *title) {}
static void window_handle_parent(void *data, struct river_window_v1 *obj, struct river_window_v1 *parent) {}
static void window_handle_decoration_hint(void *data, struct river_window_v1 *obj, uint32_t hint) {}
static void window_handle_show_window_menu_requested(void *data, struct river_window_v1 *obj, int32_t x, int32_t y) {}
static void window_handle_maximize_requested(void *data, struct river_window_v1 *obj) {}
static void window_handle_unmaximize_requested(void *data, struct river_window_v1 *obj) {}
static void window_handle_fullscreen_requested(void *data, struct river_window_v1 *obj, struct river_output_v1 *river_output) {}
static void window_handle_exit_fullscreen_requested(void *data, struct river_window_v1 *obj) {}
static void window_handle_minimize_requested(void *data, struct river_window_v1 *obj) {}
static void window_handle_unreliable_pid(void *data, struct river_window_v1 *obj, int32_t unreliable_pid) {}
static void window_handle_presentation_hint(void *data, struct river_window_v1 *obj, uint32_t hint) {}
static void window_handle_identifier(void *data, struct river_window_v1 *obj, const char *identifier) {}

const struct river_window_v1_listener river_window_listener = {
	.closed = window_handle_closed,
	.dimensions_hint = window_handle_dimensions_hint,
	.dimensions = window_handle_dimensions,
	.app_id = window_handle_app_id,
	.title = window_handle_title,
	.parent = window_handle_parent,
	.decoration_hint = window_handle_decoration_hint,
	.pointer_move_requested = window_handle_pointer_move_requested,
	.pointer_resize_requested = window_handle_pointer_resize_requested,
	.show_window_menu_requested = window_handle_show_window_menu_requested,
	.maximize_requested = window_handle_maximize_requested,
	.unmaximize_requested = window_handle_unmaximize_requested,
	.fullscreen_requested = window_handle_fullscreen_requested,
	.exit_fullscreen_requested = window_handle_exit_fullscreen_requested,
	.minimize_requested = window_handle_minimize_requested,
	.unreliable_pid = window_handle_unreliable_pid,
	.presentation_hint = window_handle_presentation_hint,
	.identifier = window_handle_identifier,
};

static void window_maybe_destroy(Window *window)
{
	if (!window->closed) {
		return;
	}

	Seat *seat;
	wl_list_for_each(seat, &wm.seats, link)
	{
		if (seat->focused == window) {
			seat->focused = NULL;
		}
		if (seat->op_window == window) {
			river_seat_v1_op_end(seat->obj);
			seat->op = SEAT_OP_NONE;
			seat->op_window = NULL;
		}
	}

	river_window_v1_destroy(window->obj);
	wl_list_remove(&window->link);
	free(window);
}

static void window_set_position(Window *window, int32_t x, int32_t y)
{
	river_node_v1_set_position(window->node, x, y);
	window->x = x;
	window->y = y;
}

static void seat_pointer_move(Seat *seat, Window *window);
static void seat_pointer_resize(Seat *seat, Window *window, uint32_t edges);

static void window_manage(Window *window)
{
	if (window->new) {
		window->new = false;
		window_set_position(window, 0, 0);
		river_window_v1_propose_dimensions(window->obj, 0, 0);
	}
	if (window->pointer_move_requested != NULL) {
		seat_pointer_move(window->pointer_move_requested, window);
		window->pointer_move_requested = NULL;
	}
	if (window->pointer_resize_requested != NULL) {
		seat_pointer_resize(window->pointer_resize_requested, window, window->pointer_resize_requested_edges);
		window->pointer_resize_requested = NULL;
	}
}

static void xkb_binding_handle_pressed(void *data, struct river_xkb_binding_v1 *obj)
{
	XkbBinding *binding = data;
	binding->seat->pending_action = binding->action;
}

static void xkb_binding_handle_released(void *data, struct river_xkb_binding_v1 *obj) {}

const struct river_xkb_binding_v1_listener river_xkb_binding_listener = {
	.pressed = xkb_binding_handle_pressed,
	.released = xkb_binding_handle_released,
};

static void xkb_binding_destroy(XkbBinding *binding)
{
	river_xkb_binding_v1_destroy(binding->obj);
	wl_list_remove(&binding->link);
	free(binding);
}

void xkb_binding_create(Seat *seat, uint32_t mods, xkb_keysym_t keysym, march_action action)
{
	XkbBinding *binding = calloc(1, sizeof(XkbBinding));
	binding->obj = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings_v1, seat->obj, keysym, mods);
	binding->seat = seat;
	binding->action = action;

	river_xkb_binding_v1_add_listener(binding->obj, &river_xkb_binding_listener, binding);
	river_xkb_binding_v1_enable(binding->obj);

	wl_list_insert(seat->xkb_bindings.prev, &binding->link);
}

static void pointer_binding_handle_pressed(void *data, struct river_pointer_binding_v1 *obj)
{
	PointerBinding *binding = data;
	binding->seat->pending_action = binding->action;
}

static void pointer_binding_handle_released(void *data, struct river_pointer_binding_v1 *obj) {}

const struct river_pointer_binding_v1_listener river_pointer_binding_listener = {
	.pressed = pointer_binding_handle_pressed,
	.released = pointer_binding_handle_released,
};

static void pointer_binding_destroy(PointerBinding *binding)
{
	river_pointer_binding_v1_destroy(binding->obj);
	wl_list_remove(&binding->link);
	free(binding);
}

void pointer_binding_create(Seat *seat, uint32_t mods, uint32_t button, march_action action)
{
	PointerBinding *binding = calloc(1, sizeof(PointerBinding));
	binding->obj = river_seat_v1_get_pointer_binding(seat->obj, button, mods);
	binding->seat = seat;
	binding->action = action;

	river_pointer_binding_v1_add_listener(binding->obj, &river_pointer_binding_listener, binding);
	river_pointer_binding_v1_enable(binding->obj);

	wl_list_insert(seat->pointer_bindings.prev, &binding->link);
}

static void seat_handle_removed(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;
	seat->removed = true;
}

static void seat_handle_pointer_enter(void *data, struct river_seat_v1 *obj, struct river_window_v1 *river_window)
{
	Seat *seat = data;
	seat->hovered = river_window_v1_get_user_data(river_window);
}

static void seat_handle_pointer_leave(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;
	seat->hovered = NULL;
}

static void seat_handle_window_interaction(void *data, struct river_seat_v1 *obj, struct river_window_v1 *river_window)
{
	Seat *seat = data;
	seat->interacted = river_window_v1_get_user_data(river_window);
}

static void seat_handle_op_delta(void *data, struct river_seat_v1 *obj, int32_t dx, int32_t dy)
{
	Seat *seat = data;
	seat->op_dx = dx;
	seat->op_dy = dy;
}

static void seat_handle_op_release(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;
	seat->op_release = true;
}

// Ignored events
static void seat_handle_wl_seat(void *data, struct river_seat_v1 *obj, uint32_t id) {}
static void seat_handle_shell_surface_interaction(void *data, struct river_seat_v1 *obj, struct river_shell_surface_v1 *river_shell_surface) {}
static void seat_handle_pointer_position(void *data, struct river_seat_v1 *obj, int32_t x, int32_t y) {}

const struct river_seat_v1_listener river_seat_listener = {
	.removed = seat_handle_removed,
	.wl_seat = seat_handle_wl_seat,
	.pointer_enter = seat_handle_pointer_enter,
	.pointer_leave = seat_handle_pointer_leave,
	.window_interaction = seat_handle_window_interaction,
	.shell_surface_interaction = seat_handle_shell_surface_interaction,
	.op_delta = seat_handle_op_delta,
	.op_release = seat_handle_op_release,
	.pointer_position = seat_handle_pointer_position,
};

static void seat_maybe_destroy(Seat *seat)
{
	if (!seat->removed) {
		return;
	}

	XkbBinding *xkb_binding, *xkb_binding_tmp;
	wl_list_for_each_safe(xkb_binding, xkb_binding_tmp, &seat->xkb_bindings, link) { xkb_binding_destroy(xkb_binding); }

	PointerBinding *pointer_binding, *pointer_binding_tmp;
	wl_list_for_each_safe(pointer_binding, pointer_binding_tmp, &seat->pointer_bindings, link) { pointer_binding_destroy(pointer_binding); }

	river_seat_v1_destroy(seat->obj);
	wl_list_remove(&seat->link);
	free(seat);
}

static void seat_focus(Seat *seat, Window *window)
{
	// Focus the top window (if any) when there is no explicit target.
	if (window == NULL && !wl_list_empty(&wm.windows)) {
		window = wl_container_of(wm.windows.prev, window, link);
	}

	if (seat->focused == window) {
		return;
	}

	if (window != NULL) {
		river_seat_v1_focus_window(seat->obj, window->obj);
		river_node_v1_place_top(window->node);
		wl_list_remove(&window->link);
		wl_list_insert(wm.windows.prev, &window->link);
	} else {
		river_seat_v1_clear_focus(seat->obj);
	}

	seat->focused = window;
}

static void seat_pointer_move(Seat *seat, Window *window)
{
	seat_focus(seat, window);
	river_seat_v1_op_start_pointer(seat->obj);
	seat->op = SEAT_OP_MOVE;
	seat->op_window = window;
	seat->op_start_x = window->x;
	seat->op_start_y = window->y;
	seat->op_dx = 0;
	seat->op_dy = 0;
}

static void seat_pointer_resize(Seat *seat, Window *window, uint32_t edges)
{
	seat_focus(seat, window);
	river_window_v1_inform_resize_start(window->obj);
	river_seat_v1_op_start_pointer(seat->obj);
	seat->op = SEAT_OP_RESIZE;
	seat->op_window = window;
	seat->op_edges = edges;
	seat->op_start_x = window->x;
	seat->op_start_y = window->y;
	seat->op_start_width = window->width;
	seat->op_start_height = window->height;
	seat->op_dx = 0;
	seat->op_dy = 0;
}

static void seat_manage(Seat *seat)
{
	if (seat->new) {
		seat->new = false;

		ConfigKeyBinding *kb;
		wl_list_for_each(kb, &binds.config_key_bindings, link) { xkb_binding_create(seat, kb->mods, kb->keysym, kb->action); }

		ConfigButtonBinding *bb;
		wl_list_for_each(bb, &binds.config_button_bindings, link) { pointer_binding_create(seat, bb->mods, bb->button, bb->action); }
	}

	// If no window was interacted with in the current manage sequence,
	// intentionally pass NULL to ensure the window on top has focus.
	// This is necessary to handle new windows for example.
	seat_focus(seat, seat->interacted);
	seat->interacted = NULL;

	if (seat->pending_action != NULL) {
		seat->pending_action(seat);
		seat->pending_action = NULL;
	}

	switch (seat->op) {
	case SEAT_OP_NONE:
		break;
	case SEAT_OP_MOVE:
		if (seat->op_release) {
			river_seat_v1_op_end(seat->obj);
			seat->op = SEAT_OP_NONE;
			seat->op_window = NULL;
			break;
		}
		break;
	case SEAT_OP_RESIZE:
		if (seat->op_release) {
			river_window_v1_inform_resize_end(seat->op_window->obj);
			river_seat_v1_op_end(seat->obj);
			seat->op = SEAT_OP_NONE;
			seat->op_window = NULL;
			break;
		}
		int32_t width = seat->op_start_width;
		int32_t height = seat->op_start_height;
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
			width -= seat->op_dx;
		}
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_RIGHT) != 0) {
			width += seat->op_dx;
		}
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
			height -= seat->op_dy;
		}
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_BOTTOM) != 0) {
			height += seat->op_dy;
		}
		river_window_v1_propose_dimensions(
			seat->op_window->obj, width > 1 ? width : 1, height > 1 ? height : 1);
		break;
	}
	seat->op_release = false;
}

static void seat_render(Seat *seat)
{
	switch (seat->op) {
	case SEAT_OP_NONE:
		break;
	case SEAT_OP_MOVE:
		window_set_position(seat->op_window, seat->op_start_x + seat->op_dx, seat->op_start_y + seat->op_dy);
		break;
	case SEAT_OP_RESIZE:;
		int32_t x = seat->op_start_x;
		int32_t y = seat->op_start_y;
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_LEFT) != 0) {
			x += seat->op_start_width - seat->op_window->width;
		}
		if ((seat->op_edges & RIVER_WINDOW_V1_EDGES_TOP) != 0) {
			y += seat->op_start_height - seat->op_window->height;
		}
		window_set_position(seat->op_window, x, y);
		break;
	}
}

void action_close(Seat *seat)
{
	if (seat->focused != NULL) {
		river_window_v1_close(seat->focused->obj);
	}
}

void action_focus_next(Seat *seat)
{
	if (!wl_list_empty(&wm.windows)) {
		// Focus the bottom window
		Window *window = wl_container_of(wm.windows.next, window, link);
		seat_focus(seat, window);
	}
}

void action_spawn_foot(Seat *seat)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("foot", "foot", (char *)0);
		perror("execlp failed");
	}
}

void action_move(Seat *seat)
{
	if (seat->op == SEAT_OP_NONE && seat->hovered != NULL) {
		seat_pointer_move(seat, seat->hovered);
	}
}

void action_resize(Seat *seat)
{
	if (seat->op == SEAT_OP_NONE && seat->hovered != NULL) {
		seat_pointer_resize(seat, seat->hovered, RIVER_WINDOW_V1_EDGES_BOTTOM | RIVER_WINDOW_V1_EDGES_RIGHT);
	}
}

void action_exit(Seat *seat)
{
	river_window_manager_v1_exit_session(window_manager_v1);
}

static void window_manager_handle_unavailable(void *data, struct river_window_manager_v1 *obj)
{
	fputs("error: another window manager is already running", stderr);
	exit(EXIT_FAILURE);
}

static void window_manager_handle_finished(void *data, struct river_window_manager_v1 *obj)
{
	exit(EXIT_SUCCESS);
}

static void window_manager_handle_manage_start(void *data, struct river_window_manager_v1 *obj)
{
	// Destroy closed windows and removed outputs/seats
	Output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &wm.outputs, link) { output_maybe_destroy(output); }
	Window *window, *window_tmp;
	wl_list_for_each_safe(window, window_tmp, &wm.windows, link) { window_maybe_destroy(window); }
	Seat *seat, *seat_tmp;
	wl_list_for_each_safe(seat, seat_tmp, &wm.seats, link) { seat_maybe_destroy(seat); }

	// Carry out window management policy
	wl_list_for_each(window, &wm.windows, link) { window_manage(window); }
	wl_list_for_each(seat, &wm.seats, link) { seat_manage(seat); }

	river_window_manager_v1_manage_finish(window_manager_v1);
}

static void window_manager_handle_render_start(void *data, struct river_window_manager_v1 *obj)
{
	Seat *seat;
	wl_list_for_each(seat, &wm.seats, link) { seat_render(seat); }

	river_window_manager_v1_render_finish(window_manager_v1);
}

static void window_manager_handle_window(void *data, struct river_window_manager_v1 *obj, struct river_window_v1 *river_window)
{
	Window *window = calloc(1, sizeof(Window));
	window->obj = river_window;
	window->node = river_window_v1_get_node(window->obj);
	window->new = true;

	river_window_v1_add_listener(window->obj, &river_window_listener, window);

	wl_list_insert(wm.windows.prev, &window->link);
}

static void window_manager_handle_output(void *data, struct river_window_manager_v1 *obj, struct river_output_v1 *river_output)
{
	Output *output = calloc(1, sizeof(Output));
	output->obj = river_output;

	river_output_v1_add_listener(output->obj, &river_output_listener, output);

	wl_list_insert(wm.outputs.prev, &output->link);
}

static void window_manager_handle_seat(void *data, struct river_window_manager_v1 *obj, struct river_seat_v1 *river_seat)
{
	Seat *seat = calloc(1, sizeof(Seat));
	seat->obj = river_seat;
	seat->new = true;
	wl_list_init(&seat->xkb_bindings);
	wl_list_init(&seat->pointer_bindings);

	river_seat_v1_add_listener(seat->obj, &river_seat_listener, seat);

	wl_list_insert(wm.seats.prev, &seat->link);
}

// Ignored events
static void window_manager_handle_session_locked(void *data, struct river_window_manager_v1 *obj) {}
static void window_manager_handle_session_unlocked(void *data, struct river_window_manager_v1 *obj) {}

static const struct river_window_manager_v1_listener window_manager_listener = {
	.unavailable = window_manager_handle_unavailable,
	.finished = window_manager_handle_finished,
	.manage_start = window_manager_handle_manage_start,
	.render_start = window_manager_handle_render_start,
	.session_locked = window_manager_handle_session_locked,
	.session_unlocked = window_manager_handle_session_unlocked,
	.window = window_manager_handle_window,
	.output = window_manager_handle_output,
	.seat = window_manager_handle_seat,
};

static void window_manager_init(void)
{
	wl_list_init(&wm.outputs);
	wl_list_init(&wm.windows);
	wl_list_init(&wm.seats);
}

static void handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	if (strcmp(interface, river_window_manager_v1_interface.name) == 0) {
		if (version >= 4) {
			window_manager_v1 = wl_registry_bind(registry, name, &river_window_manager_v1_interface, 4);
		}
	} else if (strcmp(interface, river_xkb_bindings_v1_interface.name) == 0) {
		xkb_bindings_v1 = wl_registry_bind(registry, name, &river_xkb_bindings_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(void)
{
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fputs("failed to connect to Wayland server", stderr);
		return EXIT_FAILURE;
	}

	// Avoid passing WAYLAND_DEBUG on to our children.
	// It only matters if it's set when the display is created.
	unsetenv("WAYLAND_DEBUG");

	// Ensure children are automatically reaped.
	signal(SIGCHLD, SIG_IGN);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) < 0) {
		fputs("roundtrip failed", stderr);
		return EXIT_FAILURE;
	}

	if (window_manager_v1 == NULL || xkb_bindings_v1 == NULL) {
		fputs("river_window_manager_v1 or river_xkb_bindings_v1 not supported by the Wayland server", stderr);
		return EXIT_FAILURE;
	}

	window_manager_init();
	config_init();

	river_window_manager_v1_add_listener(window_manager_v1, &window_manager_listener, NULL);

	while (true) {
		if (wl_display_dispatch(display) < 0) {
			fputs("dispatch failed", stderr);
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
