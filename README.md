# Tiny Thermal Cam
## Parts

- [ESP32](https://fr.aliexpress.com/item/1005006476877078.html)  
- [ST7735 Screen](https://www.amazon.fr/dp/B07BY2W1TQ)  
- [Lepton FLIR Breakout Board v2](https://mou.sr/3UNaF37)  
- [Lepton FLIR 3.1R](https://mou.sr/416S7OK)  

## Pinout

![Wiring](Schéma.png)

| ESP32 Pin | Lepton Pin | Function  |
|-----------|------------|-----------|
| D17       | Pin 5      | SDA       |
| D15       | Pin 8      | SCL       |
| D22       | Pin 10     | CS        |
| D16       | Pin 7      | SCK       |
| D19       | Pin 12     | MISO      |
| GND       | GND        | Ground    |
| 3V3       | Power In   | Power     |

| ESP32 Pin | ST7735 Pin | Function        |
|-----------|------------|-----------------|
| 3V3       | VCC        | Power           |
| GND       | GND        | Ground          |
| D5        | CS         | Chip Select     |
| D2        | RST        | Reset           |
| D4        | A0         | Command/Data    |
| D23       | SDA        | Data    (MOSI)  |
| D18       | SCK        | SPI Clock       |
| 3V3       | LED        | Backlight       |
