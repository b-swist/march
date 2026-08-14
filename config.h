#ifndef MARCH_CONFIG_H
#define MARCH_CONFIG_H

#include "march.h"
#include <xkbcommon/xkbcommon.h>

typedef struct {
	uint32_t mods;
	xkb_keysym_t keysym;
	march_action action;
	struct wl_list link;
} ConfigKeyBinding;

typedef struct {
	uint32_t mods;
	uint32_t button;
	march_action action;
	struct wl_list link;
} ConfigButtonBinding;

struct Bindings {
	struct wl_list config_key_bindings;
	struct wl_list config_button_bindings;
};
extern struct Bindings binds;

void config_init(void);

#endif
