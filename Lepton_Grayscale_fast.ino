/* Used with an ESP-WROOM-32 dev board to interface with a FLIR Lepton 3.5 dev module. */

#include <SPI.h>
#include <Wire.h>

// NOTE: Within the config file, the SPI frequency for this library must be set to 20 MHz. Higher frequencies
//       have caused issues.
#include <TFT_eSPI.h>

#define MISO_PIN      19
#define MOSI_PIN      23 // This is ignored by the module and can be left N/C
#define SCK_PIN       16
#define FLIR_NCS_PIN  22

static const int spiClk = 20000000; // 20 MHz, min is 2.2, max 20 for the Lepton 3.5 module.

// SPI bus which will implement the Lepton VOSPI
SPIClass * hspi = NULL;

#define I2C_SDA_PIN   17
#define I2C_SCL_PIN   15
#define ADDRESS       0x2A

#define VOSPI_FRAME_SIZE  (164)
uint16_t lepton_frame_packet[VOSPI_FRAME_SIZE / 2];
uint16_t lepton_frame_segment[60][VOSPI_FRAME_SIZE / 2]; // 60 packets per segment

// Palette Rainbow (LUT de 256 couleurs)
uint16_t rainbowLUT[256];


//defining variables related with the image
int image_index;
#define image_x (160)
#define image_y (120)
uint16_t image[image_x][image_y];

TFT_eSPI ips = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&ips);

double max_temp = 0;
double min_temp = 0;

uint16_t ironBlackLUT[256];

void initIronBlackLUT() {
  for (int i = 0; i < 256; i++) {
    // i = intensité de 0 (noir) à 255 (blanc)
    uint8_t v = i;
    // Convertir en RGB565
    ironBlackLUT[i] = ips.color565(v, v, v);
  }
}


void leptonSync(void){
  int i;
  int data = 0x0f00;

  digitalWrite(FLIR_NCS_PIN, HIGH);
  delay(300); // Waits for the required time indicated by datasheet, ensures a timeout of VoSPI I/F
  
  while ((data & (0x0f00)) == 0x0f00){ // I changed this because I don't think it was right

    // begin checking the first returned data packet, should be a discard packet

    // Start of VoSPI transfer for first packet, reading for discard packet
    digitalWrite(FLIR_NCS_PIN, LOW);
    hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3)); // these arent included in others' code
    data = hspi->transfer(0x00) << 8;
    data |= hspi->transfer(0x00);
    // hspi->endTransaction();
    digitalWrite(FLIR_NCS_PIN, HIGH);

    // Process and discard the remaining data in the packet
    for (i = 0; i < ((VOSPI_FRAME_SIZE - 2) / 2); i++){
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->transfer(0x00); // unused garbage data
      hspi->transfer(0x00); // unused garbage data
      digitalWrite(FLIR_NCS_PIN, HIGH);
    }
    
    hspi->endTransaction();
  }
}

void captureImage( void ) {
  
  hspi->setDataMode(SPI_MODE3);
  hspi->setFrequency(16000000);

  // leptonSync();
  // delay(50);
  
  bool collectedSegments[4];
  collectedSegments[0] = false;
  collectedSegments[1] = false;
  collectedSegments[2] = false;
  collectedSegments[3] = false;
  uint8_t lastFoundSegment = 0;
  uint8_t segmentsRead = 0;

  while(!collectedSegments[0] | !collectedSegments[1] | !collectedSegments[2] | !collectedSegments[3]){

    // Get 60 valid packets per segment
    // digitalWrite(13, LOW); // Debug indicator
    for (int packetNumber = 0; packetNumber < 60; packetNumber++){
      do {
        digitalWrite(FLIR_NCS_PIN, LOW);  
        hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
        byte dummyBuffer[164];
        hspi->transfer(dummyBuffer, sizeof(dummyBuffer));

        for (int i = 0; i < 164l; i += 2) {
          lepton_frame_packet[i/2] = dummyBuffer[i] << 8 | dummyBuffer[i+1];
        }
          
        hspi->endTransaction();
        digitalWrite(FLIR_NCS_PIN, HIGH);
      } while (((lepton_frame_packet[0] & 0x0f00) >> 8 == 0x0f)); // wait until a non-discard packet has been found

      // Load the packet into the segment
      for (int i = 0; i < VOSPI_FRAME_SIZE / 2; i++) {
        lepton_frame_segment[packetNumber][i] = lepton_frame_packet[i];
      }
    }
    // digitalWrite(13,HIGH); // Debug indicator

    // Load the collected segment into the image after it's all been captured
    
    int segmentNumber = (lepton_frame_segment[20][0] >> 12) & 0b0111; // This should give the segment number which is held at packet 20. The transmitted packets start at number 0.

    if (segmentNumber != 0){ // if the segment is number 0, ignore the segment
      
      // Serial.println(segmentNumber);
      // Makes sure that we get the segments in the right order
      if (segmentNumber == 2) {
        if (!collectedSegments[0]){ // We haven't found the first segment
          lastFoundSegment = 0;
          segmentsRead = 0;
          leptonSync();
          break;
        }
      } else if (segmentNumber == 3){
        if (!collectedSegments[0] && !collectedSegments[1]){
          lastFoundSegment = 0;
          segmentsRead = 0;
          leptonSync();
          break;
        }
      } else if (segmentNumber == 4){
        if (!collectedSegments[0] && !collectedSegments[1] && !collectedSegments[2]){
          lastFoundSegment = 0;
          segmentsRead = 0;
          leptonSync();
          break;
        }
      }
      

      segmentsRead++;
      if (segmentsRead > 4) { // This means that we're probably out of sync for some reason
        collectedSegments[0] = false;
        collectedSegments[1] = false;
        collectedSegments[2] = false;
        collectedSegments[3] = false;
        lastFoundSegment = 0;
        segmentsRead = 0;
        leptonSync();
        break;
      }

      if (segmentNumber > 4) {
        leptonSync();
        break;
      }
      collectedSegments[segmentNumber - 1] = true;

      for (int packetNumber = 0; packetNumber < 60; packetNumber++) {
        for (int px = 0; px < 80; px ++) {
          if (packetNumber % 2){
            // If the packet number is odd, put it on the right side of the image
            // Placement starts at X = 80 px
            image[80 + px][(segmentNumber - 1) * 30 + (int)(packetNumber/2)] = lepton_frame_segment[packetNumber][px + 2];
          } else {
            // Otherwise put it on the left side
            // Placement starts at X = 0 px
            image[px][(segmentNumber - 1) * 30 + (int)(packetNumber/2)] = lepton_frame_segment[packetNumber][px + 2];
          }
        }
      } 
    }
  }
  // Serial.println("Image Complete")ironBlack;
  hspi->setDataMode(SPI_MODE0);
  hspi->setFrequency(20000000);
}

void displayImage(void) {
  double min, max;
  max = min = image[0][0];

  // Recherche min/max dans toute l'image
  for (int i = 0; i < image_x; i++) {
    for (int j = 0; j < image_y; j++) {
      if (image[i][j] > max) max = image[i][j];
      if (image[i][j] < min) min = image[i][j];
    }
  }

  max_temp = (max / 100) - 273.15;
  min_temp = (min / 100) - 273.15;

for (int i = 0; i < image_x; i++) {
  for (int j = 0; j < image_y; j++) {
    double norm = (image[i][j] - min) / (max - min);  // [0.0 - 1.0]
    uint8_t index = constrain(norm * 255.0, 0, 255);  // 0-255
    uint16_t color = ironBlackLUT[index];
    spr.drawPixel(i, j, color);
  }
}


  // Bande noire en bas
  spr.fillRect(0, image_y - 8, image_x, 8, TFT_BLACK);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextSize(1);
  spr.setCursor(2, image_y - 7);

  char buffer[64];
  snprintf(buffer, sizeof(buffer), "Min: %.1fC Max: %.1fC", min_temp, max_temp);
  spr.print(buffer);

  spr.pushSprite(0, 0);
}



void printBin(byte aByte) {
  for (int8_t aBit = 7; aBit >= 0; aBit--) {
    Serial.write(bitRead(aByte, aBit) ? '1' : '0');
  }
}

void transferImage( void ) {
  int divider = 4;
  for(int i=0; i < image_y / divider; i++){
    for(int j=0; j < image_x / divider; j++){
      Serial.print((int)image[j * divider][i * divider], DEC);
      Serial.print("\t");
    }
    Serial.print("\n");
  }
  Serial.println("Transfer Complete");
}

void debugPackets( byte * packet ) {
  if (((packet[0] & 0x0f) != 0x0f) & (packet[0] != 0x00)){
    // Serial.print("Segment number:\t");
    // Serial.print((lepton_frame_packet[0] & 0xf0) >> 4, HEX);
    // Serial.print("\tPacket Number:\t");
    // Serial.print(lepton_frame_packet[1], DEC);
    printBin(packet[0]);
    Serial.print("\t");
    printBin(packet[1]);
    Serial.println("");
  }
}

int readRegister( unsigned int reg ) {
  int reading = 0;
  setRegister(reg);

  Wire.requestFrom(ADDRESS, 2);

  reading = Wire.read();  // receive high byte (overwrites previous reading)
  // Serial.println(reading);
  reading = reading << 8;    // shift high byte to be high 8 bits

  reading |= Wire.read(); // receive low byte as lower 8 bits
  // Serial.print("reg:");
  // Serial.print(reg);
  // Serial.print("==0x");
  // Serial.print(reading, HEX);
  // Serial.print(" binary:");
  // Serial.println(reading, BIN);
  return reading;
}

void setRegister( unsigned int reg ) {
  byte error;
  Wire.beginTransmission(ADDRESS); // transmit to device #4
  Wire.write(reg >> 8 & 0xff);
  Wire.write(reg & 0xff);            // sends one byte

  error = Wire.endTransmission();    // stop transmitting
  // if (error != 0){
  //   Serial.print("Error =");
  //   Serial.println(error);
  // }
}




void setup() {

  // Setup the hspi bus
  hspi = new SPIClass(HSPI);
  hspi->begin(SCK_PIN, MISO_PIN, MOSI_PIN, FLIR_NCS_PIN);
  hspi->setDataMode(SPI_MODE3);
  pinMode(FLIR_NCS_PIN, OUTPUT);

  // Initialize serial port
  Serial.begin(115200);

  // Add delay needed for lepton setup
  delay(7000);

  // Set up i2c on alternate pins, 400kHz baud
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  bool isBusy = true;
  do {
    int status = readRegister(0x0002);
    if(!(status & 0b100) & (status & 0b1)){
      Serial.println("I2C is busy.");
      delay(1000);
    } else {
      isBusy = false;
    }
  } while (isBusy);

  ips.init();
  ips.setRotation(3);

  spr.createSprite(image_x, image_y); // Will have to update and show this twice

  ips.invertDisplay( true );
  ips.fillScreen(TFT_BLACK);
initIronBlackLUT();

  leptonSync();
}

void loop() {

  captureImage();

  displayImage();

  // transferImage();
  //delay(140); // This needs to be looked into, we're having stability issues. Likely switch to timer-based frame capture
}
