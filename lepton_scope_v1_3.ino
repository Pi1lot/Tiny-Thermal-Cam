/* Lepton-scope v1
 * ESP-WROOM-32 + FLIR Lepton 3.5 + ST7735
 */

#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#define MISO_PIN      19
#define MOSI_PIN      23
#define SCK_PIN       16
#define FLIR_NCS_PIN  22

static const int spiClk = 20000000;

SPIClass * hspi = NULL;

#define I2C_SDA_PIN   17
#define I2C_SCL_PIN   15
#define ADDRESS       0x2A

#define VOSPI_FRAME_SIZE  (164)
uint16_t lepton_frame_packet[VOSPI_FRAME_SIZE / 2];
uint16_t lepton_frame_segment[60][VOSPI_FRAME_SIZE / 2];

#define image_x (160)
#define image_y (120)
uint16_t image[image_x][image_y];

TFT_eSPI ips = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&ips);

uint16_t grayLUT[256];

#define POT_PIN       34
float pot_ratio = 0.0f;

unsigned long lastFrameTime = 0;
float fps = 0;

// ─── Couleurs palette boot ────────────────────────────────────────────────────
#define C_BG      0x0000  // Noir
#define C_ACCENT  0x07E0  // Vert vif (BGR = vert)
#define C_DIM     0x02A0  // Vert sombre
#define C_WHITE   0xFFFF
#define C_GRAY    0x8410
#define C_RED     0x001F  // Rouge BGR
#define C_ORANGE  0x04FF  // Orange BGR approx

// ─── LUT ──────────────────────────────────────────────────────────────────────

void initGrayLUT() {
  for (int i = 0; i < 256; i++) {
    uint8_t r = i >> 3;
    uint8_t g = i >> 2;
    uint8_t b = i >> 3;
    grayLUT[i] = (r << 11) | (g << 5) | b;
  }
}

// ─── Potentiomètre ────────────────────────────────────────────────────────────

float readPotRatio() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(POT_PIN);
  return (sum / 16.0f) / 4095.0f;
}

// ─── Viseur ───────────────────────────────────────────────────────────────────

void drawCrosshair() {
  int centerX = image_x / 2;
  int centerY = image_y / 2;
  int size = 10;
  int gap = 3;
  uint16_t color = 0x001F;
  spr.drawLine(centerX - size, centerY, centerX - gap, centerY, color);
  spr.drawLine(centerX + gap,  centerY, centerX + size, centerY, color);
  spr.drawLine(centerX, centerY - size, centerX, centerY - gap, color);
  spr.drawLine(centerX, centerY + gap,  centerX, centerY + size, color);
  spr.drawPixel(centerX, centerY, color);
}

// ─── Ecran de boot ────────────────────────────────────────────────────────────

void drawBootScreen() {
  ips.fillScreen(C_BG);

  // Bordure extérieure
  ips.drawRect(0, 0, 160, 128, C_DIM);
  ips.drawRect(2, 2, 156, 124, C_DIM);

  // Logo : dessin d'un viseur thermique stylisé centré en haut
  int cx = 80, cy = 38;
  // Cercle extérieur
  ips.drawCircle(cx, cy, 22, C_ACCENT);
  // Cercle intérieur
  ips.drawCircle(cx, cy, 10, C_ACCENT);
  // Croix
  ips.drawFastHLine(cx - 22, cy, 10, C_ACCENT);
  ips.drawFastHLine(cx + 13, cy, 10, C_ACCENT);
  ips.drawFastVLine(cx, cy - 22, 10, C_ACCENT);
  ips.drawFastVLine(cx, cy + 13, 10, C_ACCENT);
  // Point central chaud
  ips.fillCircle(cx, cy, 3, C_RED);
  // Petits arcs décoratifs aux coins
  ips.drawCircle(cx, cy, 16, C_DIM);

  // Titre
  ips.setTextColor(C_ACCENT, C_BG);
  ips.setTextSize(1);
  ips.setCursor(28, 66);
  ips.print("LEPTON-SCOPE");
  ips.setTextColor(C_DIM, C_BG);
  ips.setCursor(62, 76);
  ips.print("v1.0");

  // Ligne séparatrice
  ips.drawFastHLine(10, 86, 140, C_DIM);

  // Infos bas
  ips.setTextColor(C_GRAY, C_BG);
  ips.setCursor(18, 92);
  ips.print("FLIR Lepton 3.1R");
  ips.setCursor(30, 102);
  ips.print("ESP32 @ 240MHz");

  // Ligne de statut (sera mise à jour)
  ips.drawFastHLine(10, 113, 140, C_DIM);
  ips.setTextColor(C_DIM, C_BG);
  ips.setCursor(8, 117);
  ips.print("Initialisation...");
}

void updateBootStatus(const char* msg, uint16_t color = C_ACCENT) {
  // Efface uniquement la zone de statut
  ips.fillRect(4, 114, 152, 12, C_BG);
  ips.setTextColor(color, C_BG);
  ips.setTextSize(1);
  // Centrage approximatif (6px par char)
  int len = strlen(msg);
  int x = (160 - len * 6) / 2;
  ips.setCursor(x, 117);
  ips.print(msg);
}

// Barre de progression animée pendant la sync
void animateSyncBar(int step, int total) {
  int barX = 10, barY = 108, barW = 140, barH = 4;
  ips.drawRect(barX, barY, barW, barH, C_DIM);
  int filled = (step * (barW - 2)) / total;
  ips.fillRect(barX + 1, barY + 1, filled, barH - 2, C_ACCENT);
}

// ─── I2C ──────────────────────────────────────────────────────────────────────

int readRegister(unsigned int reg) {
  int reading = 0;
  setRegister(reg);
  Wire.requestFrom(ADDRESS, 2);
  reading = Wire.read();
  reading = reading << 8;
  reading |= Wire.read();
  return reading;
}

void setRegister(unsigned int reg) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg >> 8 & 0xff);
  Wire.write(reg & 0xff);
  Wire.endTransmission();
}

// ─── Lepton ───────────────────────────────────────────────────────────────────

bool waitForLeptonReady(int timeout_ms = 15000) {
  Serial.println("Attente du Lepton...");
  unsigned long start = millis();
  int dot = 0;
  while (millis() - start < timeout_ms) {
    int status = readRegister(0x0002);
    if (!(status & 0x0001) && (status & 0x0004)) {
      Serial.println("Lepton pret !");
      return true;
    }
    animateSyncBar(dot % 20, 20);
    dot++;
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTimeout - Lepton non pret");
  return false;
}

void leptonSync(void) {
  Serial.println("Synchronisation Lepton...");
  digitalWrite(FLIR_NCS_PIN, HIGH);
  delay(200);
  for (int attempt = 0; attempt < 5; attempt++) {
    digitalWrite(FLIR_NCS_PIN, HIGH);
    delay(185);
    for (int i = 0; i < 300; i++) {
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
      uint16_t header = (hspi->transfer(0x00) << 8) | hspi->transfer(0x00);
      for (int j = 0; j < (VOSPI_FRAME_SIZE - 2) / 2; j++) {
        hspi->transfer16(0x00);
      }
      hspi->endTransaction();
      digitalWrite(FLIR_NCS_PIN, HIGH);
      if ((header & 0x0F00) != 0x0F00) {
        Serial.println("Sync OK!");
        return;
      }
    }
  }
  Serial.println("ERREUR: Sync impossible");
}

void captureImage(void) {
  bool collectedSegments[4] = {false, false, false, false};
  uint8_t segmentsRead = 0;

  while (!collectedSegments[0] || !collectedSegments[1] || !collectedSegments[2] || !collectedSegments[3]) {

    for (int packetNumber = 0; packetNumber < 60; packetNumber++) {
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
      byte dummyBuffer[164];
      hspi->transferBytes(NULL, dummyBuffer, VOSPI_FRAME_SIZE);
      hspi->endTransaction();
      digitalWrite(FLIR_NCS_PIN, HIGH);

      for (int i = 0; i < VOSPI_FRAME_SIZE; i += 2) {
        lepton_frame_packet[i/2] = (dummyBuffer[i] << 8) | dummyBuffer[i+1];
      }

      if ((lepton_frame_packet[0] & 0x0f00) == 0x0f00) {
        packetNumber--;
        continue;
      }

      memcpy(lepton_frame_segment[packetNumber], lepton_frame_packet, VOSPI_FRAME_SIZE);
    }

    int segmentNumber = (lepton_frame_segment[20][0] >> 12) & 0b0111;

    if (segmentNumber != 0) {
      if (segmentNumber == 2) {
        if (!collectedSegments[0]) { segmentsRead = 0; leptonSync(); break; }
      } else if (segmentNumber == 3) {
        if (!collectedSegments[0] || !collectedSegments[1]) { segmentsRead = 0; leptonSync(); break; }
      } else if (segmentNumber == 4) {
        if (!collectedSegments[0] || !collectedSegments[1] || !collectedSegments[2]) { segmentsRead = 0; leptonSync(); break; }
      }

      segmentsRead++;
      if (segmentsRead > 4) {
        collectedSegments[0] = false;
        collectedSegments[1] = false;
        collectedSegments[2] = false;
        collectedSegments[3] = false;
        segmentsRead = 0;
        leptonSync();
        break;
      }

      if (segmentNumber > 4) { leptonSync(); break; }

      collectedSegments[segmentNumber - 1] = true;

      for (int packetNumber = 0; packetNumber < 60; packetNumber++) {
        int yPos = (segmentNumber - 1) * 30 + (packetNumber / 2);
        if (packetNumber % 2) {
          for (int px = 0; px < 80; px++) image[80 + px][yPos] = lepton_frame_segment[packetNumber][px + 2];
        } else {
          for (int px = 0; px < 80; px++) image[px][yPos] = lepton_frame_segment[packetNumber][px + 2];
        }
      }
    }
  }
}

// ─── Affichage ────────────────────────────────────────────────────────────────

void displayImage(void) {
  double frame_min = 65535, frame_max = 0;
  for (int j = 0; j < image_y; j++) {
    for (int i = 0; i < image_x; i++) {
      uint16_t val = image[i][j];
      if (val > frame_max) frame_max = val;
      if (val < frame_min) frame_min = val;
    }
  }

  double range = frame_max - frame_min;
  if (range < 100) range = 100;

  double threshold = frame_min + pot_ratio * range;
  double lut_range = frame_max - threshold;
  if (lut_range < 1) lut_range = 1;

  for (int j = 0; j < image_y; j++) {
    for (int i = 0; i < image_x; i++) {
      double v = image[i][j];
      uint8_t grayValue;
      if (v <= threshold) {
        grayValue = 0;
      } else {
        double norm = (v - threshold) / lut_range;
        norm = constrain(norm, 0.0, 1.0);
        grayValue = (uint8_t)(norm * 255.0);
      }
      spr.drawPixel(i, j, grayLUT[grayValue]);
    }
  }

  drawCrosshair();

  // Sprite occupe exactement les 120px de l'image, pas de superposition
  spr.pushSprite(0, 0);

  // Les 8px restants de l'écran (lignes 120-127) : texte direct sur ips
  ips.fillRect(0, image_y, image_x, 8, 0x0000);
  ips.setTextColor(C_ACCENT, 0x0000);
  ips.setTextSize(1);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.1f FPS  thr:%.0f%%", fps, pot_ratio * 100.0f);
  ips.setCursor(2, image_y + 1);
  ips.print(buffer);

  unsigned long now = millis();
  if (lastFrameTime > 0) {
    float frameTime = (now - lastFrameTime) / 1000.0;
    fps = fps * 0.9 + (1.0 / frameTime) * 0.1;
  }
  lastFrameTime = now;
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  setCpuFrequencyMhz(240);

  hspi = new SPIClass(HSPI);
  hspi->begin(SCK_PIN, MISO_PIN, MOSI_PIN, FLIR_NCS_PIN);
  hspi->setDataMode(SPI_MODE3);
  hspi->setFrequency(20000000);
  hspi->setHwCs(false);

  pinMode(FLIR_NCS_PIN, OUTPUT);
  digitalWrite(FLIR_NCS_PIN, HIGH);

  pinMode(POT_PIN, INPUT);
  analogReadResolution(12);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== LEPTON-SCOPE v1 ===");

  // Ecran allumé en premier pour le boot screen
  ips.init();
  ips.setRotation(3);
  ips.invertDisplay(false);
  drawBootScreen();
  updateBootStatus("Demarrage...");
  delay(800); // laisser le temps d'admirer :)

  // I2C + attente Lepton
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  updateBootStatus("Attente Lepton...");
  delay(5000); // temps de boot Lepton

  if (!waitForLeptonReady()) {
    updateBootStatus("ERREUR LEPTON !", C_RED);
    Serial.println("ERREUR: Lepton non detecte");
    while(1) delay(1000);
  }

  updateBootStatus("Sync VoSPI...", C_ORANGE);

  // Boot robuste : on boucle jusqu'à avoir un segment valide
  bool bootOk = false;
  int attempt = 0;
  while (!bootOk) {
    attempt++;
    char msg[24];
    snprintf(msg, sizeof(msg), "Sync VoSPI... (%d)", attempt);
    updateBootStatus(msg, C_ORANGE);
    animateSyncBar(attempt % 20, 20);

    leptonSync();
    delay(100);

    for (int pkt = 0; pkt < 120 && !bootOk; pkt++) {
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
      static byte probeBuf[164];
      hspi->transferBytes(NULL, probeBuf, VOSPI_FRAME_SIZE);
      hspi->endTransaction();
      digitalWrite(FLIR_NCS_PIN, HIGH);
      if ((probeBuf[0] & 0x0F) == 0x0F) continue;
      uint16_t w0 = (probeBuf[0] << 8) | probeBuf[1];
      int seg = (w0 >> 12) & 0x07;
      if (seg >= 1 && seg <= 4) {
        bootOk = true;
      }
    }
  }

  // Barre pleine
  animateSyncBar(20, 20);
  updateBootStatus("PRET !", C_ACCENT);
  delay(600);

  // Animation balayage : deux lignes du centre vers les bords puis révèle l'image
  ips.fillScreen(C_BG);
  int cx = image_x / 2;
  for (int x = 0; x <= cx; x++) {
    // Ligne verte qui s'étend du centre vers la gauche et la droite
    ips.drawFastVLine(cx - x, 0, image_y, C_ACCENT);
    ips.drawFastVLine(cx + x, 0, image_y, C_ACCENT);
    // Efface derrière pour un effet de balayage propre
    if (x > 2) {
      ips.drawFastVLine(cx - x + 2, 0, image_y, C_BG);
      ips.drawFastVLine(cx + x - 2, 0, image_y, C_BG);
    }
    delay(4);
  }
  ips.fillScreen(C_BG);

  // Init sprite et LUT
  initGrayLUT();
  spr.createSprite(image_x, image_y);
  spr.fillSprite(0x0000);

  Serial.println("Systeme pret !");
  Serial.print("CPU: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
  pot_ratio = readPotRatio();
  captureImage();
  displayImage();
}
