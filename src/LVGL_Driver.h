#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "Display_SPD2010.h"
#include "Touch_SPD2010.h"

#define LCD_WIDTH     EXAMPLE_LCD_WIDTH
#define LCD_HEIGHT    EXAMPLE_LCD_HEIGHT
#define LVGL_BUF_LEN  (LCD_WIDTH * LCD_HEIGHT / 10)

void Lvgl_Init(void);
void Lvgl_Loop(void);
