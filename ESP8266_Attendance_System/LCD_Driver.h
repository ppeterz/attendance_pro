#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "Config.h"

// HAL wrapper around the I2C LCD. Application code calls showLine()/
// showTwoLines() — it never touches LiquidCrystal_I2C directly.
class LCD_Driver {
public:
    void begin();
    void showTwoLines(const String &line1, const String &line2);
    void showLine(uint8_t row, const String &text);
    void clear();

private:
    LiquidCrystal_I2C _lcd{LCD_I2C_ADDR, LCD_COLS, LCD_ROWS};
    void _writePadded(uint8_t row, const String &text);
};

#endif // LCD_DRIVER_H
