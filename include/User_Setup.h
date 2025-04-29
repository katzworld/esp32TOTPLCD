#define USER_SETUP_LOADED

// Define ST7789 driver
#define ST7789_DRIVER

// Define screen dimensions
#define TFT_WIDTH 170
#define TFT_HEIGHT 320

// Define ESP32-S3 pins for the display
#define TFT_MISO -1
#define TFT_MOSI 13
#define TFT_SCLK 12
#define TFT_CS 10
#define TFT_DC 11
#define TFT_RST 1
#define TFT_BL 14

// Define touch pin (this will eliminate the warning)
#define TOUCH_CS -1 // Set to -1 if touch is not used

// Backlight control
#define TFT_BACKLIGHT_ON HIGH

// Load the fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// Set SPI frequency
#define SPI_FREQUENCY 40000000