// standard
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// lua
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

static lua_State *L;


////////////////////////////////
//// extern function definitions

void w_init(void) {
    // name global extern table
    lua_setglobal(lvm, "_yocto");
}

//--------------------------------------------------
//--- define lua handlers for system callbacks

// gpio handler
void w_handle_key(const int n, const int val) {
    lua_getglobal(L, "_yocto");
    lua_getfield(L, -1, "key");
    lua_remove(L, -2);
    lua_pushinteger(L, n);
    lua_pushinteger(L, val);
    l_report(L, l_docall(L, 2, 0));
}

// gpio handler
void w_handle_knob(const int n, const int val) {
    lua_getglobal(L, "_yocto");
    lua_getfield(L, -1, "enc");
    lua_remove(L, -2);
    lua_pushinteger(L, n);
    lua_pushinteger(L, val);
    l_report(L, l_docall(L, 2, 0));
}

// #pragma GCC diagnostic pop
