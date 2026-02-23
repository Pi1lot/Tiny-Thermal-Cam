/* Used with an ESP-WROOM-32 dev board to interface with a FLIR Lepton 3.5 dev module. 
 * Version optimisée pour un rafraîchissement maximal


Using a potentiometer on GPIO34 to adjust LUT range -> better results in outdoor environnement to account for the sky coldness

 
 */

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

//defining variables related with the image
int image_index;
#define image_x (160)
#define image_y (120)
uint16_t image[image_x][image_y];

TFT_eSPI ips = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&ips);

// Variables pour AGC glissant
double running_min = 27315; // 0°C en centikelvins
double running_max = 31315; // 40°C
const float AGC_ALPHA = 0.15; // Facteur de lissage (0.1 = lent, 0.3 = rapide)

// Variables de calibration pour corriger les températures
float temp_offset = -1.0;   // Offset à ajouter aux températures (en °C) - AJUSTEZ ICI
float temp_scale = 1.0;     // Facteur multiplicateur

// --- Potentiomètre percentile_low ---
#define POT_PIN 34

float percentile_low_min = 0.0;   // 0%
float percentile_low_max = 0.90;  // 30% max
int pot_raw = 0;
float percentile_low_percent = 10.0; // pour affichage (%)


// Clipping automatique des valeurs aberrantes (comme le ciel)
float percentile_low = 0.33;   // Ignore les 2% de valeurs les plus basses (ciel froid)
float percentile_high = 0.98;  // Ignore les 2% de valeurs les plus hautes
bool enable_auto_range = true; // Active le clipping automatique (RECOMMANDÉ)

// Plage minimale pour maintenir le contraste dans les scènes uniformes
float min_display_range_celsius = 10.0; // Force au moins 10°C de plage pour le contraste

// Surbrillance des zones chaudes
float hot_spot_threshold = 0.95;  // Seuil pour les zones très chaudes (95% = top 5%)
bool enable_hot_spot = true;      // Active/désactive la surbrillance verte

double max_temp = 0;
double min_temp = 0;

uint16_t ironBlackLUT[256];

// Buffer pour le rendu optimisé
uint16_t colorBuffer[image_x];

// Variables de performance
unsigned long lastFrameTime = 0;
float fps = 0;

void initIronBlackLUT() {
  // Palette niveaux de gris SIMPLE pour écran BGR
  // BLANC = CHAUD, NOIR = FROID
  for (int i = 0; i < 256; i++) {
    // On crée directement du gris en mettant R=G=B identiques
    // Comme le texte blanc/noir fonctionne, on utilise la même méthode
    uint16_t gray565;
    
    if (i == 0) {
      gray565 = 0x0000; // Noir pur
    } else if (i == 255) {
      gray565 = 0xFFFF; // Blanc pur
    } else {
      // Interpolation linéaire entre noir et blanc
      // On génère du gris en gardant R=G=B
      uint8_t val = i;
      uint8_t r = val >> 3;  // 5 bits
      uint8_t g = val >> 2;  // 6 bits
      uint8_t b = val >> 3;  // 5 bits
      
      // Assemblage standard RGB565
      gray565 = (r << 11) | (g << 5) | b;
    }
    
    ironBlackLUT[i] = gray565;
  }
  
  Serial.println("Palette niveaux de gris initialisee");
  Serial.print("LUT[0] (noir) = 0x");
  Serial.println(ironBlackLUT[0], HEX);
  Serial.print("LUT[128] (gris) = 0x");
  Serial.println(ironBlackLUT[128], HEX);
  Serial.print("LUT[255] (blanc) = 0x");
  Serial.println(ironBlackLUT[255], HEX);
}

void drawCrosshair() {
  int centerX = image_x / 2;
  int centerY = image_y / 2;
  int size = 10; // Taille du viseur
  int gap = 3;   // Espace au centre
  
  // Rouge standard (comme le rouge fonctionne, on garde cette valeur)
  uint16_t color = 0x001F; // Rouge pour écran BGR
  
  // Ligne horizontale gauche
  spr.drawLine(centerX - size, centerY, centerX - gap, centerY, color);
  // Ligne horizontale droite
  spr.drawLine(centerX + gap, centerY, centerX + size, centerY, color);
  
  // Ligne verticale haut
  spr.drawLine(centerX, centerY - size, centerX, centerY - gap, color);
  // Ligne verticale bas
  spr.drawLine(centerX, centerY + gap, centerX, centerY + size, color);
  
  // Point central
  spr.drawPixel(centerX, centerY, color);
}

bool waitForLeptonReady(int timeout_ms = 15000) {
  Serial.println("Attente du Lepton...");
  unsigned long start = millis();
  
  while (millis() - start < timeout_ms) {
    int status = readRegister(0x0002); // Status register
    
    // Bit 0 = busy, Bit 2 = boot status
    if (!(status & 0x0001) && (status & 0x0004)) {
      Serial.println("Lepton pret !");
      return true;
    }
    
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
  
  // Tentatives multiples de sync
  for (int attempt = 0; attempt < 5; attempt++) {
    digitalWrite(FLIR_NCS_PIN, HIGH);
    delay(185); // > 185ms pour forcer un timeout VoSPI
    
    for (int i = 0; i < 300; i++) { // Max 300 paquets
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
      
      uint16_t header = (hspi->transfer(0x00) << 8) | hspi->transfer(0x00);
      
      // Vider le reste du paquet
      for (int j = 0; j < (VOSPI_FRAME_SIZE - 2) / 2; j++) {
        hspi->transfer16(0x00);
      }
      
      hspi->endTransaction();
      digitalWrite(FLIR_NCS_PIN, HIGH);
      
      // Si on trouve un paquet valide (non-discard)
      if ((header & 0x0F00) != 0x0F00) {
        Serial.println("Sync OK!");
        return;
      }
    }
  }
  
  Serial.println("ERREUR: Sync impossible");
}

void captureImage(void) {
  
  hspi->setDataMode(SPI_MODE3);
  hspi->setFrequency(20000000); // 20MHz pour vitesse maximale
  
  bool collectedSegments[4] = {false, false, false, false};
  uint8_t segmentsRead = 0;

  while(!collectedSegments[0] || !collectedSegments[1] || !collectedSegments[2] || !collectedSegments[3]) {

    // Get 60 valid packets per segment
    for (int packetNumber = 0; packetNumber < 60; packetNumber++) {
      
      // UNE SEULE transaction SPI par paquet (OPTIMISATION MAJEURE)
      digitalWrite(FLIR_NCS_PIN, LOW);
      hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE3));
      
      // Transfert avec buffer temporaire
      byte dummyBuffer[164];
      hspi->transferBytes(NULL, dummyBuffer, VOSPI_FRAME_SIZE);
      
      hspi->endTransaction();
      digitalWrite(FLIR_NCS_PIN, HIGH);
      
      // Reconstruction correcte des uint16_t (big-endian)
      for (int i = 0; i < VOSPI_FRAME_SIZE; i += 2) {
        lepton_frame_packet[i/2] = (dummyBuffer[i] << 8) | dummyBuffer[i+1];
      }
      
      // Attendre un paquet valide
      if ((lepton_frame_packet[0] & 0x0f00) == 0x0f00) {
        packetNumber--; // Recommencer ce paquet
        continue;
      }

      // Copier dans le segment avec memcpy (plus rapide que boucle)
      memcpy(lepton_frame_segment[packetNumber], lepton_frame_packet, VOSPI_FRAME_SIZE);
    }

    // Load the collected segment into the image after it's all been captured
    
    int segmentNumber = (lepton_frame_segment[20][0] >> 12) & 0b0111;

    if (segmentNumber != 0) { // if the segment is number 0, ignore the segment
      
      // Makes sure that we get the segments in the right order
      if (segmentNumber == 2) {
        if (!collectedSegments[0]) {
          segmentsRead = 0;
          leptonSync();
          break;
        }
      } else if (segmentNumber == 3) {
        if (!collectedSegments[0] || !collectedSegments[1]) {
          segmentsRead = 0;
          leptonSync();
          break;
        }
      } else if (segmentNumber == 4) {
        if (!collectedSegments[0] || !collectedSegments[1] || !collectedSegments[2]) {
          segmentsRead = 0;
          leptonSync();
          break;
        }
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

      if (segmentNumber > 4) {
        leptonSync();
        break;
      }
      
      collectedSegments[segmentNumber - 1] = true;

      // Copie optimisée des pixels dans l'image
      for (int packetNumber = 0; packetNumber < 60; packetNumber++) {
        int yPos = (segmentNumber - 1) * 30 + (packetNumber / 2);
        
        if (packetNumber % 2) {
          // Packet impair : côté droit (X = 80-159)
          for (int px = 0; px < 80; px++) {
            image[80 + px][yPos] = lepton_frame_segment[packetNumber][px + 2];
          }
        } else {
          // Packet pair : côté gauche (X = 0-79)
          for (int px = 0; px < 80; px++) {
            image[px][yPos] = lepton_frame_segment[packetNumber][px + 2];
          }
        }
      }
    }
  }
  
  hspi->setDataMode(SPI_MODE0);
}

void displayImage(void) {


  double frame_min = 65535, frame_max = 0;

  // Recherche min/max dans l'image
  for (int j = 0; j < image_y; j++) {
    for (int i = 0; i < image_x; i++) {
      uint16_t val = image[i][j];
      if (val > frame_max) frame_max = val;
      if (val < frame_min) frame_min = val;
    }
  }

  double display_min = frame_min;
  double display_max = frame_max;

  // Clipping automatique pour ignorer les valeurs aberrantes (comme le ciel)
  if (enable_auto_range) {
    // Créer un histogramme simplifié pour trouver les percentiles
    const int HIST_SIZE = 200;
    int histogram[HIST_SIZE] = {0};
    
    double range = frame_max - frame_min;
    if (range < 100) range = 100;
    
    // Remplir l'histogramme
    for (int j = 0; j < image_y; j++) {
      for (int i = 0; i < image_x; i++) {
        double norm = (image[i][j] - frame_min) / range;
        int bin = constrain((int)(norm * (HIST_SIZE - 1)), 0, HIST_SIZE - 1);
        histogram[bin]++;
      }
    }
    
    // Trouver les percentiles pour ignorer les extrêmes
    int total_pixels = image_x * image_y;
    int threshold_low = total_pixels * percentile_low;
    int threshold_high = total_pixels * percentile_high;
    
    int cumul = 0;
    int bin_min = 0, bin_max = HIST_SIZE - 1;
    
    // Trouver le percentile bas (ignore le ciel froid)
    for (int i = 0; i < HIST_SIZE; i++) {
      cumul += histogram[i];
      if (cumul >= threshold_low) {
        bin_min = i;
        break;
      }
    }
    
    // Trouver le percentile haut
    cumul = 0;
    for (int i = HIST_SIZE - 1; i >= 0; i--) {
      cumul += histogram[i];
      if (cumul >= (total_pixels - threshold_high)) {
        bin_max = i;
        break;
      }
    }
    
    // Calculer les nouvelles limites
    display_min = frame_min + (bin_min / (double)(HIST_SIZE - 1)) * range;
    display_max = frame_min + (bin_max / (double)(HIST_SIZE - 1)) * range;
    
    // Convertir en températures pour vérifier la plage
    double temp_range_celsius = ((display_max / 100.0) - 273.15) - ((display_min / 100.0) - 273.15);
    
    // Si la plage est trop petite (scène uniforme), forcer une plage minimale
    if (temp_range_celsius < min_display_range_celsius) {
      // Centrer autour de la moyenne et étendre à la plage minimale
      double center = (display_min + display_max) / 2.0;
      double half_range_kelvin = (min_display_range_celsius / 2.0) * 100.0; // Convertir en centikelvins
      display_min = center - half_range_kelvin;
      display_max = center + half_range_kelvin;
    }
  }

  double display_range = display_max - display_min;
  if (display_range < 100) display_range = 100;

  // Calcul des températures pour affichage AVEC CALIBRATION
  max_temp = ((display_max / 100.0) - 273.15) * temp_scale + temp_offset;
  min_temp = ((display_min / 100.0) - 273.15) * temp_scale + temp_offset;

  // Calcul du seuil pour les zones très chaudes (top 5% de la plage visible)
  uint16_t hot_threshold = display_min + (display_range * hot_spot_threshold);

  // Rendu pixel par pixel avec surbrillance verte pour les zones chaudes
  for (int j = 0; j < image_y; j++) {
    for (int i = 0; i < image_x; i++) {
      uint16_t pixelValue = image[i][j];
      
      // Normalisation avec clipping
      double norm = (pixelValue - display_min) / display_range;
      norm = constrain(norm, 0.0, 1.0);
      
      uint16_t color;
      
      // Si le pixel est dans les zones très chaudes (top 5%) et que la surbrillance est activée
      if (enable_hot_spot && pixelValue >= hot_threshold) {
        // Vert néon pour les zones très chaudes
        double hot_norm = (pixelValue - hot_threshold) / (display_max - hot_threshold);
        hot_norm = constrain(hot_norm, 0.0, 1.0);
        
        // Vert néon progressif
        uint8_t green_intensity = 200 + (uint8_t)(hot_norm * 55); // 200-255
        uint8_t r = (uint8_t)(hot_norm * 100); // Un peu de rouge pour l'effet néon
        uint8_t g = green_intensity;
        uint8_t b = 0;
        
        // Conversion en RGB565
        uint8_t r5 = r >> 3;
        uint8_t g6 = g >> 2;
        uint8_t b5 = b >> 3;
        color = (r5 << 11) | (g6 << 5) | b5;
        
      } else {
        // Niveaux de gris normaux pour le reste
        uint8_t grayValue = (uint8_t)(norm * 255.0);
        
        if (grayValue == 0) {
          color = 0x0000; // Noir
        } else if (grayValue == 255) {
          color = 0xFFFF; // Blanc
        } else {
          // Gris : on met R=G=B pour avoir du gris pur
          uint8_t r = grayValue >> 3;  // 5 bits
          uint8_t g = grayValue >> 2;  // 6 bits
          uint8_t b = grayValue >> 3;  // 5 bits
          color = (r << 11) | (g << 5) | b;
        }
      }
      
      spr.drawPixel(i, j, color);
    }
  }

  // Dessiner le viseur central ROUGE
  drawCrosshair();

  // Barre d'infos : TEXTE BLANC SUR FOND NOIR avec indicateurs colorés
  uint16_t black = 0x0000; // Noir
  uint16_t white = 0xFFFF; // Blanc
  uint16_t blue = 0x001F;  // Bleu pour le froid
  uint16_t red = 0xF800;   // Rouge pour le chaud (en mode BGR : swap avec bleu)
  
  // Si votre écran est en BGR, inverser rouge et bleu
  if (red == 0xF800) {  // Test pour détecter si on est en RGB ou BGR
    // En mode BGR, on doit swapper
    red = 0x001F;   // Rouge en BGR
    blue = 0xF800;  // Bleu en BGR
  }
  
  spr.fillRect(0, image_y - 10, image_x, 10, black);
  spr.setTextColor(white, black);
  spr.setTextSize(1);
  
  // Température froide avec point bleu (à gauche)
  spr.fillCircle(4, image_y - 6, 2, blue);  // Point bleu
  spr.setCursor(10, image_y - 9);
  char bufferMin[16];
  snprintf(bufferMin, sizeof(bufferMin), "%.1fC", min_temp);
  spr.print(bufferMin);
  
  // Température chaude avec point rouge (espacement réduit)
  int tempMaxX = 55;  // Position réduite pour rapprocher les températures
  spr.fillCircle(tempMaxX, image_y - 6, 2, red);  // Point rouge
  spr.setCursor(tempMaxX + 6, image_y - 9);
  char bufferMax[16];
  snprintf(bufferMax, sizeof(bufferMax), "%.1fC", max_temp);
  spr.print(bufferMax);



  // FPS à droite avec virgule
  spr.setCursor(image_x - 38, image_y - 9);
  char bufferFPS[12];
  snprintf(bufferFPS, sizeof(bufferFPS), "%.1fFPS", fps);
  spr.print(bufferFPS);

    // Affichage percentile_low (%)
spr.setCursor(image_x - 95, image_y - 110);
char bufferPct[16];
snprintf(bufferPct, sizeof(bufferPct), "Low:%2.0f%%", percentile_low_percent);
spr.print(bufferPct);

  spr.pushSprite(0, 0);
  
  // Calcul du FPS
  unsigned long now = millis();
  if (lastFrameTime > 0) {
    float frameTime = (now - lastFrameTime) / 1000.0;
    fps = fps * 0.9 + (1.0 / frameTime) * 0.1; // Lissage
  }
  lastFrameTime = now;
}

void printBin(byte aByte) {
  for (int8_t aBit = 7; aBit >= 0; aBit--) {
    Serial.write(bitRead(aByte, aBit) ? '1' : '0');
  }
}

void transferImage(void) {
  int divider = 4;
  for(int i = 0; i < image_y / divider; i++) {
    for(int j = 0; j < image_x / divider; j++) {
      Serial.print((int)image[j * divider][i * divider], DEC);
      Serial.print("\t");
    }
    Serial.print("\n");
  }
  Serial.println("Transfer Complete");
}

void debugPackets(byte * packet) {
  if (((packet[0] & 0x0f) != 0x0f) && (packet[0] != 0x00)) {
    printBin(packet[0]);
    Serial.print("\t");
    printBin(packet[1]);
    Serial.println("");
  }
}

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

void configureLeptonHighSpeed() {
  Serial.println("Configuration Lepton haute vitesse...");
  
  // Désactiver la télémétrie pour gagner en vitesse
  setRegister(0x0004);
  Wire.beginTransmission(ADDRESS);
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();
  
  delay(100);
}

void calibrateTemperature() {
  Serial.println("\n=== CALIBRATION TEMPERATURE ===");
  Serial.println("Par defaut, le Lepton 3.5 donne des temperatures en centikelvins");
  Serial.println("Formule de conversion : T(C) = (valeur / 100) - 273.15");
  Serial.println();
  Serial.println("Options de calibration :");
  Serial.println("1. Modifier temp_offset dans le code (ajoute/retire des degres)");
  Serial.println("2. Modifier temp_scale dans le code (facteur multiplicateur)");
  Serial.println();
  Serial.println("Exemple : Si vos temperatures sont 5°C trop basses,");
  Serial.println("  mettez : temp_offset = 5.0;");
  Serial.println();
  Serial.println("Valeurs actuelles :");
  Serial.print("  temp_offset = ");
  Serial.println(temp_offset);
  Serial.print("  temp_scale = ");
  Serial.println(temp_scale);
  Serial.println();
  Serial.println("Pour calibrer correctement :");
  Serial.println("1. Pointez vers un objet de temperature connue (ex: eau glacee = 0°C)");
  Serial.println("2. Notez la temperature affichee");
  Serial.println("3. Ajustez temp_offset = temperature_reelle - temperature_affichee");
  Serial.println("================================\n");
}

void setup() {
  // Configuration ADC pour le potentiomètre
pinMode(POT_PIN, INPUT);
analogReadResolution(12);      // 0–4095
analogSetAttenuation(ADC_11db); // plage ~0–3.3V

  // Overclocking ESP32 pour performances maximales
  setCpuFrequencyMhz(240);

  // Setup the hspi bus
  hspi = new SPIClass(HSPI);
  hspi->begin(SCK_PIN, MISO_PIN, MOSI_PIN, FLIR_NCS_PIN);
  hspi->setDataMode(SPI_MODE3);
  hspi->setFrequency(20000000);
  hspi->setHwCs(false); // Gestion manuelle du CS pour DMA
  
  pinMode(FLIR_NCS_PIN, OUTPUT);
  digitalWrite(FLIR_NCS_PIN, HIGH);

  // Initialize serial port
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== DEMARRAGE FLIR LEPTON 3.5 ===");

  // Add delay needed for lepton setup
  delay(5000);

  // Set up i2c on alternate pins, 400kHz baud
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  
  // Attendre que le Lepton soit prêt
  if (!waitForLeptonReady()) {
    Serial.println("ERREUR: Lepton non detecte");
    Serial.println("Verifiez le cablage et appuyez sur RESET");
    while(1) delay(1000);
  }

  // Configuration haute vitesse
  configureLeptonHighSpeed();
  
  // Afficher les infos de calibration
  calibrateTemperature();

  // Initialisation de l'écran
  ips.init();
  ips.setRotation(3);
  
  // Inversion désactivée
  ips.invertDisplay(false);
  
  ips.fillScreen(0x0000); // Noir en BGR
  
  // Initialisation de la palette BGR
  initIronBlackLUT();
  
  // Création du sprite
  spr.createSprite(image_x, image_y);
  spr.fillSprite(0x0000); // Noir

  // Synchronisation initiale
  leptonSync();
  
  Serial.println("Systeme pret !");
  Serial.print("CPU: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");
}

void loop() {
  // Lecture du potentiomètre
pot_raw = analogRead(POT_PIN);

// Mapping vers percentile_low
float pot_norm = pot_raw / 4095.0;
percentile_low = percentile_low_min +
                 pot_norm * (percentile_low_max - percentile_low_min);

// Pour affichage en %
percentile_low_percent = percentile_low * 100.0;


  captureImage();
  displayImage();
  
  // Pas de delay - vitesse maximale !
}
