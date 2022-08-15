#include <Wire.h>
#include <hd44780.h>                       // main hd44780 header
#include <hd44780ioClass/hd44780_I2Cexp.h> // i2c expander i/o class header

// LCD object: auto locate & auto config expander chip
hd44780_I2Cexp m_Lcd;

bool m_IsLcdBacklight = true;        // is the backlight on?
unsigned long m_LastBacklightOn = 0; // time of last backlight on
unsigned int m_LastEditChar = 0;

const char m_EditableChars[] = {'x', 0xFF};

char getEditChar()
{
    if (++m_LastEditChar >= sizeof(m_EditableChars))
    {
        m_LastEditChar = 0;
    }
    return m_EditableChars[m_LastEditChar];
}

void enableLcdBacklight()
{
    m_LastBacklightOn = millis();
    m_Lcd.backlight();
    m_IsLcdBacklight = true;
}

void disableLcdBacklight()
{
    m_Lcd.noBacklight();
    m_IsLcdBacklight = false;
}

void handleLcdBacklight()
{
    if (m_IsLcdBacklight && (millis() - m_LastBacklightOn) > 10 * 1000)
    {
        disableLcdBacklight();
    }
}

void setupLcd()
{
    int status = m_Lcd.begin(16, 2);
    if (status) // non zero status means it was unsuccesful
    {
        // hd44780 has a fatalError() routine that blinks an led if possible
        // begin() failed so blink error code using the onboard LED if possible
        Serial.println(F("Failed to initialize LCD"));
        hd44780::fatalError(status); // does not return

        m_Lcd.lineWrap();
        m_Lcd.clear();
        m_Lcd.noCursor();
        enableLcdBacklight();
    }
}
