// SquareLine LVGL GENERATED FILE
// EDITOR VERSION: SquareLine Studio 1.1.1
// LVGL VERSION: 8.3.3
// PROJECT: sensecap
//
// Trimmed for the TriMet display: only the template's Wi-Fi and factory
// reset screens are left (the Wi-Fi list itself is built in indicator_view.c).

#include "ui.h"
#include "ui_helpers.h"

///////////////////// VARIABLES ////////////////////
lv_obj_t *ui_screen_wifi;
lv_obj_t *ui_wifi_st_6;
lv_obj_t *ui_wifi_title;
void ui_event_back3( lv_event_t * e);
lv_obj_t *ui_back3;

lv_obj_t *ui_screen_factory;
lv_obj_t *ui_factory_resetting_title;

static lv_obj_t *ui_screen_last;

/* Where the Wi-Fi screen's back button returns to (added for the TriMet display). */
void ui_set_screen_last(lv_obj_t *screen)
{
    ui_screen_last = screen;
}
///////////////////// TEST LVGL SETTINGS ////////////////////
#if LV_COLOR_DEPTH != 16
    #error "LV_COLOR_DEPTH should be 16bit to match SquareLine Studio's settings"
#endif
#if LV_COLOR_16_SWAP !=0
    #error "LV_COLOR_16_SWAP should be 0 to match SquareLine Studio's settings"
#endif

///////////////////// FUNCTIONS ////////////////////
void ui_event_back3( lv_event_t * e) {
    lv_event_code_t event_code = lv_event_get_code(e);
    if ( event_code == LV_EVENT_CLICKED && ui_screen_last != NULL ) {
        _ui_screen_change( ui_screen_last, LV_SCR_LOAD_ANIM_OVER_TOP, 200, 0);
    }
}

///////////////////// SCREENS ////////////////////
void ui_screen_wifi_screen_init(void)
{
ui_screen_wifi = lv_obj_create(NULL);
lv_obj_clear_flag( ui_screen_wifi, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

ui_wifi_st_6 = lv_img_create(ui_screen_wifi);
lv_img_set_src(ui_wifi_st_6, &ui_img_wifi_disconet_png);
lv_obj_set_width( ui_wifi_st_6, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_wifi_st_6, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_wifi_st_6, -20 );
lv_obj_set_y( ui_wifi_st_6, 20 );
lv_obj_set_align( ui_wifi_st_6, LV_ALIGN_TOP_RIGHT );
lv_obj_add_flag( ui_wifi_st_6, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( ui_wifi_st_6, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

ui_wifi_title = lv_label_create(ui_screen_wifi);
lv_obj_set_width( ui_wifi_title, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_wifi_title, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_wifi_title, 1 );
lv_obj_set_y( ui_wifi_title, 50 );
lv_obj_set_align( ui_wifi_title, LV_ALIGN_TOP_MID );
lv_label_set_text(ui_wifi_title,"WiFi");
lv_obj_set_style_text_font(ui_wifi_title, &ui_font_font1, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_back3 = lv_btn_create(ui_screen_wifi);
lv_obj_set_width( ui_back3, 100);
lv_obj_set_height( ui_back3, 50);
lv_obj_set_x( ui_back3, 10 );
lv_obj_set_y( ui_back3, 30 );
lv_obj_add_flag( ui_back3, LV_OBJ_FLAG_SCROLL_ON_FOCUS );   /// Flags
lv_obj_clear_flag( ui_back3, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_bg_color(ui_back3, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(ui_back3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_img_src( ui_back3, &ui_img_back_png, LV_PART_MAIN | LV_STATE_DEFAULT );

lv_obj_add_event_cb(ui_back3, ui_event_back3, LV_EVENT_ALL, NULL);

}

void ui_screen_factory_screen_init(void)
{
ui_screen_factory = lv_obj_create(NULL);
lv_obj_clear_flag( ui_screen_factory, LV_OBJ_FLAG_SCROLLABLE );

ui_factory_resetting_title = lv_label_create(ui_screen_factory);
lv_obj_set_width( ui_factory_resetting_title, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_factory_resetting_title, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( ui_factory_resetting_title, LV_ALIGN_CENTER );
lv_label_set_text(ui_factory_resetting_title,"Factory Resetting...");
lv_obj_set_style_text_font(ui_factory_resetting_title, &lv_font_montserrat_32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_color(ui_factory_resetting_title, lv_color_hex(0xCD3700), LV_PART_MAIN | LV_STATE_DEFAULT );
}

void ui_init( void )
{
lv_disp_t *dispp = lv_disp_get_default();
lv_theme_t *theme = lv_theme_default_init(dispp, lv_color_hex(0x529d53), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
lv_disp_set_theme(dispp, theme);
ui_screen_wifi_screen_init();
ui_screen_factory_screen_init();
}
