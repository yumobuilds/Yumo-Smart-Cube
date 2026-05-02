#include "../ui.h"

// Recreated legacy bouncing ball interface!
lv_obj_t *ui_activity_game = NULL;
lv_obj_t *ui_bg_10 = NULL;

extern lv_obj_t* ui_game_ball;
extern lv_obj_t* ui_game_dot;
extern lv_obj_t* ui_game_score;

void ui_activity_game_screen_init(void)
{
    ui_activity_game = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_activity_game, LV_OBJ_FLAG_SCROLLABLE); // Very important!
    lv_obj_set_style_bg_color(ui_activity_game, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_activity_game, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_bg_10 = lv_image_create(ui_activity_game);
    lv_image_set_src(ui_bg_10, &ui_img_bg1_png);
    lv_obj_set_width(ui_bg_10, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_bg_10, LV_SIZE_CONTENT);
    lv_obj_set_align(ui_bg_10, LV_ALIGN_CENTER);
    lv_obj_remove_flag(ui_bg_10, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_scale(ui_bg_10, 250);

    // Build the ball
    ui_game_ball = lv_obj_create(ui_activity_game);
    lv_obj_set_width(ui_game_ball, 40);
    lv_obj_set_height(ui_game_ball, 40);
    lv_obj_set_style_radius(ui_game_ball, 20, 0);
    lv_obj_set_style_bg_color(ui_game_ball, lv_color_hex(0x148CA0), 0); // YUMO Cube Cyan
    lv_obj_set_style_border_width(ui_game_ball, 0, 0);
    lv_obj_align(ui_game_ball, LV_ALIGN_CENTER, 0, 0);

    // Build the target dot
    ui_game_dot = lv_obj_create(ui_activity_game);
    lv_obj_set_width(ui_game_dot, 20);
    lv_obj_set_height(ui_game_dot, 20);
    lv_obj_set_style_radius(ui_game_dot, 10, 0);
    lv_obj_set_style_bg_color(ui_game_dot, lv_color_hex(0xFF0000), 0); // Red
    lv_obj_set_style_border_width(ui_game_dot, 0, 0);
    lv_obj_align(ui_game_dot, LV_ALIGN_CENTER, 30, -50);

    // Build the score
    ui_game_score = lv_label_create(ui_activity_game);
    lv_obj_set_style_text_color(ui_game_score, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_game_score, &lv_font_montserrat_20, 0);
    lv_label_set_text(ui_game_score, "Score: 0");
    lv_obj_align(ui_game_score, LV_ALIGN_TOP_MID, 0, 50);
}

void ui_activity_game_screen_destroy(void)
{
    if (ui_activity_game) lv_obj_del(ui_activity_game);
    ui_activity_game = NULL;
    ui_bg_10 = NULL;
    ui_game_ball = NULL;
    ui_game_dot = NULL;
    ui_game_score = NULL;
}
