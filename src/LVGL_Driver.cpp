#include "LVGL_Driver.h"
#include <Arduino.h>

// Allocate draw buffers
static lv_color_t *buf1;
static lv_color_t *buf2;

// The display handle
static lv_display_t * disp;

void Lvgl_print(const char * buf)
{
    // Serial.printf(buf);
}

// Display flushing for LVGL 9
static lv_color_t *rotated_buf = NULL;
void Lvgl_Display_LCD(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
  lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (rot == LV_DISPLAY_ROTATION_0) {
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)px_map);
  } else {
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;
    uint16_t *src = (uint16_t *)px_map;
    uint16_t *dst = (uint16_t *)rotated_buf;
    uint16_t new_x1, new_y1, new_x2, new_y2;
    if (rot == LV_DISPLAY_ROTATION_90) {
      new_x1 = LCD_HEIGHT - 1 - area->y2;
      new_y1 = area->x1;
      new_x2 = LCD_HEIGHT - 1 - area->y1;
      new_y2 = area->x2;
      for(uint16_t y = 0; y < h; y++) {
        for(uint16_t x = 0; x < w; x++) dst[x * h + (h - 1 - y)] = src[y * w + x];
      }
    } else if (rot == LV_DISPLAY_ROTATION_180) {
      new_x1 = LCD_WIDTH - 1 - area->x2;
      new_y1 = LCD_HEIGHT - 1 - area->y2;
      new_x2 = LCD_WIDTH - 1 - area->x1;
      new_y2 = LCD_HEIGHT - 1 - area->y1;
      for(uint16_t y = 0; y < h; y++) {
        for(uint16_t x = 0; x < w; x++) dst[(h - 1 - y) * w + (w - 1 - x)] = src[y * w + x];
      }
    } else if (rot == LV_DISPLAY_ROTATION_270) {
      new_x1 = area->y1;
      new_y1 = LCD_WIDTH - 1 - area->x2;
      new_x2 = area->y2;
      new_y2 = LCD_WIDTH - 1 - area->x1;
      for(uint16_t y = 0; y < h; y++) {
        for(uint16_t x = 0; x < w; x++) dst[(w - 1 - x) * h + y] = src[y * w + x];
      }
    }
    LCD_addWindow(new_x1, new_y1, new_x2, new_y2, dst);
  }
  lv_display_flush_ready(disp);
}

// Hardware needs X coordinates to be rounded to the nearest 4 bytes
static void Lvgl_port_rounder_callback(lv_event_t * e)
{
  lv_area_t * area = (lv_area_t*)lv_event_get_param(e);
  lv_display_rotation_t rot = lv_display_get_rotation(disp);

  if (rot == LV_DISPLAY_ROTATION_0 || rot == LV_DISPLAY_ROTATION_180) {
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    // round the start of coordinate down to the nearest 4M number
    area->x1 = (x1 >> 2) << 2;

    // round the end of coordinate up to the nearest 4N+3 number
    area->x2 = ((x2 >> 2) << 2) + 3;
  } else {
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // For 90 and 270 deg, the logical Y axis becomes the physical X axis
    area->y1 = (y1 >> 2) << 2;
    area->y2 = ((y2 >> 2) << 2) + 3;
  }
}

// Read the touchpad for LVGL 9
void Lvgl_Touchpad_Read(lv_indev_t * indev, lv_indev_data_t * data)
{
  bool tp_pressed = false;
  uint16_t tp_x = 0;
  uint16_t tp_y = 0;
  uint8_t tp_cnt = 0;
  
  tp_pressed = Touch_Get_xy(&tp_x, &tp_y, NULL, &tp_cnt, 5); // 5 max points
  
  if (tp_pressed && (tp_cnt > 0)) {
    data->point.x = tp_x;
    data->point.y = tp_y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Setup tick for LVGL using ESP timer
void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(2);
}

void Lvgl_Init(void)
{
  LCD_Init(); // This comes from Display_SPD2010 to init QSPI & Touch physically

  lv_init();

  // Create the display in LVGL 9
  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  // Allocate DMA capable memory for LVGL buffers
  size_t buf_size = LCD_WIDTH * LCD_HEIGHT / 20 * sizeof(lv_color_t);
  size_t alloc_size = buf_size + (LCD_WIDTH * 10 * sizeof(lv_color_t)); // 10% safety pad for rounders
  buf1 = (lv_color_t*) heap_caps_malloc(alloc_size, MALLOC_CAP_DMA);
  buf2 = (lv_color_t*) heap_caps_malloc(alloc_size, MALLOC_CAP_DMA);
  rotated_buf = (lv_color_t*) heap_caps_malloc(alloc_size, MALLOC_CAP_DMA);
  
  lv_display_set_buffers(disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, Lvgl_Display_LCD);
  lv_display_add_event_cb(disp, Lvgl_port_rounder_callback, LV_EVENT_INVALIDATE_AREA, NULL);

  // Create the touch input device in LVGL 9
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, Lvgl_Touchpad_Read);

  // Create timer to give tick to LVGL
  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, 2 * 1000); // 2ms in us
}

void Lvgl_Loop(void)
{
  // Do nothing. LVGL loop is handled in main.cpp
}
