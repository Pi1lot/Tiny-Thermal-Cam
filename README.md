# Tiny Thermal Cam

<img width="1040" height="753" alt="Capture d&#39;écran 2026-02-24 005450" src="https://github.com/user-attachments/assets/7e875ce1-5263-42c0-9357-a34346be2c51" />

Huge thanks to [Jacob-Lundstrom](https://github.com/Jacob-Lundstrom/ESP-FLIR) for its repository. It was the only one that I could get working properly with my Lepton camera.

## Parts

- [ESP32](https://fr.aliexpress.com/item/1005006476877078.html)  
- [ST7735 Screen](https://www.amazon.fr/dp/B07BY2W1TQ)  
- [Lepton FLIR Breakout Board v2](https://mou.sr/3UNaF37)  
- [Lepton FLIR 3.1R](https://mou.sr/416S7OK)  

## Pinout

> [!WARNING]
> Pinout schema doesn't include the potentiometer yet

<img width="1827" height="1440" alt="Schéma" src="https://github.com/user-attachments/assets/b52777e2-eb24-4111-a921-af870827650b" />


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

## Code

Depending if you prefer accuracy over speed, you can choose different programms. [Lepton_Grayscale_v6_completed.ino](https://github.com/Pi1lot/Tiny-Thermal-Cam/blob/main/Lepton_Grayscale_v6_completed.ino) is the latest version with proper handling of Lepton syncing at boot and implement a scale potentiometer to adjust contrast if used in a wide range of temperature. [Lepton_Grayscale_v1.ino](https://github.com/Pi1lot/Tiny-Thermal-Cam/blob/main/Lepton_Grayscale_v1.ino) is the most accurate regarding temperatures.

## Demo

![lepton_scope_working](https://github.com/user-attachments/assets/d59a8f36-974e-44d5-af45-406cbf0b1a73)

