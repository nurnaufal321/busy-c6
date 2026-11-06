// Waveshare ESP32-C6-Touch-LCD-1.9, from Waveshare's 08_FactoryProgram demo.
#pragma once

#define PIN_LCD_MOSI 4
#define PIN_LCD_CLK 5
#define PIN_LCD_DC 6
#define PIN_LCD_CS 7
#define PIN_LCD_RST 14
#define PIN_LCD_BL 15
#define PIN_I2C_SCL 8
#define PIN_I2C_SDA 18

#define I2C_ADDR_TOUCH 0x15
#define I2C_ADDR_IMU 0x6B

// The bar's picture.
#define LED_W 72
#define LED_H 16
#define LED_PIXELS (LED_W * LED_H)
