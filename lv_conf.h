#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * LVGL v9 configuration for Arduino + ESP32 CYD (ESP32-2432S028R)
 * Target: low RAM usage, no PSRAM available.
 */

#define LV_USE_OS            LV_OS_NONE

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH       16
#define LV_COLOR_16_SWAP     0

/*=========================
   MEMORY SETTINGS
 *=========================*/
#define LV_MEM_CUSTOM        0
#define LV_MEM_SIZE          (48U * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD  30
#define LV_INDEV_DEF_READ_PERIOD 30

/*==================
   DRAW SETTINGS
 *==================*/
#define LV_DRAW_SW_COMPLEX   0
#define LV_USE_DRAW_SW       1

/*====================
   LOG / DEBUG
 *====================*/
#define LV_USE_LOG           0
#define LV_USE_ASSERT_NULL   0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_ASSERT_STYLE  0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ    0

/*====================
   FEATURES
 *====================*/
#define LV_USE_ANIMATION     1
#define LV_USE_SHADOW        0
#define LV_USE_GROUP         1

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0

/*====================
   EXAMPLES / DEMOS
 *====================*/
#define LV_BUILD_EXAMPLES    0
#define LV_USE_DEMO_WIDGETS  0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS   0
#define LV_USE_DEMO_MUSIC    0

#endif /* LV_CONF_H */
