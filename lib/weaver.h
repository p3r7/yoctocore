#pragma once

extern void w_init(void);

//-------------------------
//---- c -> lua glue

//--- gpio handler
extern void w_handle_key(const int n, const int val);
extern void w_handle_knob(const int n, const int delta);
