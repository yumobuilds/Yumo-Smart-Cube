#include "../ui.h"
#include <stdio.h>

// LVGL Stretch App Global Variables
lv_obj_t *ui_function_5 = NULL;
static lv_obj_t *ui_stretch_arc = NULL;
static lv_obj_t *ui_stretch_time_label = NULL;
static lv_obj_t *ui_stretch_name_label = NULL;
static lv_obj_t *ui_stretch_sub_label = NULL;
static lv_obj_t *ui_stretch_pause_icon = NULL;
static lv_obj_t *ui_stretch_start_overlay = NULL;

static lv_timer_t * stretch_timer = NULL;

static bool session_started = false;

static const char* stretch_names[] = {
    "Neck Roll",
    "Shoulder Shrug",
    "Wrist Rotation",
    "Torso Twist",
    "Chair Squat"
};
static const int NUM_STRETCHES = 5;
static const int STRETCH_DURATION_SEC = 30;

static int current_stretch_idx = 0;
static int time_remaining = STRETCH_DURATION_SEC;
static bool stretch_paused = false;
static bool session_done = false;

static void update_stretch_ui() {
    if (!ui_function_5 || !ui_stretch_time_label) return;

    // Not yet started — show TAP TO START overlay
    if (!session_started && !session_done) {
        if (ui_stretch_name_label) lv_label_set_text(ui_stretch_name_label, "Stretch Workout");
        if (ui_stretch_sub_label) {
            lv_label_set_text(ui_stretch_sub_label, "5 exercises  ·  30s each");
            lv_obj_align(ui_stretch_sub_label, LV_ALIGN_CENTER, 0, 70);
        }
        if (ui_stretch_time_label) lv_obj_add_flag(ui_stretch_time_label, LV_OBJ_FLAG_HIDDEN);
        if (ui_stretch_pause_icon) lv_obj_add_flag(ui_stretch_pause_icon, LV_OBJ_FLAG_HIDDEN);
        if (ui_stretch_arc) lv_arc_set_value(ui_stretch_arc, 0);
        if (ui_stretch_start_overlay) lv_obj_clear_flag(ui_stretch_start_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Hide start overlay when running
    if (ui_stretch_start_overlay) lv_obj_add_flag(ui_stretch_start_overlay, LV_OBJ_FLAG_HIDDEN);

    if (session_done) {
        if (ui_stretch_name_label) lv_label_set_text(ui_stretch_name_label, "Excellent!");
        if (ui_stretch_sub_label) {
            lv_label_set_text(ui_stretch_sub_label, "Workout finished\\nTouch to start");
            // Also shift the subtitle slightly for the new text layout
            lv_obj_align(ui_stretch_sub_label, LV_ALIGN_CENTER, 0, 20); 
        }
        if (ui_stretch_time_label) lv_obj_add_flag(ui_stretch_time_label, LV_OBJ_FLAG_HIDDEN);
        if (ui_stretch_pause_icon) lv_obj_add_flag(ui_stretch_pause_icon, LV_OBJ_FLAG_HIDDEN);
        if (ui_stretch_arc) lv_arc_set_value(ui_stretch_arc, STRETCH_DURATION_SEC); // Full ring
        return;
    }
    
    // Normal state
    if (ui_stretch_time_label) lv_obj_clear_flag(ui_stretch_time_label, LV_OBJ_FLAG_HIDDEN);
    if (ui_stretch_sub_label) lv_obj_align(ui_stretch_sub_label, LV_ALIGN_CENTER, 0, 70); // Restore original align

    // Update time formatting strictly
    int mins = time_remaining / 60;
    int secs = time_remaining % 60;
    
    if (ui_stretch_time_label) lv_label_set_text_fmt(ui_stretch_time_label, "%02d:%02d", mins, secs);
    if (ui_stretch_name_label) lv_label_set_text(ui_stretch_name_label, stretch_names[current_stretch_idx]);
    if (ui_stretch_sub_label) lv_label_set_text_fmt(ui_stretch_sub_label, "%d / %d", current_stretch_idx + 1, NUM_STRETCHES);
    
    if (ui_stretch_arc) {
        lv_arc_set_value(ui_stretch_arc, STRETCH_DURATION_SEC - time_remaining);
    }
    
    if (ui_stretch_pause_icon) {
        if (stretch_paused) {
             lv_obj_clear_flag(ui_stretch_pause_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
             lv_obj_add_flag(ui_stretch_pause_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void stretch_timer_cb(lv_timer_t * timer) {
    if (stretch_paused || session_done || !session_started) return;
    
    if (time_remaining > 0) {
        time_remaining--;
    } else {
        current_stretch_idx++;
        if (current_stretch_idx >= NUM_STRETCHES) {
            // Reached the end!
            session_done = true;
            update_stretch_ui();
            return;
        }
        time_remaining = STRETCH_DURATION_SEC;
    }
    update_stretch_ui();
}

static void stretch_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if(code == LV_EVENT_CLICKED) {
        if (session_done) {
            // Restart — go back to TAP TO START screen
            session_done = false;
            session_started = false;
            current_stretch_idx = 0;
            time_remaining = STRETCH_DURATION_SEC;
            stretch_paused = false;
            update_stretch_ui();
            return;
        }
        if (!session_started) {
            // First tap — begin the workout
            session_started = true;
            stretch_paused = false;
            update_stretch_ui();
            return;
        }
        stretch_paused = !stretch_paused;
        update_stretch_ui();
    }
    else if(code == LV_EVENT_GESTURE) {
        if (session_done) return; // Prevent swipe when done
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if(dir == LV_DIR_LEFT) {
            // Swipe Left -> Next Stretch
            current_stretch_idx++;
            if (current_stretch_idx >= NUM_STRETCHES) {
                // Reached end by skipping
                session_done = true;
                update_stretch_ui();
                return;
            }
            time_remaining = STRETCH_DURATION_SEC;
            update_stretch_ui();
        }
        else if(dir == LV_DIR_RIGHT) {
            // Swipe Right -> Previous Stretch
            current_stretch_idx--;
            if (current_stretch_idx < 0) current_stretch_idx = NUM_STRETCHES - 1;
            time_remaining = STRETCH_DURATION_SEC;
            update_stretch_ui();
        }
    }
}

void ui_function_5_screen_init(void)
{
    // Make sure we stop existing timers if re-initialized
    if (stretch_timer) {
        lv_timer_del(stretch_timer);
        stretch_timer = NULL;
    }

    ui_function_5 = lv_obj_create(NULL);
    lv_obj_remove_flag( ui_function_5, LV_OBJ_FLAG_SCROLLABLE ); // Very important so gestures aren't consumed heavily 
    lv_obj_set_style_bg_color(ui_function_5, lv_color_hex(0x0a0a0a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_function_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Add cool glowing background
    lv_obj_t *ui_bg = lv_image_create(ui_function_5);
    lv_image_set_src(ui_bg, &ui_img_bg1_png);
    lv_obj_set_width( ui_bg, LV_SIZE_CONTENT);  
    lv_obj_set_height( ui_bg, LV_SIZE_CONTENT);   
    lv_obj_align( ui_bg, LV_ALIGN_CENTER, 0, 0 );
    lv_obj_remove_flag(ui_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_scale(ui_bg, 270); // Scaled slightly smaller from 280

    // YUMO Cube Blue Tint
    lv_obj_set_style_image_recolor(ui_bg, lv_color_hex(0x148CA0), 0); 
    lv_obj_set_style_image_recolor_opa(ui_bg, 100, 0);

    // The huge circular progress Arc
    ui_stretch_arc = lv_arc_create(ui_function_5);
    lv_obj_set_size(ui_stretch_arc, 340, 340);
    lv_obj_align(ui_stretch_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(ui_stretch_arc, 270); // start at top
    lv_arc_set_bg_angles(ui_stretch_arc, 0, 360);
    lv_arc_set_angles(ui_stretch_arc, 0, 0);  // current angle
    lv_arc_set_range(ui_stretch_arc, 0, STRETCH_DURATION_SEC);
    lv_obj_remove_style(ui_stretch_arc, NULL, LV_PART_KNOB); // hide knob
    lv_obj_clear_flag(ui_stretch_arc, LV_OBJ_FLAG_CLICKABLE); // purely visual
    
    lv_obj_set_style_arc_color(ui_stretch_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    // Use an extra wide track for a premium "Activity Ring" feel!
    lv_obj_set_style_arc_width(ui_stretch_arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui_stretch_arc, lv_color_hex(0x148CA0), LV_PART_INDICATOR); // YUMO cyan 
    lv_obj_set_style_arc_width(ui_stretch_arc, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui_stretch_arc, true, LV_PART_INDICATOR);
    
    // Create Time Label
    ui_stretch_time_label = lv_label_create(ui_function_5);
    lv_obj_set_style_text_font(ui_stretch_time_label, &ui_font_Number_big, 0); 
    lv_obj_set_style_text_color(ui_stretch_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_stretch_time_label, LV_ALIGN_CENTER, 0, -10); // Center slightly up
    
    // Create Stretch Name title above
    ui_stretch_name_label = lv_label_create(ui_function_5);
    lv_obj_set_style_text_font(ui_stretch_name_label, &ui_font_Title, 0); 
    lv_obj_set_style_text_color(ui_stretch_name_label, lv_color_hex(0x148CA0), 0);
    lv_obj_align(ui_stretch_name_label, LV_ALIGN_CENTER, 0, -100);
    
    // Stretch # display
    ui_stretch_sub_label = lv_label_create(ui_function_5);
    lv_obj_set_style_text_font(ui_stretch_sub_label, &ui_font_Subtitle, 0); 
    lv_obj_set_style_text_color(ui_stretch_sub_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(ui_stretch_sub_label, LV_ALIGN_CENTER, 0, 70);

    // Pause icon
    ui_stretch_pause_icon = lv_label_create(ui_function_5);
    lv_obj_set_style_text_font(ui_stretch_pause_icon, &ui_font_Subtitle, 0); // Using standard subtitle font
    // Fallback to text if symbol not available.
    lv_label_set_text(ui_stretch_pause_icon, "PAUSED");
    lv_obj_set_style_text_color(ui_stretch_pause_icon, lv_color_hex(0xff3333), 0);
    lv_obj_align(ui_stretch_pause_icon, LV_ALIGN_CENTER, 0, 110);
    lv_obj_add_flag(ui_stretch_pause_icon, LV_OBJ_FLAG_HIDDEN);

    // TAP TO START overlay label
    ui_stretch_start_overlay = lv_label_create(ui_function_5);
    lv_obj_set_width(ui_stretch_start_overlay, 280);
    lv_label_set_long_mode(ui_stretch_start_overlay, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui_stretch_start_overlay, "TAP TO\nSTART");
    lv_obj_set_style_text_font(ui_stretch_start_overlay, &ui_font_Title, 0);
    lv_obj_set_style_text_color(ui_stretch_start_overlay, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(ui_stretch_start_overlay, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_stretch_start_overlay, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_flag(ui_stretch_start_overlay, LV_OBJ_FLAG_HIDDEN); // shown by update_stretch_ui

    // Reset session state each time screen is built
    session_started = false;
    session_done = false;
    current_stretch_idx = 0;
    time_remaining = STRETCH_DURATION_SEC;
    stretch_paused = false;

    // Gestures and Tap
    lv_obj_add_flag(ui_function_5, LV_OBJ_FLAG_CLICKABLE); // So it catches taps
    lv_obj_clear_flag(ui_function_5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui_function_5, stretch_event_cb, LV_EVENT_ALL, NULL);

    // Initial update
    update_stretch_ui();
    
    // Timer at exactly 1 second
    stretch_timer = lv_timer_create(stretch_timer_cb, 1000, NULL);
}

void ui_function_5_screen_reset(void)
{
    if (!ui_function_5) return;
    session_started = false;
    session_done = false;
    current_stretch_idx = 0;
    time_remaining = STRETCH_DURATION_SEC;
    stretch_paused = false;
    update_stretch_ui();
}

void ui_function_5_screen_destroy(void)
{
    if (stretch_timer) {
        lv_timer_del(stretch_timer);
        stretch_timer = NULL;
    }
    if (ui_function_5) {
        lv_obj_del(ui_function_5);
        ui_function_5 = NULL;
    }
    
    ui_stretch_arc = NULL;
    ui_stretch_time_label = NULL;
    ui_stretch_name_label = NULL;
    ui_stretch_sub_label = NULL;
    ui_stretch_pause_icon = NULL;
    ui_stretch_start_overlay = NULL;
}
