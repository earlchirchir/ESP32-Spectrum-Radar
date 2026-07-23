#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* Memory management */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB
#define LV_MEM_SIZE (64 * 1024U)

/* Hal settings */
#define LV_DEF_REFR_PERIOD  33      /*[ms]*/
#define LV_DPI_DEF 130     /*[px/inch]*/

/* Drawing settings */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT    1
#define LV_DRAW_SW_COMPLEX          1

/* Features */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0

/* Fonts */
#define LV_FONT_MONTSERRAT_8     1
#define LV_FONT_MONTSERRAT_10    1
#define LV_FONT_MONTSERRAT_12    1
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_18    1
#define LV_FONT_MONTSERRAT_20    1

/* Use the default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Widgets */
#define LV_USE_BUTTON           1
#define LV_USE_LABEL            1
#define LV_USE_SLIDER           1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1
#define LV_USE_LIST             1
#define LV_USE_CANVAS           1
#define LV_USE_LINE             1
#define LV_USE_BAR              1

#endif
