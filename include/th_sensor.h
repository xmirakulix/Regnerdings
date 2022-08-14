#include <Wire.h>

const int ADDRESS = 0x40;

void sensor_init(uint8_t sda, uint8_t scl)
{
    Serial.printf("Wire to %d, %d on address %d\n", sda, scl, ADDRESS);
    Wire.begin(sda, scl);
    delay(100);
    Wire.beginTransmission(ADDRESS);
    Wire.endTransmission();
}

int read_temperature()
{
    double temperature;
    int low_byte, high_byte, raw_data = 0;

    // send command of initiating temperature measurement
    Wire.beginTransmission(ADDRESS);
    Wire.write(0xE3);
    Wire.endTransmission();

    // read data of temperature
    Wire.requestFrom(ADDRESS, 2);
    if (Wire.available() <= 2)
    {
        high_byte = Wire.read();
        low_byte = Wire.read();
        high_byte = high_byte << 8;
        raw_data = high_byte + low_byte;
    }

    temperature = (175.72 * raw_data) / 65536;
    temperature = temperature - 46.85;

    return temperature;
}

int read_humidity()
{
    double humidity, raw_data_1 = 0, raw_data_2 = 0;
    int low_byte, high_byte, container;

    // send command of initiating relative humidity measurement
    Wire.beginTransmission(ADDRESS);
    Wire.write(0xE5);
    Wire.endTransmission();

    // read data of relative humidity
    Wire.requestFrom(ADDRESS, 2);
    if (Wire.available() <= 2)
    {
        high_byte = Wire.read();
        container = high_byte / 100;
        high_byte = high_byte % 100;
        low_byte = Wire.read();
        raw_data_1 = container * 25600;
        raw_data_2 = high_byte * 256 + low_byte;
    }

    raw_data_1 = (125 * raw_data_1) / 65536;
    raw_data_2 = (125 * raw_data_2) / 65536;
    humidity = raw_data_1 + raw_data_2;
    humidity = humidity - 6;

    return humidity;
}
