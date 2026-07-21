// Custom TFT_eSPI User Setup for ESP32-S3 + 240x320 Display
#define USER_SETUP_LOADED 1

#define ILI9341_DRIVER

// SPI pins
#define TFT_CS   2
#define TFT_DC   1
#define TFT_MOSI 47
#define TFT_SCLK 21
#define TFT_RST  -1
#define TFT_BL   14

// SPI configuration - NO HSPI on ESP32-S3! Use default FSPI
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000

// Display orientation
#define TFT_WIDTH  320
#define TFT_HEIGHT 240

// Backlight
#define TFT_BACKLIGHT_ON HIGH

// Font
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

// Smooth font
#define SMOOTH_FONT
