#ifndef MARCH_MARCH_H
#define MARCH_MARCH_H

#include <stdint.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

typedef struct Seat Seat;
typedef struct Output Output;
typedef struct Window Window;

typedef void (*march_action)(Seat *seat);

struct Output {
	struct river_output_v1 *obj;
	bool removed;
	struct wl_list link; // WindowManager.outputs
};

struct Window {
	struct river_window_v1 *obj;
	struct river_node_v1 *node;

	bool new;
	bool closed;

	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;

	Seat *pointer_move_requested;
	Seat *pointer_resize_requested;
	uint32_t pointer_resize_requested_edges;

	struct wl_list link; // WindowManager.windows
};

typedef struct {
	struct river_xkb_binding_v1 *obj;
	Seat *seat;
	march_action action;
	struct wl_list link;
} XkbBinding;

typedef struct {
	struct river_pointer_binding_v1 *obj;
	Seat *seat;
	march_action action;
	struct wl_list link;
} PointerBinding;

enum SeatOp {
	SEAT_OP_NONE,
	SEAT_OP_MOVE,
	SEAT_OP_RESIZE,
};

struct Seat {
	struct river_seat_v1 *obj;
	bool new;
	bool removed;

	Window *focused;
	Window *hovered;
	Window *interacted;

	struct wl_list xkb_bindings;     // XkbBinding
	struct wl_list pointer_bindings; // PointerBinding
	march_action pending_action;

	enum SeatOp op;
	// For SEAT_OP_MOVE and SEAT_OP_RESIZE
	Window *op_window;
	int32_t op_start_x, op_start_y;
	int32_t op_dx, op_dy;
	bool op_release;
	// For SEAT_OP_RESIZE only
	int32_t op_start_width, op_start_height;
	uint32_t op_edges;

	struct wl_list link; // WindowManager.seats
};

void xkb_binding_create(Seat *seat, uint32_t mods, xkb_keysym_t keysym, march_action action);
void pointer_binding_create(Seat *seat, uint32_t mods, uint32_t button, march_action action);

void action_close(Seat *seat);
void action_spawn_foot(Seat *seat);
void action_focus_next(Seat *seat);
void action_move(Seat *seat);
void action_resize(Seat *seat);
void action_exit(Seat *seat);

#endif
