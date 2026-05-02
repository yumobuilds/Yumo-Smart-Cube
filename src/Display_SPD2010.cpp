#include "Display_SPD2010.h"

#include <stdlib.h>
#include <string.h>
#include "esp_intr_alloc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_spd2010.h"

#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_ops.h"

uint8_t LCD_Backlight = 60;

void SPD2010_Reset(){
  Set_EXIO(EXIO_PIN2,Low);
  delay(50);
  Set_EXIO(EXIO_PIN2,High);
  delay(50);
}
void LCD_Init() {        
  SPD2010_Init();
  Touch_Init();
}

static void test_draw_bitmap(esp_lcd_panel_handle_t panel_handle)
{
  uint16_t row_line = ((EXAMPLE_LCD_WIDTH / EXAMPLE_LCD_COLOR_BITS) << 1) >> 1;
  uint8_t byte_per_pixel = EXAMPLE_LCD_COLOR_BITS / 8;
  uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * EXAMPLE_LCD_HEIGHT * byte_per_pixel, MALLOC_CAP_DMA);
  for (int j = 0; j < EXAMPLE_LCD_COLOR_BITS; j++) {
    for (int i = 0; i < row_line * EXAMPLE_LCD_HEIGHT; i++) {
      for (int k = 0; k < byte_per_pixel; k++) {
        color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), EXAMPLE_LCD_COLOR_BITS) >> (k * 8)) & 0xff;
      }
    }
    esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, EXAMPLE_LCD_HEIGHT, (j + 1) * row_line, color);
  }
  free(color);
}

bool QSPI_Init(void){
  static const spi_bus_config_t host_config = {            
    .data0_io_num = ESP_PANEL_LCD_SPI_IO_DATA0,                    
    .data1_io_num = ESP_PANEL_LCD_SPI_IO_DATA1,                   
    .sclk_io_num = ESP_PANEL_LCD_SPI_IO_SCK,                   
    .data2_io_num = ESP_PANEL_LCD_SPI_IO_DATA2,                    
    .data3_io_num = ESP_PANEL_LCD_SPI_IO_DATA3,                    
    .data4_io_num = -1,                       
    .data5_io_num = -1,                      
    .data6_io_num = -1,                       
    .data7_io_num = -1,                      
    .max_transfer_sz = ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE, 
    .flags = SPICOMMON_BUSFLAG_MASTER,       
    .intr_flags = 0,                            
  };
  if(spi_bus_initialize(SPI2_HOST, &host_config, SPI_DMA_CH_AUTO) != ESP_OK){
    printf("The SPI initialization failed.\r\n");
    return false;
  }
  printf("The SPI initialization succeeded.\r\n");
  return true;
}

esp_lcd_panel_handle_t panel_handle = NULL;
bool SPD2010_Init() {
  SPD2010_Reset();
  pinMode(ESP_PANEL_LCD_SPI_IO_TE, OUTPUT);
  if(!QSPI_Init()){
    printf("SPD2010 Failed to be initialized\r\n");
  }
    
  const esp_lcd_panel_io_spi_config_t io_config ={
    .cs_gpio_num = ESP_PANEL_LCD_SPI_IO_CS,               
    .dc_gpio_num = -1,                  
    .spi_mode = ESP_PANEL_LCD_SPI_MODE,                      
    .pclk_hz = ESP_PANEL_LCD_SPI_CLK_HZ,     
    .trans_queue_depth = ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ,            
    .on_color_trans_done = NULL,                              // 传输完成时调用该函数  
    .user_ctx = NULL,                   
    .lcd_cmd_bits = ESP_PANEL_LCD_SPI_CMD_BITS,                 
    .lcd_param_bits = ESP_PANEL_LCD_SPI_PARAM_BITS,                
    .flags = {                          
      .dc_low_on_data = 0,            
      .octal_mode = 0,                
      .quad_mode = 1,                 
      .sio_mode = 0,                  
      .lsb_first = 0,                 
      .cs_high_active = 0,            
    },                                  
  };
  esp_lcd_panel_io_handle_t io_handle = NULL;
  if(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ESP_PANEL_HOST_SPI_ID_DEFAULT, &io_config, &io_handle) != ESP_OK){
    printf("Failed to set LCD communication parameters -- SPI\r\n");
    return false;
  }else{
    printf("LCD communication parameters are set successfully -- SPI\r\n");
  }

  printf("Install LCD driver of spd2010\r\n");
  spd2010_vendor_config_t vendor_config={  
    .flags = {
      .use_qspi_interface = 1,
    },
  };
  esp_lcd_panel_dev_config_t panel_config={
    .reset_gpio_num = EXAMPLE_LCD_PIN_NUM_RST,                                     
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,                   
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,                       
    .bits_per_pixel = EXAMPLE_LCD_COLOR_BITS,                                 
    // .flags = {                                                    
    //   .reset_active_high = 0,                                   
    // },                                                            
    .vendor_config = (void *) &vendor_config,                                  
  };
  esp_lcd_new_panel_spd2010(io_handle, &panel_config, &panel_handle);

  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  // esp_lcd_panel_invert_color(panel_handle,false);

  esp_lcd_panel_disp_on_off(panel_handle, true);
  test_draw_bitmap(panel_handle);
  printf("spd2010 LCD OK\r\n");
  return true;
}

void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color)
{ 
  uint32_t size = (Xend - Xstart +1 ) * (Yend - Ystart + 1);
  for (size_t i = 0; i < size; i++) {
    color[i] = (((color[i] >> 8) & 0xFF) | ((color[i] << 8) & 0xFF00));
  }
  
  Xend = Xend + 1;      // esp_lcd_panel_draw_bitmap: x_end End index on x-axis (x_end not included)
  Yend = Yend + 1;      // esp_lcd_panel_draw_bitmap: y_end End index on y-axis (y_end not included)
  if (Xend > EXAMPLE_LCD_WIDTH)
    Xend = EXAMPLE_LCD_WIDTH;
  if (Yend > EXAMPLE_LCD_HEIGHT)
    Yend = EXAMPLE_LCD_HEIGHT;
    
  // printf("Xstart = %d    Ystart = %d    Xend = %d    Yend = %d \r\n",Xstart, Ystart, Xend, Yend);
  esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, color);                     // x_end End index on x-axis (x_end not included)
}



// backlight
void Backlight_Init()
{
  ledcAttach(LCD_Backlight_PIN, Frequency, Resolution);    // 设置通道
  ledcWrite(LCD_Backlight_PIN, Dutyfactor);               // 设置亮度    
  Set_Backlight(LCD_Backlight);      //0~100  
}

void Set_Backlight(uint8_t Light)                        //
{
  if(Light > Backlight_MAX || Light < 0)
    printf("Set Backlight parameters in the range of 0 to 100 \r\n");
  else{
    uint32_t Backlight = Light*10;
    if(Backlight == 1000)
      Backlight = 1024;
    ledcWrite(LCD_Backlight_PIN, Backlight);
  }
}
