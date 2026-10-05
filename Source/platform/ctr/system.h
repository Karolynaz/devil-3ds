#pragma once

void ctr_lcd_backlight_on();
void ctr_lcd_backlight_off();

bool ctr_check_dsp();

void ctr_sys_init();

// Stop application workers while SDK services and their stacks are alive.
void ctr_sys_shutdown();
