#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// ESP32-2432S022C Hardware Pin Definitions
// ============================================================================

// Display ST7789 (240x320 SPI)
#define TFT_MOSI      13
#define TFT_MISO      12
#define TFT_SCLK      14
#define TFT_CS        15
#define TFT_DC        2
#define TFT_RST       -1
#define TFT_BL        27

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Touch Screen Capacitive (I2C)
#define TOUCH_SDA     33
#define TOUCH_SCL     32
#define TOUCH_INT     21
#define TOUCH_RST     25

// Onboard Peripherals
#define LDR_PIN       34    // Ambient Light Sensor (Analog ADC1_CH6)
#define RGB_LED_R     4     // Red LED channel
#define RGB_LED_G     16    // Green LED channel
#define RGB_LED_B     17    // Blue LED channel
#define BOOT_BUTTON   0     // Boot / User Switch

// PWM Backlight Channels
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ    5000
#define BL_PWM_RES     8

// Status LED PWM Channels
#define LED_PWM_FREQ   5000
#define LED_PWM_RES    8
#define LED_R_CHANNEL  1
#define LED_G_CHANNEL  2
#define LED_B_CHANNEL  3

// Scanning Parameters
#define MAX_WIFI_DEVICES 30
#define MAX_BLE_DEVICES  20
#define WIFI_SCAN_INTERVAL_MS 4000
#define BLE_SCAN_INTERVAL_MS  5000

#endif // CONFIG_H
