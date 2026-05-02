/**
 * ==============================================================================
 * PROJECT: YUMO Cube Firmware
 * AUTHOR: Yumo Builds
 * DESCRIPTION: Main application file for the YUMO Cube device. Handles LVGL UI,
 *              FreeRTOS background tasks (WiFi, Weather, Jokes), hardware
 *              initialization, QMI8658 IMU integration, and power latching.
 * 
 * HARDWARE:
 * - Board: ESP32-S3 (16MB Flash, 8MB PSRAM)
 * - Display: SPI/8080 or RGB interface with Touch
 * - IMU: QMI8658 6-axis (SDA: 11, SCL: 10)
 * - Power Latch/Mosfet EN: IO7
 * - Push Button / Power Off: IO6
 * ==============================================================================
 */

#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"
#include <SensorQMI8658.hpp>
#include "LVGL_Driver.h"
#include <WiFiManager.h>

WiFiManager wm;
volatile bool wifi_init_done = false;
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <SD_MMC.h>
#include <vector>

#include <JPEGDEC.h>
#define SD_CLK_PIN      14
#define SD_CMD_PIN      17
#define SD_D0_PIN       16

// ============================================================
// BATTERY CONFIGURATION (GPIO 8 = ADC1_CH7, 3:1 voltage divider)
// ============================================================
#define BAT_ADC_PIN     8

int read_battery_percent() {
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
    int raw_mv = analogReadMilliVolts(BAT_ADC_PIN);
    float vbat = (float)raw_mv * 3.0f / 1000.0f / 0.990476f;
    return constrain((int)((vbat - 3.0f) / 1.2f * 100.0f), 0, 100);
}
// ============================================================

// ============================================================
// LED CONFIGURATION (GPIO 12 = Blue, GPIO 13 = Purple)
// ============================================================
#define LED_BLUE_PIN    12
#define LED_PURPLE_PIN  13
#define LED_PWM_FREQ    5000
#define LED_PWM_RES     8        // 8-bit: 0-255
#define LED_MAX_DUTY    179      // 70% of 255

enum LedMode { LED_OFF, LED_BREATH, LED_HEARTWAVE, LED_POLICE };
static LedMode       led_mode     = LED_OFF;
static unsigned long led_start_ms = 0;

void leds_init() {
    ledcAttach(LED_BLUE_PIN,   LED_PWM_FREQ, LED_PWM_RES);
    ledcAttach(LED_PURPLE_PIN, LED_PWM_FREQ, LED_PWM_RES);
    ledcWrite(LED_BLUE_PIN,   0);
    ledcWrite(LED_PURPLE_PIN, 0);
}

void leds_off() {
    led_mode = LED_OFF;
    ledcWrite(LED_BLUE_PIN,   0);
    ledcWrite(LED_PURPLE_PIN, 0);
}

void leds_start_breath() {
    led_mode     = LED_BREATH;
    led_start_ms = millis();
}

void leds_start_heartwave() {
    led_mode     = LED_HEARTWAVE;
    led_start_ms = millis();
}

void leds_start_police() {
    led_mode     = LED_POLICE;
    led_start_ms = millis();
}

void leds_tick() {
    if (led_mode == LED_OFF) return;
    unsigned long elapsed = millis() - led_start_ms;

    if (led_mode == LED_BREATH) {
        // Single 2-second breath: 0 → 70% in 1 s, 70% → 0 in 1 s
        if (elapsed >= 2000) { leds_off(); return; }
        int duty = (elapsed < 1000)
            ? (int)(elapsed * LED_MAX_DUTY / 1000)
            : (int)((2000 - elapsed) * LED_MAX_DUTY / 1000);
        ledcWrite(LED_BLUE_PIN,   duty);
        ledcWrite(LED_PURPLE_PIN, duty);
        return;
    }

    if (led_mode == LED_HEARTWAVE) {
        // Continuous heartwave: blue rises while purple falls, then swap. Each half = 1 s.
        unsigned long phase = elapsed % 2000;
        int blue_duty, purple_duty;
        if (phase < 1000) {
            blue_duty   = (int)(phase * LED_MAX_DUTY / 1000);
            purple_duty = LED_MAX_DUTY - blue_duty;
        } else {
            blue_duty   = LED_MAX_DUTY - (int)((phase - 1000) * LED_MAX_DUTY / 1000);
            purple_duty = LED_MAX_DUTY - blue_duty;
        }
        ledcWrite(LED_BLUE_PIN,   blue_duty);
        ledcWrite(LED_PURPLE_PIN, purple_duty);
    }

    if (led_mode == LED_POLICE) {
        // Police flash: 2x blue then 2x purple, each flash 150ms on / 150ms off
        // One full cycle = 8 slots x 150ms = 1200ms
        unsigned long slot = (elapsed % 1200) / 150; // 0-7
        int blue_duty   = 0;
        int purple_duty = 0;
        if      (slot == 0 || slot == 2) blue_duty   = LED_MAX_DUTY; // blue flash 1 & 2
        else if (slot == 4 || slot == 6) purple_duty = LED_MAX_DUTY; // purple flash 1 & 2
        ledcWrite(LED_BLUE_PIN,   blue_duty);
        ledcWrite(LED_PURPLE_PIN, purple_duty);
    }
}
// ============================================================

// ============================================================
// SLEEP MODE CONFIGURATION
// ============================================================
static bool is_sleeping = false;
static unsigned long last_activity_ms = 0;
static float sleep_ax = 0, sleep_ay = 0, sleep_az = 0;
#define SLEEP_TIMEOUT_MS  150000UL  // 2.5 minutes
#define WAKE_THRESHOLD    0.3f      // 0.3g movement triggers wake
// ============================================================

JPEGDEC jpeg;

static std::vector<String> gallery_photos;
static int current_photo_index = 0;
static lv_timer_t* gallery_timer = NULL;
static lv_obj_t* gallery_debug_label = NULL;

// Buffer for our decoded JPG
static uint8_t * decoded_img_buf = NULL;
int current_jpeg_w = 0;
int current_jpeg_h = 0;
static lv_image_dsc_t custom_img_dsc;

File current_jpeg_file;
void * myOpen(const char *filename, int32_t *size) {
    current_jpeg_file = SD_MMC.open(filename, FILE_READ);
    if (!current_jpeg_file) {
        Serial.print("SD_MMC.open FAILED for: ");
        Serial.println(filename);
        return NULL;
    }
    *size = current_jpeg_file.size();
    return (void *)1; // dummy handle
}

void myClose(void *handle) {
    if (current_jpeg_file) current_jpeg_file.close();
}

int32_t myRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    if (!current_jpeg_file) return 0;
    int32_t r = current_jpeg_file.read(pBuf, iLen);
    if (r > 0) {
        pFile->iPos += r;
    }
    return r;
}

int32_t mySeek(JPEGFILE *pFile, int32_t iPosition) {
    if (!current_jpeg_file) return 0;
    current_jpeg_file.seek(iPosition);
    pFile->iPos = current_jpeg_file.position();
    return pFile->iPos;
}

int MCU_Draw(JPEGDRAW *pDraw) {
    if (!decoded_img_buf) return 0; // abort if no memory

    int total_width = current_jpeg_w;
    int total_height = current_jpeg_h;
    int y = pDraw->y;
    int x = pDraw->x;
    
    // Determine how many rows and columns are ACTUALLY valid for this block
    // (JPEGs are encoded in 8x8 or 16x16 MCU blocks, which may overshoot the image boundaries!)
    int valid_cols = pDraw->iWidth;
    if (x + valid_cols > total_width) valid_cols = total_width - x;
    
    int valid_rows = pDraw->iHeight;
    if (y + valid_rows > total_height) valid_rows = total_height - y;

    // Copy row by row safely!
    for (int i = 0; i < valid_rows; i++) {
        int dest_idx = ((y + i) * total_width) + x;
        // pDraw->pPixels is what the decoder hands us
        memcpy(&decoded_img_buf[dest_idx * 2], &pDraw->pPixels[i * pDraw->iWidth], valid_cols * 2);
    }
    
    return 1; // 1 = continue decoding
}





#include <time.h>
#include "components/ui_comp_titlegroup.h"
// Weather Icon references from SquareLine UI
extern lv_obj_t *ui_sun;
extern lv_obj_t *ui_clouds;


// --- WEATHER & TIME SETTINGS ---
const char* weather_api_key = "YOUR_OPENWEATHERMAP_API_KEY"; // Get yours free at https://openweathermap.org/api

String current_city = "Loading...";
long gmtOffset_sec = 0; // Will be auto-discovered via IP API
const int daylightOffset_sec = 0; // IP-API offset already includes DST!

unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 10 * 60 * 1000; // Update weather every 10 minutes


#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct WeatherData {
    float temp;
    float temp_min;
    float temp_max;
    int humidity;
    float wind_speed;
    char desc[32];
    char icon_code[8];
    char city[64];
    long gmt_offset;
};

QueueHandle_t weatherQueue = NULL;

unsigned long lastTimeUpdate = 0;
const unsigned long timeUpdateInterval = 1000; // Update time on screen every 1 second
// --------------------------------


SensorQMI8658 qmi;
// Display handle is managed by LVGL_Init() inside LVGL_Driver.cpp

// Function to map the QMI8658 gravity vector to one of 6 faces
int get_cube_face() {
    float x, y, z;
    if (qmi.getAccelerometer(x, y, z)) {
        // Determine which axis has the strongest gravity pull (approx 9.8 or 1g)
        // Mapped based on user request: 1 -> 2 -> 3 -> 4 rotation, and swapped 5/6
        // Make Z-axis (Function 5 & 6) 50% less sensitive by requiring a stronger pull
        if (abs(z) > abs(x) * 1.5 && abs(z) > abs(y) * 1.5) {
            // Top / Bottom
            return (z > 0) ? 6 : 5;
        } else if (abs(x) > abs(y)) {
            // Front / Back
            return (x > 0) ? 1 : 3;
        } else {
            // Right / Left (rotating 90 degrees around Z axis from Front)
            return (y > 0) ? 2 : 4;
        }
    }
    return 1; // Default fallback rotation
}

// --- REAL TIME UPDATE ---




// Safe Background Task for HTTP (Runs on Core 0)
extern int current_face;
void weatherBgTask(void* pvParameters) {
    char current_city_str[64] = "London";
    long current_gmt_offset = 0;

    Serial.println("weatherBgTask started!");

    // Wait until we have a real IP and WiFi is actually connected
    while (WiFi.status() != WL_CONNECTED || WiFi.localIP().toString() == "0.0.0.0" || WiFi.gatewayIP().toString() == "0.0.0.0") {
        Serial.println("Waiting for valid IP...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    WiFi.mode(WIFI_STA); // Drop AP mode to prevent router kicking ESP32 off!
    Serial.print("Got IP: ");
    Serial.println(WiFi.localIP());
    


    // Give router time to set up routing and avoid power spikes during boot animations
    vTaskDelay(pdMS_TO_TICKS(2000));

    while(true) {
        // Since Weather is only displayed on Face 1, DO NOT fetch or fight the router if the user is on the Gallery (Face 2)!
        // This completely prevents the SD card and the Wi-Fi radio from crashing each other.
        if (current_face != 1) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if(WiFi.status() == WL_CONNECTED) {
            
            // 1. IP Location
            WiFiClient clientLoc;
            HTTPClient httpLoc;
            httpLoc.setTimeout(2000);
            httpLoc.begin(clientLoc, "http://ip-api.com/json/?fields=city,offset");
            
            int locCode = httpLoc.GET();
            if (locCode == 200) {
                JsonDocument doc;
                deserializeJson(doc, httpLoc.getString());
                if(doc["offset"].is<long>()) current_gmt_offset = doc["offset"].as<long>();
                if(doc["city"].is<const char*>() && doc["city"].is<const char*>()) {
                    strncpy(current_city_str, doc["city"].as<const char*>(), sizeof(current_city_str) - 1);
                }
                configTime(current_gmt_offset, 0, "pool.ntp.org");
                Serial.printf("Configured time with offset %ld and pool.ntp.org\n", current_gmt_offset);
            } else {
                Serial.printf("IP location failed with code %d\n", locCode);
            }
            httpLoc.end();

            // 2. Weather
            WiFiClient clientWeather;
            HTTPClient httpWeather;
            httpWeather.setTimeout(2000);
            String url = String("http://api.openweathermap.org/data/2.5/weather?q=") + current_city_str + "&units=metric&appid=" + weather_api_key;
            httpWeather.begin(clientWeather, url);
            int wCode = httpWeather.GET();
            if (wCode == 200) {
                JsonDocument docW;
                deserializeJson(docW, httpWeather.getString());
                
                WeatherData wd;
                wd.temp = docW["main"]["temp"].as<float>();
                wd.temp_min = docW["main"]["temp_min"].as<float>();
                wd.temp_max = docW["main"]["temp_max"].as<float>();
                wd.humidity = docW["main"]["humidity"].as<int>();
                wd.wind_speed = docW["wind"]["speed"].as<float>() * 3.6;
                
                if (docW["weather"][0]["main"].is<const char*>()) {
                    strncpy(wd.desc, docW["weather"][0]["main"].as<const char*>(), sizeof(wd.desc) - 1);
                } else {
                    strcpy(wd.desc, "Unknown");
                }
                
                if (docW["weather"][0]["icon"].is<const char*>()) {
                    strncpy(wd.icon_code, docW["weather"][0]["icon"].as<const char*>(), sizeof(wd.icon_code) - 1);
                } else {
                    strcpy(wd.icon_code, "01d");
                }
                
                strncpy(wd.city, current_city_str, sizeof(wd.city) - 1);
                wd.gmt_offset = current_gmt_offset;

                // Send safely by-value to Core 1, overwriting any stale data
                if (weatherQueue != NULL) {
                    xQueueOverwrite(weatherQueue, &wd);
                }
            }
            httpWeather.end();
            
            // Check if Wi-Fi stack says it is connected, but sockets are failing
            if (locCode < 0 || wCode < 0) {
                Serial.println("Network request failed. Wait 15s to allow DNS to settle...");
                vTaskDelay(pdMS_TO_TICKS(15000)); // Give 15s break for the router to figure out DNS!
                continue;
            } else if (wCode == 200) {
                vTaskDelay(pdMS_TO_TICKS(600000)); // Success! Sleep 10 minutes
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000)); // Failed! Sleep 10 seconds
            }
        } else {
            // Not connected yet, drop state and reconnect safely
            Serial.printf("WiFi Not Connected! Status: %d\n", WiFi.status());
            WiFi.disconnect(true, false);
            vTaskDelay(pdMS_TO_TICKS(2000));
            WiFi.mode(WIFI_STA);
            WiFi.begin();
            
            // Block this task from spamming reconnects while it tries to negotiate with the router
            int wait_sec = 0;
            while (WiFi.status() != WL_CONNECTED && wait_sec < 15) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                wait_sec++;
                Serial.print(".");
            }
            Serial.println();
        }
    }
}

// --------------------------------

// Map the face (1-6) to a physical screen rotation angle
lv_display_rotation_t face_to_rotation(int face) {
    switch (face) {
        case 1: return LV_DISPLAY_ROTATION_0;   // Weather Front
        case 2: return LV_DISPLAY_ROTATION_90;  // Gallery Right
        case 3: return LV_DISPLAY_ROTATION_180; // Jokes Back
        case 4: return LV_DISPLAY_ROTATION_270; // Activity Left
        case 5: return LV_DISPLAY_ROTATION_0;   // Top/Bottom (Adjust if it's sideways!)
        case 6: return LV_DISPLAY_ROTATION_0;   // Top/Bottom (Adjust if it's sideways!)
        default: return LV_DISPLAY_ROTATION_0;
    }
}


// --- GAME VARIABLES ---
lv_obj_t* ui_game_ball = NULL;
lv_obj_t* ui_game_dot = NULL;
lv_obj_t* ui_game_score = NULL;
float game_ball_x = 0;
float game_ball_y = 0;
float game_ball_vel_x = 0;
float game_ball_vel_y = 0;
int game_score = 0;
int game_target_x = 30;
int game_target_y = -50;

int current_face = 1;
bool is_startup_phase = true;

// Function to load the correct final page (app) based on the face
void load_final_page(int face) {
    last_activity_ms = millis();
    switch (face) {
        case 1: lv_screen_load(ui_weather_time); break;
        case 2: lv_screen_load(ui_Gallery_pictures); break;
        case 3: lv_screen_load(ui_joks); break;
        case 4: ui_function_5_screen_reset(); lv_screen_load(ui_function_5); break;
        case 5: lv_screen_load(ui_activity_game); break;
        case 6: lv_screen_load(ui_function_6); break;
    }
    if (face == 6) { leds_start_heartwave(); Set_Backlight(0); } else { leds_off(); Set_Backlight(60); }
}

// Timer to transition from loading screen to actual app
lv_timer_t * app_transition_timer = NULL;

void app_transition_cb(lv_timer_t * timer) {
    int face = (int)(uintptr_t)lv_timer_get_user_data(timer);
    load_final_page(face);
    lv_timer_delete(timer);
    app_transition_timer = NULL;
}

// Function to load the correct loading page based on the face
void load_loading_page(int face) {
    last_activity_ms = millis();
    // Delete any existing app timer if we rotate while loading
    if(app_transition_timer != NULL) {
        lv_timer_delete(app_transition_timer);
        app_transition_timer = NULL;
    }

    switch (face) {
        case 1: lv_screen_load(ui_functions_loading_page_1); break;
        case 2: lv_screen_load(ui_functions_loading_page_2); break;
        case 3: lv_screen_load(ui_functions_loading_page_3); break;
        case 4: lv_screen_load(ui_functions_loading_page_4); break;
        case 5: lv_screen_load(ui_functions_loading_page_5); break;
        case 6: lv_screen_load(ui_functions_loading_page_6); break;
    }

    // Start a 2-second timer to switch to the final app page
    app_transition_timer = lv_timer_create(app_transition_cb, 2000, (void*)(uintptr_t)face);
    if (face >= 1 && face <= 5) { Set_Backlight(60); leds_start_breath(); } else { leds_off(); }
}

// Timer callback to trigger the loading page shortly after the startup page
void transition_timer_cb(lv_timer_t * timer) {
    is_startup_phase = false; // Mark startup as finished
    last_activity_ms = millis(); // Start sleep timer from end of startup
    load_loading_page(current_face); // Load the page for whatever face is currently up

    // Delete the timer so it only runs once
    lv_timer_delete(timer);
}




// --- JPEGDEC GALLERY LOGIC ---
void init_sd_card() {
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD MOUNT FAILED!");
    } else {
        Serial.printf("SD card mounted! Size: %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));
    }
}

void scan_gallery_photos() {
    gallery_photos.clear();
    // Scan typical Yumo cube picture folders
    String folders[] = {"/pictures", "/pictures for display round 412x 412"};
    for (int i=0; i<2; i++) {
        File folder = SD_MMC.open(folders[i]);
        if (folder && folder.isDirectory()) {
            File f = folder.openNextFile();
            while(f) {
                if(!f.isDirectory()) {
                    String n = f.name();
                    String l = n; l.toLowerCase();
                    if ((l.endsWith(".jpg") || l.endsWith(".jpeg")) && !n.startsWith("._")) { // Only JPEGs
                        gallery_photos.push_back(folders[i] + "/" + n);
                        Serial.println("Found JPEG: " + gallery_photos.back());
                    }
                }
                f = folder.openNextFile();
            }
            folder.close();
        }
    }
}


void ensure_debug_label() {
    // Label disabled to remove "Gallery Started..." text
    /*
    extern lv_obj_t *ui_Gallery_pictures;
    if (ui_Gallery_pictures && !gallery_debug_label) {
        gallery_debug_label = lv_label_create(ui_Gallery_pictures);
        lv_obj_align(gallery_debug_label, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_bg_color(gallery_debug_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(gallery_debug_label, 150, 0); // slightly transparent black
        lv_obj_set_style_text_color(gallery_debug_label, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(gallery_debug_label, "Gallery Started...");
    }
    */
}

void load_and_decode_jpeg(String filepath) {
    ensure_debug_label();
    // disabled label
    Serial.printf("JPEGDEC Opening: %s\n", filepath.c_str());

    
    current_jpeg_file = SD_MMC.open(filepath.c_str(), FILE_READ);
    if (!current_jpeg_file) {
        Serial.println("COULD NOT OPEN FILE DIRECTLY WITH SD_MMC!");
        return;
    }
    Serial.printf("File opened. Size: %d bytes\n", current_jpeg_file.size());
    uint8_t buffer[4];
    current_jpeg_file.read(buffer, 4);
    Serial.printf("Header: %02x %02x %02x %02x\n", buffer[0], buffer[1], buffer[2], buffer[3]);
    current_jpeg_file.seek(0); // Reset for jpegdec

    // Use string open to avoid ambiguity if File reference is broken
    // Wait, let's fix the scan logic too!    
    if (jpeg.open(current_jpeg_file, MCU_Draw)) {
        int w = jpeg.getWidth();
        int h = jpeg.getHeight();
        Serial.printf("JPEG Opened! %d x %d\n", w, h);
        
        int scale_flag = 0;
        int scale_div = 1;
        if (w >= 3296 || h >= 3296) { scale_flag = 8; scale_div = 8; }
        else if (w >= 1648 || h >= 1648) { scale_flag = 4; scale_div = 4; }
        else if (w >= 824 || h >= 824) { scale_flag = 2; scale_div = 2; }
        else { scale_flag = 0; scale_div = 1; }
        
        current_jpeg_w = w / scale_div;
        current_jpeg_h = h / scale_div;
        
        // Allocate PSRAM buffer
        if (decoded_img_buf) {
            heap_caps_free(decoded_img_buf);
            decoded_img_buf = NULL;
        }
        
        decoded_img_buf = (uint8_t*)heap_caps_malloc(current_jpeg_w * current_jpeg_h * 2, MALLOC_CAP_SPIRAM);
                if (!decoded_img_buf) {
            Serial.println("FAILED TO ALLOCATE PSRAM FOR JPEG!");
            extern lv_obj_t *ui_bg_8;
            if (ui_bg_8) {
                lv_image_set_src(ui_bg_8, NULL); 
            }
            jpeg.close();
            if(current_jpeg_file) current_jpeg_file.close();
            return;
        }
        
        jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
        
        long t = millis();
        int res = jpeg.decode(0, 0, scale_flag); 
        long dur = millis() - t;
        
        if (res) {
            // disabled label
            Serial.printf("Decode successful in %ld ms! Updating screen.\n", dur);
            
            // Set up the LVGL Image Descriptor
            custom_img_dsc.header.w = current_jpeg_w;
            custom_img_dsc.header.h = current_jpeg_h;
            custom_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565; 
            custom_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            custom_img_dsc.header.flags = 0;
            custom_img_dsc.header.stride = current_jpeg_w * 2; // crucial for LVGL 9 rendering!
            custom_img_dsc.data_size = current_jpeg_w * current_jpeg_h * 2;
            custom_img_dsc.data = decoded_img_buf;
            

                        // Handle EXIF Rotation using JPEGDEC
            int orientation = jpeg.getOrientation();
            int angle = 0;
            if (orientation == 3) angle = 1800;
            else if (orientation == 6) angle = 900;
            else if (orientation == 8) angle = 2700;

            extern lv_obj_t *ui_bg_8;
            if (ui_bg_8) {
                // Force LVGL to accept the new image data by clearing the source first if it's the same pointer
                lv_image_set_src(ui_bg_8, NULL);
                lv_image_set_src(ui_bg_8, &custom_img_dsc);
                lv_image_set_rotation(ui_bg_8, angle);
                lv_obj_invalidate(ui_bg_8); // FORCIBLY redraw the new pixels!
            }


        } else {
            // disabled label
            Serial.println("Decode failed.");
        }
        jpeg.close(); 
    } else {
        // disabled label
        Serial.println("JPEGDEC failed to open the file.");
    }
    if (current_jpeg_file) {
        current_jpeg_file.close();
        Serial.println("Closed JPEG file handle.");
    }
}

void gallery_timer_cb(lv_timer_t* timer) {
    if (gallery_photos.empty()) return;
    
    // ONLY change pictures and spend CPU time decoding IF we are actually on the Gallery face!
    if (lv_screen_active() != ui_Gallery_pictures) return;
    
    String filename = gallery_photos[current_photo_index];
    current_photo_index = (current_photo_index + 1) % gallery_photos.size();
    
    load_and_decode_jpeg(filename);
}

void start_gallery() {
    extern lv_obj_t *ui_bg_8;
    // Remove black image test, we want to watch serial output for safety
    if (SD_MMC.cardSize() == 0) {
        SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
        SD_MMC.begin("/sdcard", true);
    }
    scan_gallery_photos();
    current_photo_index = 0;
    // Set timer to change photos
    if (gallery_photos.size() > 0) {
        gallery_timer = lv_timer_create(gallery_timer_cb, 4000, NULL);
        lv_timer_ready(gallery_timer); // Force it to run immediately so you don't wait on the first image!
    }
}

// -----------------------------


// --- BACKGROUND WIFI MANAGER TASK (Core 0) ---

// --- JOKE FETCHING TASK ---
volatile bool new_joke_ready = false;
String current_joke_text = "Loading joke...";

extern "C" const char* get_current_joke() {
    return current_joke_text.c_str();
}


void fetchJokeTask(void * parameter) {
    Serial.println("fetchJokeTask started via HTTP!");
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient clientJoke; // NORMAL HTTP CLIENT to save RAM and time!
        HTTPClient httpJoke;
        httpJoke.setTimeout(5000);
        
        String url = "http://official-joke-api.appspot.com/random_joke";
        httpJoke.begin(clientJoke, url);
        
        int wCode = httpJoke.GET();
        if (wCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, httpJoke.getString());
            if (doc.containsKey("setup")) {
                current_joke_text = doc["setup"].as<String>() + "\n\n" + doc["punchline"].as<String>();
                new_joke_ready = true;
                Serial.println("New HTTP joke fetched successfully! " + current_joke_text);
            } else {
                Serial.println("Joke JSON missing setup key!");
            }
        } else {
            Serial.printf("Joke fetch failed with code %d\n", wCode);
        }
        httpJoke.end();
    }
    vTaskDelete(NULL);
}

void wifiInitTask(void* pvParameters) {
    String custom_css = "<style>"
                        "body{background-color:#000000; color:#FFFFFF; font-family:'Helvetica Neue', sans-serif;}"
                        ".wrap {max-width: 400px; margin: 0 auto; padding: 20px;}"
                        "button{background-color:#148CA0; color:#FFFFFF; border:none; border-radius:20px; padding:15px; font-size:18px; width:100%; margin-top:15px; box-shadow: 0px 4px 10px rgba(20,140,160,0.5); font-weight:bold;}"
                        "button:hover{background-color:#106b7a;}"
                        "input[type='text'], input[type='password']{background-color:#1a1a1a; color:#fff; border:2px solid #148CA0; border-radius:10px; padding:15px; width:100%; box-sizing:border-box; margin-top:10px; font-size:16px;}"
                        "h1 {color: #148CA0; text-align:center; font-size: 2.5em; font-weight: 800; margin-bottom: 5px; text-transform: uppercase; letter-spacing: 2px;}"
                        "div.c, div.q {background-color:#111111; padding:20px; border-radius:15px; margin-bottom:20px; border: 1px solid #333;}"
                        "a {color:#148CA0; text-decoration:none; font-weight:bold;}"
                        ".msg {color: #aaa; text-align:center; margin-bottom: 20px;}"
                        "</style>";
    
    wm.setCustomHeadElement(custom_css.c_str());
    wm.setCustomMenuHTML("<div class='msg'>Welcome to YUMO Cube Setup<br>Select your Wi-Fi network below.</div>");
    WiFi.setSleep(false);
    wm.setConfigPortalBlocking(false);
    
    WiFi.mode(WIFI_STA);
    if(wm.autoConnect("YUMO Cube")) {
        Serial.println("Already connected to WiFi!");
    
    } else {
        Serial.println("Configportal running on: YUMO Cube");
    }
    
    wifi_init_done = true;
    vTaskDelete(NULL);
}


void updateLocalTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 10)) { 
        static unsigned long lastWaitPrint = 0;
        if(millis() - lastWaitPrint > 5000) {
            Serial.println("Waiting for NTP sync...");
            lastWaitPrint = millis();
        }
        return; 
    }
    
    char timeStr[10];
    int hr = timeinfo.tm_hour;
    bool isPM = (hr >= 12);
    if (hr == 0) hr = 12;
    if (hr > 12) hr -= 12;
    
    if (timeinfo.tm_sec % 2 == 0) {
        sprintf(timeStr, "%d:%02d", hr, timeinfo.tm_min);
    } else {
        sprintf(timeStr, "%d %02d", hr, timeinfo.tm_min);
    }
    
    char dateStr[32];
    strftime(dateStr, sizeof(dateStr), "%d %B %Y", &timeinfo);
    
    char dayStr[16];
    strftime(dayStr, sizeof(dayStr), "%A", &timeinfo);
    
    const char* ampm = isPM ? "pm" : "am";
    
    extern lv_obj_t *ui_clock1;
    if(ui_clock1) lv_label_set_text(ui_clock1, timeStr);
    
    extern lv_obj_t *ui_dots1;
    if(ui_dots1) {
        lv_obj_add_flag(ui_dots1, LV_OBJ_FLAG_HIDDEN); // Hide the disconnected dots forever!
    }
    
    extern lv_obj_t *ui_day_night;
    if(ui_day_night) lv_label_set_text(ui_day_night, ampm);
    
    extern lv_obj_t* ui_city_gruop_1;
    lv_obj_t* title_group = ui_comp_get_child(ui_city_gruop_1, UI_COMP_TITLEGROUP_SUBTITLE);
    if(title_group) lv_label_set_text(title_group, dateStr);
}

void setup() {
    
    // --- BATTERY POWER LATCH ---
    // The schematic shows the power LDO/Mosfet EN is driven by IO7.
    // We instantly pull IO7 HIGH so the ESP32 keeps itself alive!
    pinMode(7, OUTPUT);
    digitalWrite(7, HIGH);

    // Set the button pin to INPUT (schematic shows it's on IO6, active low when pushed)
    pinMode(6, INPUT_PULLUP);
    leds_init();

    Serial.begin(115200);
    
    // 1. Initialize the I2C bus FIRST!
    // The screen's I/O expander (TCA9554), the touch panel, and the IMU ALL share this bus.
    // If we don't start it here, the screen backlight and reset pins can't be turned on.
    Wire.begin(11, 10);

    // IMPORTANT Waveshare initialization calls for the IO Expander and Backlight!
    TCA9554PWR_Init(0x00);

    // 2. Initialize the QSPI Screen, Touch Panel, and I/O Expander
    Lvgl_Init();
    // 3. Initialize QMI8658 IMU (I2C)
    if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, 11, 10)) {
        Serial.println("Failed to find QMI8658!");
    } else {
        qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0); // 3 args
        qmi.enableAccelerometer();
        Serial.println("QMI8658 Found and Initialized successfully!");
    }


    // 3. Initialize your exact UI directly generated from SquareLine
    ui_init();
    
    // 4. Calculate current physical face based on gravity immediately on boot
    current_face = get_cube_face();

    // 5. Rotate the screen physically so it is right-side up for the user
    lv_display_t * current_display = lv_display_get_default();
    if(current_display) {
        lv_display_set_rotation(current_display, face_to_rotation(current_face));
    }

    // 6. Force the Startup Page exactly as you requested
    lv_screen_load(ui_startup_page);
    leds_start_police();

    // 7. Create a delay timer (e.g., 5000 milliseconds / 5 seconds) to transition into the loading page sequence
    // The explicit timer runs once to move out of the startup phase.
    lv_timer_create(transition_timer_cb, 5000, NULL);


    // LVGL MUST DRAW THE STARTUP SCREEN NOW before WiFi blocks!
    lv_timer_handler();

    
    // NOW TURN ON THE BACKLIGHT! This is done *after* LVGL renders the first frame to hide the garbled memory screen!
    Backlight_Init();
    Set_Backlight(60);

    // After the screen is on, we can take a little time to initialize the SD card and Gallery photos!
    start_gallery();


    weatherQueue = xQueueCreate(1, sizeof(WeatherData));
    if (weatherQueue != NULL) {
        xTaskCreatePinnedToCore(weatherBgTask, "weatherBgTask", 8192, NULL, 1, NULL, 1);
    }


    // 8. Lastly, start WiFi connection silently on Core 0 so it DOES NOT block the boot animations!
    xTaskCreatePinnedToCore(wifiInitTask, "wifiInitTask", 8192, NULL, 1, NULL, 0);
}


void loop() {
    // --- 3-SECOND POWER-OFF HOLD LOGIC ---
    static unsigned long buttonHoldStart = 0;
    if (digitalRead(6) == LOW) { // Button pressed
        last_activity_ms = millis();
        if (buttonHoldStart == 0) buttonHoldStart = millis();
        else if (millis() - buttonHoldStart > 3000) {
            // Unlatch power mosfet! Devices shuts off.
            Serial.println("Powering off...");
            digitalWrite(7, LOW); 
            delay(100); // Give it time to die
        }
    } else {
        buttonHoldStart = 0; // Reset holding timer
    }

    // LVGL MUST run often. By default it decodes images here.
    // The loop task stack was upgraded to 32KB in platformio.ini to prevent panics!
    lv_timer_handler();

    Lvgl_Loop();
    // --- GAME LOOP FOR FACE 5 ---
    extern lv_obj_t *ui_activity_game;
    if (lv_screen_active() == ui_activity_game && current_face == 5) {
        float ax, ay, az;
        if (qmi.getAccelerometer(ax, ay, az)) {
            // Apply physics (swapped axes for Face 5)
            game_ball_vel_x -= ay * 0.8;
            game_ball_vel_y += ax * 0.8;

            static unsigned long last_game_print = 0;
            if (millis() - last_game_print > 200) {
                last_game_print = millis();
                // Serial.printf("GAME TILT -> ax: %.2f | ay: %.2f | vel_x: %.2f | vel_y: %.2f\n", ax, ay, game_ball_vel_x, game_ball_vel_y);
            }

            // Apply friction
            game_ball_vel_x *= 0.95;
            game_ball_vel_y *= 0.95;
            
            game_ball_x += game_ball_vel_x;
            game_ball_y += game_ball_vel_y;

            // Circular boundary collision! 
            // Screen is 412x412 (Radius 206). So max distance is 206 for half the ball to exit the screen.
            float dist = sqrt(game_ball_x*game_ball_x + game_ball_y*game_ball_y);
            float max_radius = 206.0f;
            if (dist > max_radius) {
                float nx = game_ball_x / dist;
                float ny = game_ball_y / dist;
                
                // Reposition precisely on the boundary
                game_ball_x = nx * max_radius;
                game_ball_y = ny * max_radius;
                
                // Reflect velocity: V = V - 2*(V.N)*N
                float dot = (game_ball_vel_x * nx) + (game_ball_vel_y * ny);
                game_ball_vel_x = (game_ball_vel_x - 2 * dot * nx) * 0.5f; // Bounce dampening
                game_ball_vel_y = (game_ball_vel_y - 2 * dot * ny) * 0.5f;
            }
            
            // Move object safely
            if (ui_game_ball != NULL) {
                lv_obj_align(ui_game_ball, LV_ALIGN_CENTER, (int)game_ball_x, (int)game_ball_y);
            }

            // Hit detection with target (Radius ~20 for ball, 10 for target. Distance < 30 means hit)
            float dx = game_ball_x - game_target_x;
            float dy = game_ball_y - game_target_y;
            if (dx*dx + dy*dy < 900) { 
                game_score++;
                if (ui_game_score != NULL) {
                    lv_label_set_text_fmt(ui_game_score, "Score: %d", game_score);
                }
                
                // Randomize new target inside circular area securely
                float angle = (rand() % 360) * 3.14159f / 180.0f;
                int r = rand() % 150; // Max radius 150 keeps the dot away from edges
                game_target_x = r * cos(angle);
                game_target_y = r * sin(angle);
                if (ui_game_dot != NULL) {
                    lv_obj_align(ui_game_dot, LV_ALIGN_CENTER, game_target_x, game_target_y);
                }
            }
        }
    }

    // Check if we need to fetch a new joke (every 5 mins or on first connect)
    static unsigned long lastJokeUpdate = 0;
    static bool firstJokeFetched = false;
    if (WiFi.status() == WL_CONNECTED) {
        if (!firstJokeFetched || (millis() - lastJokeUpdate > 300000)) {
            firstJokeFetched = true;
            lastJokeUpdate = millis() == 0 ? 1 : millis();
            BaseType_t res = xTaskCreatePinnedToCore(fetchJokeTask, "fetchJokeTask", 8192, NULL, 1, NULL, 1);
            if (res != pdPASS) Serial.printf("Joke Task create failed! %d\n", res);
        }
    }

    
    if (new_joke_ready) {
        new_joke_ready = false;
        extern lv_obj_t* ui_joke_label;
        if (ui_joke_label) {
            lv_label_set_text(ui_joke_label, current_joke_text.c_str());
        }
    }

    if (wifi_init_done && WiFi.status() != WL_CONNECTED) {
        wm.process();
    }

    // Check if we just swiped to the gallery screen so we load the first image instantly!
    extern lv_obj_t *ui_Gallery_pictures;
    static bool gallery_was_active = false;
    bool gallery_is_active = (lv_screen_active() == ui_Gallery_pictures);
    if (gallery_is_active && !gallery_was_active) {
        gallery_was_active = true;
        // We just entered the gallery screen! Load an image right now by firing the timer immediately.
        if (gallery_timer != NULL) {
            lv_timer_ready(gallery_timer);
        }
    } else if (!gallery_is_active && gallery_was_active) {
        gallery_was_active = false;
    }

    // Check if we swiped to the weather screen to instantly refresh time and weather!
    extern lv_obj_t *ui_weather_time;
    static bool weather_was_active = false;
    bool weather_is_active = (lv_screen_active() == ui_weather_time);
    if (weather_is_active && !weather_was_active) {
        weather_was_active = true;
        if (WiFi.status() == WL_CONNECTED) {
            updateLocalTime();
        }
        // Update battery immediately when entering weather screen
        {
            int pct = read_battery_percent();
            char batStr[8];
            sprintf(batStr, "%d%%", pct);
            extern lv_obj_t *ui_batterygroup1;
            lv_obj_t* batt_label = ui_comp_get_child(ui_batterygroup1, UI_COMP_BATTERYGROUP_BATTERY_PERCENT);
            if (batt_label) lv_label_set_text(batt_label, batStr);
        }
    } else if (!weather_is_active && weather_was_active) {
        weather_was_active = false;
    }

    // Update battery every 30 seconds while on the weather screen
    static unsigned long lastBattUpdate = 0;
    if (weather_is_active && millis() - lastBattUpdate >= 30000) {
        lastBattUpdate = millis();
        int pct = read_battery_percent();
        char batStr[8];
        sprintf(batStr, "%d%%", pct);
        extern lv_obj_t *ui_batterygroup1;
        lv_obj_t* batt_label = ui_comp_get_child(ui_batterygroup1, UI_COMP_BATTERYGROUP_BATTERY_PERCENT);
        if (batt_label) lv_label_set_text(batt_label, batStr);
    }


    // --- Background Tasks for Weather and Time ---
    if (WiFi.status() == WL_CONNECTED) {
        // Update UI Time every second
        if (millis() - lastTimeUpdate >= timeUpdateInterval) {
            lastTimeUpdate = millis();
            updateLocalTime();
        }

        // Check if our background task safely delivered a new weather package via IPC Queue!
        if (weatherQueue != NULL) {
            WeatherData wd;
            if (xQueueReceive(weatherQueue, &wd, 0) == pdTRUE) {
                // Safely update LVGL from Core 1!
                char tStr[8], minmaxStr[32], windStr[16], humStr[8];
                sprintf(tStr, "%d°", (int)wd.temp);
                sprintf(minmaxStr, "Max: %d° Min: %d°", (int)wd.temp_max, (int)wd.temp_min);
                sprintf(windStr, "%d km/h", (int)wd.wind_speed);
                sprintf(humStr, "%d%%", wd.humidity);
                
                if(ui_label_degree) lv_label_set_text(ui_label_degree, tStr);
                if(ui_wind_speed) lv_label_set_text(ui_wind_speed, windStr);
                if(ui_rain_percent) lv_label_set_text(ui_rain_percent, humStr);
                
                lv_obj_t* w_subtitle = ui_comp_get_child(ui_weather_title_group_3, UI_COMP_TITLEGROUP_SUBTITLE);
                if(w_subtitle) lv_label_set_text(w_subtitle, minmaxStr);
                
                lv_obj_t* w_title = ui_comp_get_child(ui_weather_title_group_3, UI_COMP_TITLEGROUP_TITLE);
                if(w_title) lv_label_set_text(w_title, wd.desc);
                
                lv_obj_t* c_title = ui_comp_get_child(ui_city_gruop_1, UI_COMP_TITLEGROUP_TITLE);
                if(c_title) lv_label_set_text(c_title, wd.city);
                
                if(ui_sun && ui_clouds) {
                    String ic = String((char*)wd.icon_code);
                    if (ic.startsWith("01")) {
                        lv_obj_remove_flag(ui_sun, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_add_flag(ui_clouds, LV_OBJ_FLAG_HIDDEN);
                    } else if (ic.startsWith("02")) {
                        lv_obj_remove_flag(ui_sun, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_remove_flag(ui_clouds, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui_sun, LV_OBJ_FLAG_HIDDEN);
                        lv_obj_remove_flag(ui_clouds, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                Serial.println("Safe IPC Queue: Weather & UI Updated Successfully, 0 Frame Drops!");
            }
        }
    }

    // ---------------------------------------------

    // Read the rotation every ~100ms
    static unsigned long last_imu_check = 0;
    if (millis() - last_imu_check >= 100) {
        last_imu_check = millis();

        if (is_sleeping) {
            // While asleep: only watch for movement to wake up
            float ax, ay, az;
            if (qmi.getAccelerometer(ax, ay, az)) {
                float diff = fabs(ax - sleep_ax) + fabs(ay - sleep_ay) + fabs(az - sleep_az);
                if (diff > WAKE_THRESHOLD) {
                    is_sleeping = false;
                    last_activity_ms = millis();
                    // Re-read face in case the cube was moved while asleep
                    current_face = get_cube_face();
                    lv_display_t * disp = lv_display_get_default();
                    if (disp) lv_display_set_rotation(disp, face_to_rotation(current_face));
                    if (current_face == 6) { leds_start_heartwave(); Set_Backlight(0); }
                    else { leds_off(); Set_Backlight(60); }
                    Serial.println("Sleep: Wake triggered by movement.");
                }
            }
        } else {
            // Check if it's time to sleep (only after startup is done)
            if (!is_startup_phase && last_activity_ms > 0 && millis() - last_activity_ms > SLEEP_TIMEOUT_MS) {
                is_sleeping = true;
                float ax, ay, az;
                if (qmi.getAccelerometer(ax, ay, az)) { sleep_ax = ax; sleep_ay = ay; sleep_az = az; }
                Set_Backlight(0);
                leds_off();
                Serial.println("Sleep: Entering sleep mode.");
            } else {
                int new_face = get_cube_face();

                if (new_face != current_face) {
                    current_face = new_face;

                    Serial.printf("Rotation detected! New Face: %d\n", current_face);

                    // Instantly rotate the display output
                    lv_display_t * current_display = lv_display_get_default();
                    if(current_display) {
                        lv_display_set_rotation(current_display, face_to_rotation(current_face));
                    }

                    // Only switch screens automatically if we're past the startup page
                    if (!is_startup_phase) {
                        load_loading_page(current_face);
                    }
                }
            }
        }
    }

    leds_tick();
    delay(5);
}// just checking EOF
