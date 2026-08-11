#include "LCD_Driver.h"

void LCD_Driver::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();
}

void LCD_Driver::_writePadded(uint8_t row, const String &text) {
    String padded = text;
    while (padded.length() < LCD_COLS) padded += " ";
    if (padded.length() > LCD_COLS) padded = padded.substring(0, LCD_COLS);
    _lcd.setCursor(0, row);
    _lcd.print(padded);
}

void LCD_Driver::showTwoLines(const String &line1, const String &line2) {
    _writePadded(0, line1);
    _writePadded(1, line2);
}

void LCD_Driver::showLine(uint8_t row, const String &text) {
    _writePadded(row, text);
}

void LCD_Driver::clear() {
    _lcd.clear();
}
