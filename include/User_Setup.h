// Custom TFT_eSPI User Setup for ESP32-S3 + 240x320 Display
#define USER_SETUP_LOADED 1

// Display driver - ILI9341 (common 320x240, with swap_xy becomes 240x320)
#define ILI9341_DRIVER

// SPI pins
#define TFT_CS   2
#define TFT_DC   1
#define TFT_MOSI 47
#define TFT_SCLK 21
#define TFT_RST  -1  // GPIO_NUM_NC = not connected
#define TFT_BL   14

// SPI configuration
#define TFT_SPI_MODE 0
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

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

// Use SPI DMA
#define USE_HSPI_PORT
#define TFT_SPI_PORT HSPI
