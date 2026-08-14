#include <lauxlib.h>
#include <linux/input-event-codes.h>
#include <lua.h>
#include <lualib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

#include "config.h"
#include "march.h"
#include "river-window-management-v1-client-protocol.h"

static bool modifier_from_name(const char *name, uint32_t *out)
{
	if (strcmp(name, "Super") == 0)
		*out = RIVER_SEAT_V1_MODIFIERS_MOD4;
	else if (strcmp(name, "Alt") == 0)
		*out = RIVER_SEAT_V1_MODIFIERS_MOD1;
	else if (strcmp(name, "Shift") == 0)
		*out = RIVER_SEAT_V1_MODIFIERS_SHIFT;
	else if (strcmp(name, "Ctrl") == 0)
		*out = RIVER_SEAT_V1_MODIFIERS_CTRL;
	else
		return false;

	return true;
}

static bool key_from_name(const char *name, xkb_keysym_t *out)
{
	xkb_keysym_t tmp = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
	if (tmp == XKB_KEY_NoSymbol) return false;
	*out = tmp;
	return true;
}

static bool button_from_name(const char *name, uint32_t *out)
{
	if (strcmp(name, "left") == 0)
		*out = BTN_LEFT;
	else if (strcmp(name, "right") == 0)
		*out = BTN_RIGHT;
	else
		return false;

	return true;
}

static bool action_from_name(const char *name, march_action *out)
{
	if (strcmp(name, "spawn_foot") == 0)
		*out = action_spawn_foot;
	else if (strcmp(name, "close") == 0)
		*out = action_close;
	else if (strcmp(name, "focus_next") == 0)
		*out = action_focus_next;
	else if (strcmp(name, "move") == 0)
		*out = action_move;
	else if (strcmp(name, "resize") == 0)
		*out = action_resize;
	else if (strcmp(name, "exit") == 0)
		*out = action_exit;
	else
		return false;

	return true;
}

static int l_bind(lua_State *L)
{
	const char *mods_tmp = luaL_checkstring(L, 1);
	uint32_t mods;
	if (!modifier_from_name(mods_tmp, &mods)) return luaL_error(L, "unknown modifier: %s", mods_tmp);

	const char *keysym_tmp = luaL_checkstring(L, 2);
	xkb_keysym_t keysym;
	if (!key_from_name(keysym_tmp, &keysym)) return luaL_error(L, "unknown key: %s", keysym_tmp);

	const char *action_tmp = luaL_checkstring(L, 3);
	march_action action;
	if (!action_from_name(action_tmp, &action)) return luaL_error(L, "unknown action: %s", action_tmp);

	ConfigKeyBinding *cb = calloc(1, sizeof(ConfigKeyBinding));
	cb->mods = mods;
	cb->keysym = keysym;
	cb->action = action;

	wl_list_insert(binds.config_key_bindings.prev, &cb->link);

	return 0;
}

static int l_bind_pointer(lua_State *L)
{
	const char *mods_tmp = luaL_checkstring(L, 1);
	uint32_t mods;
	if (!modifier_from_name(mods_tmp, &mods)) return luaL_error(L, "unknown modifier: %s", mods_tmp);

	const char *button_tmp = luaL_checkstring(L, 2);
	uint32_t button;
	if (!button_from_name(button_tmp, &button)) return luaL_error(L, "unknown button: %s", button_tmp);

	const char *action_tmp = luaL_checkstring(L, 3);
	march_action action;
	if (!action_from_name(action_tmp, &action)) return luaL_error(L, "unknown action: %s", action_tmp);

	ConfigButtonBinding *cb = calloc(1, sizeof(ConfigButtonBinding));
	cb->mods = mods;
	cb->button = button;
	cb->action = action;

	wl_list_insert(binds.config_button_bindings.prev, &cb->link);

	return 0;
}

void config_init(void)
{
	wl_list_init(&binds.config_key_bindings);
	wl_list_init(&binds.config_button_bindings);

	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	lua_register(L, "bind", l_bind);
	lua_register(L, "bind_pointer", l_bind_pointer);

	if (luaL_dofile(L, "init.lua") != LUA_OK) {
		fprintf(stderr, "config error: %s\n)", lua_tostring(L, -1));
		lua_close(L);
		exit(EXIT_FAILURE);
	}

	lua_close(L);
}
