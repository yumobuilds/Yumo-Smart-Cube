#pragma once

#include <Arduino.h>
#include "I2C_Driver.h"
#include "TCA9554PWR.h"

#define SPD2010_ADDR                    (0x53)
#define EXAMPLE_PIN_NUM_TOUCH_INT       (4)
#define EXAMPLE_PIN_NUM_TOUCH_RST       (-1)


#define CONFIG_ESP_LCD_TOUCH_MAX_POINTS     (5)     
/****************HYN_REG_MUT_DEBUG_INFO_MODE address start***********/
#define SPD2010_REG_Status         (NULL)

extern struct SPD2010_Touch touch_data;
typedef struct {
  uint8_t id;
  uint16_t x;
  uint16_t y;
  uint8_t weight;
} tp_report_t;
struct SPD2010_Touch{
  tp_report_t rpt[10];
  uint8_t touch_num;     // Number of touch points
  uint8_t pack_code;
  uint8_t down;
  uint8_t up;
  uint8_t gesture;
  uint16_t down_x;
  uint16_t down_y;
  uint16_t up_x;
  uint16_t up_y;
};

typedef struct {
  uint8_t none0;
  uint8_t none1;
  uint8_t none2;
  uint8_t cpu_run;
  uint8_t tint_low;
  uint8_t tic_in_cpu;
  uint8_t tic_in_bios;
  uint8_t tic_busy;
} tp_status_high_t;

typedef struct {
  uint8_t pt_exist;
  uint8_t gesture;
  uint8_t key;
  uint8_t aux;
  uint8_t keep;
  uint8_t raw_or_pt;
  uint8_t none6;
  uint8_t none7;
} tp_status_low_t;

typedef struct {
  tp_status_low_t status_low;
  tp_status_high_t status_high;
  uint16_t read_len;
} tp_status_t;
typedef struct {
  uint8_t status;
  uint16_t next_packet_len;
} tp_hdp_status_t;


bool I2C_Read_Touch(uint8_t Driver_addr, uint16_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
bool I2C_Write_Touch(uint8_t Driver_addr, uint16_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);

uint8_t Touch_Init();
void Touch_Loop(void);
uint8_t SPD2010_Touch_Reset(void);
uint16_t SPD2010_Read_cfg(void); 
void Touch_Read_Data(void);
bool Touch_Get_xy(uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
void example_touchpad_read(void);
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPD2010 函数
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
esp_err_t write_tp_point_mode_cmd();
esp_err_t write_tp_start_cmd();
esp_err_t write_tp_cpu_start_cmd();
esp_err_t write_tp_clear_int_cmd();
esp_err_t read_tp_status_length(tp_status_t *tp_status);
esp_err_t read_tp_hdp(tp_status_t *tp_status, SPD2010_Touch *touch);
esp_err_t read_tp_hdp_status(tp_hdp_status_t *tp_hdp_status);
esp_err_t Read_HDP_REMAIN_DATA(tp_hdp_status_t *tp_hdp_status);
esp_err_t read_fw_version();
esp_err_t tp_read_data(SPD2010_Touch *touch);

