#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <QMI8658.h> // Official Lahavg Library

// Matrix Configurations
#define MATRIX_PIN  14        
#define NUM_LEDS    64        
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(NUM_LEDS, MATRIX_PIN, NEO_GRB + NEO_KHZ800);

// Board Specific I2C Pin Mapping
#define I2C_SDA 11
#define I2C_SCL 12

// Core Driver Instantiation
QMI8658 imu;

// Operational Limits & Timing Delays
unsigned long lastShakeTime = 0;
const int SHAKE_COOLDOWN = 1200; 
const float SHAKE_THRESHOLD = 350.0;     

unsigned long lastSideChangeTime = 0;   
const unsigned long SLEEP_TIMEOUT = 4000; // 4 seconds before starting the screensaver fade
bool isAsleep = false;
bool isDissolved = false;

// Brightness Variable Tracking
bool fadeInitialized = false;
int fadeBrightness = 20; 
unsigned long lastFadeStepTime = 0;

// Side Tracking Flags and Debounce Logic
enum BoardSide { SIDE_UNKNOWN, SIDE_FRONT, SIDE_BACK, SIDE_LEFT, SIDE_RIGHT, SIDE_TOP, SIDE_BOTTOM };
BoardSide confirmedSide = SIDE_UNKNOWN;
BoardSide lastConfirmedSide = SIDE_UNKNOWN;
BoardSide rawSideSample = SIDE_UNKNOWN;
unsigned long sideDebounceStartTime = 0;
const unsigned long DEBOUNCE_DELAY = 300; // Must hold side for 300ms to lock it in

// Cross-Fade State Configuration (FIXED: Increased to 3 seconds for a very visible blend)
bool isCrossFading = false;
unsigned long crossFadeStartTime = 0;
const unsigned long CROSS_FADE_DURATION = 800; 

// Pointers and metadata to handle drawing and blending faces
const byte* currentFace;
uint32_t currentFaceColor;
const byte* previousFace = NULL;
uint32_t previousFaceColor = 0;

// Canvas trail buffers to store screensaver color paths
byte trailRed[64];
byte trailGreen[64];
byte trailBlue[64];

// Physics Struct Maps
struct Bouncer {
  float x, y;
  float dx, dy;
  uint32_t color;
};
Bouncer singleBouncer;

struct Crosser {
  float x, y;
  float dx, dy;
  uint32_t color;
  bool active;
  unsigned long spawnDelay;
  unsigned long lastSpawnCheck;
};
Crosser singleCrosser;

// 8x8 Custom Expression Bitmaps
const byte faceHappy[]     = { B00000000, B01100110, B01100110, B00000000, B01000010, B00111100, B00000000, B00000000 };
const byte faceWink[]      = { B00000000, B00000110, B01100110, B00000000, B01000010, B00111100, B00000000, B00000000 };
const byte faceCool[]      = { B00000000, B01111110, B11100111, B00000000, B00000000, B01111110, B00111100, B00000000 };
const byte faceSurprised[] = { B00000000, B01100110, B01100110, B00000000, B00011000, B00100100, B00011000, B00000000 };
const byte faceSilly[]     = { B00000000, B01100110, B01100000, B00000000, B01111110, B00011000, B00011000, B00000000 };
const byte faceSleepy[]    = { B00000000, B01100110, B00000000, B00000000, B00111100, B00000000, B00000000, B00000000 };

// Color Generation Wheel Function
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) return matrix.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  if(WheelPos < 170) { WheelPos -= 85; return matrix.Color(0, WheelPos * 3, 255 - WheelPos * 3); }
  WheelPos -= 170; return matrix.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void setup() {
  Serial.begin(115200);
  matrix.begin();
  matrix.setBrightness(20); 
  
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!imu.begin(I2C_SDA, I2C_SCL, QMI8658_ADDRESS_LOW)) {
    matrix.fill(matrix.Color(150, 0, 0)); matrix.show();
    while (1) { delay(10); } 
  }

  imu.setAccelUnit_mg(true);   
  imu.setGyroUnit_dps(true);   
  imu.setAccelRange(QMI8658_ACCEL_RANGE_2G);
  imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  imu.setGyroRange(QMI8658_GYRO_RANGE_512DPS);
  imu.setGyroODR(QMI8658_GYRO_ODR_125HZ);
  
  currentFace = faceHappy;
  currentFaceColor = matrix.Color(0, 150, 0); 
  lastSideChangeTime = millis();
}
unsigned long lastPhysicsTime = 0;
const int PHYSICS_SPEED = 100; 

// Initialize Screensaver Elements
void initScreensaver() {
  randomSeed(micros());
  for (int i = 0; i < 64; i++) { trailRed[i] = 0; trailGreen[i] = 0; trailBlue[i] = 0; }
  
  singleBouncer.x = random(2, 6); 
  singleBouncer.y = random(2, 6);
  singleBouncer.dx = (random(0, 2) == 0 ? 0.6 : -0.6); 
  singleBouncer.dy = (random(0, 2) == 0 ? 0.6 : -0.6);
  singleBouncer.color = Wheel(random(256));

  singleCrosser.active = false; 
  singleCrosser.spawnDelay = random(500, 3000); 
  singleCrosser.lastSpawnCheck = millis(); 
  singleCrosser.color = Wheel(random(256));
}

// Math blend function for cross-fading colors dynamically
uint32_t blendColors(uint32_t c1, uint32_t c2, float progress) {
  byte r1 = (c1 >> 16) & 0xFF; byte g1 = (c1 >> 8) & 0xFF; byte b1 = c1 & 0xFF;
  byte r2 = (c2 >> 16) & 0xFF; byte g2 = (c2 >> 8) & 0xFF; byte b2 = c2 & 0xFF;
  
  byte r = r1 + (r2 - r1) * progress;
  byte g = g1 + (g2 - g1) * progress;
  byte b = b1 + (b2 - b1) * progress;
  return matrix.Color(r, g, b);
}

void drawBitmapDirect(const byte* bitmap, uint32_t color) {
  matrix.clear();
  for (int row = 0; row < 8; row++) {
    int targetRow = 7 - row; 
    for (int col = 0; col < 8; col++) {
      if ((bitmap[row] >> (7 - col)) & 0x01) { matrix.setPixelColor(targetRow * 8 + col, color); }
    }
  }
  matrix.show();
}

void loop() {
  QMI8658_Data sensor;
  if (imu.readSensorData(sensor)) {
    float ax = sensor.accelX, ay = sensor.accelY, az = sensor.accelZ;
    float gx = sensor.gyroX, gy = sensor.gyroY, gz = sensor.gyroZ;

    // 1. RAW SIDE DETECTION CODES
    BoardSide detectedSide = rawSideSample;
    if (az > 750)       { detectedSide = SIDE_FRONT; } 
    else if (az < -750) { detectedSide = SIDE_BACK;  } 
    else if (ax > 750)  { detectedSide = SIDE_LEFT;  } 
    else if (ax < -750) { detectedSide = SIDE_RIGHT; } 
    else if (ay > 750)  { detectedSide = SIDE_TOP;    } 
    else if (ay < -750) { detectedSide = SIDE_BOTTOM; }

    // 2. FIXED: DEBOUNCE ORIENTATION DATA INPUT TO ELIMINATE DRIFT NOISE
    if (detectedSide != rawSideSample) {
      rawSideSample = detectedSide;
      sideDebounceStartTime = millis(); // Restart debounce window clock
    }

    if ((millis() - sideDebounceStartTime) > DEBOUNCE_DELAY) {
      confirmedSide = rawSideSample; // Lock side position down securely
    }

    // 3. SWITCH FACE LAYOUT DATA MAPS ON CONFIRMED SIDE CHANGES ONLY
    if (confirmedSide != lastConfirmedSide) {
      if (lastConfirmedSide != SIDE_UNKNOWN && !isAsleep) {
        previousFace = currentFace; // Snapshot active image profile pointers
        previousFaceColor = currentFaceColor;
        isCrossFading = true;
        crossFadeStartTime = millis();
      }
      
      // Update target asset links matching your orientation values
      if (confirmedSide == SIDE_FRONT)       { currentFace = faceHappy;     currentFaceColor = matrix.Color(0, 150, 0); }
      else if (confirmedSide == SIDE_BACK)   { currentFace = faceWink;      currentFaceColor = matrix.Color(150, 150, 0); }
      else if (confirmedSide == SIDE_LEFT)   { currentFace = faceCool;      currentFaceColor = matrix.Color(0, 150, 150); }
      else if (confirmedSide == SIDE_RIGHT)  { currentFace = faceSurprised; currentFaceColor = matrix.Color(150, 0, 150); }
      else if (confirmedSide == SIDE_TOP)    { currentFace = faceSilly;     currentFaceColor = matrix.Color(150, 60, 0); }
      else if (confirmedSide == SIDE_BOTTOM) { currentFace = faceSleepy;    currentFaceColor = matrix.Color(90, 0, 150); }

      lastConfirmedSide = confirmedSide; 
      lastSideChangeTime = millis(); 
      isAsleep = false; isDissolved = false; fadeInitialized = false; 
      if (!isCrossFading) matrix.setBrightness(20);
    }

    // Shake Mechanics Override
    float totalRotation = abs(gx) + abs(gy) + abs(gz);
    if (totalRotation > SHAKE_THRESHOLD && (millis() - lastShakeTime > SHAKE_COOLDOWN)) {
      lastShakeTime = millis(); lastSideChangeTime = millis(); 
      isAsleep = false; isDissolved = false; fadeInitialized = false; isCrossFading = false; matrix.setBrightness(20); 
    }

    if (!isAsleep && !isCrossFading && (millis() - lastSideChangeTime > SLEEP_TIMEOUT)) { isAsleep = true; fadeInitialized = false; }

    if (isAsleep) {
      if (!isDissolved) { // Smooth Screensaver Fade-Down Window
        if (!fadeInitialized) { fadeBrightness = 20; fadeInitialized = true; lastFadeStepTime = millis(); }
        if (millis() - lastFadeStepTime > 150) {
          lastFadeStepTime = millis(); fadeBrightness--;
          if (fadeBrightness >= 0) { matrix.setBrightness(fadeBrightness); drawBitmapDirect(currentFace, currentFaceColor); } 
          else { isDissolved = true; matrix.setBrightness(15); initScreensaver(); lastPhysicsTime = millis(); }
        }
      } 
      else { // Bouncing & Crossing Trail Calculations
        if (millis() - lastPhysicsTime > PHYSICS_SPEED) {
          lastPhysicsTime = millis();
          for (int idx = 0; idx < 64; idx++) {
            trailRed[idx] = (trailRed[idx] * 6) / 10; trailGreen[idx] = (trailGreen[idx] * 6) / 10; trailBlue[idx] = (trailBlue[idx] * 6) / 10;
          }
          int oldPx = (int)round(singleBouncer.x), oldPy = (int)round(singleBouncer.y);
          if (oldPx >= 0 && oldPx < 8 && oldPy >= 0 && oldPy < 8) {
            int idx = (7 - oldPy) * 8 + oldPx;
            trailRed[idx] = (singleBouncer.color >> 16) & 0xFF; trailGreen[idx] = (singleBouncer.color >> 8) & 0xFF; trailBlue[idx] = singleBouncer.color & 0xFF;
          }
          singleBouncer.x += singleBouncer.dx; singleBouncer.y += singleBouncer.dy;
          if (singleBouncer.x <= 0 || singleBouncer.x >= 7) {
            singleBouncer.dx = -singleBouncer.dx; singleBouncer.dy += random(-15, 16) * 0.01; singleBouncer.color = Wheel(random(256)); singleBouncer.x = constrain(singleBouncer.x, 0, 7);
          }
          if (singleBouncer.y <= 0 || singleBouncer.y >= 7) {
            singleBouncer.dy = -singleBouncer.dy; singleBouncer.dx += random(-15, 16) * 0.01; singleBouncer.color = Wheel(random(256)); singleBouncer.y = constrain(singleBouncer.y, 0, 7);
          }
          singleBouncer.dx = constrain(singleBouncer.dx, -0.9, 0.9); singleBouncer.dy = constrain(singleBouncer.dy, -0.9, 0.9);

          if (!singleCrosser.active) {
            if (millis() - singleCrosser.lastSpawnCheck > singleCrosser.spawnDelay) {
              singleCrosser.active = true; singleCrosser.color = Wheel(random(256));
              int wall = random(0, 4); float ang = random(20, 70) * (PI / 180.0);
              if (wall == 0) { singleCrosser.x = 0; singleCrosser.y = random(1, 7); singleCrosser.dx = cos(ang)*0.8; singleCrosser.dy = (random(0,2)==0?1:-1)*sin(ang)*0.8; }
              else if (wall == 1) { singleCrosser.x = 7; singleCrosser.y = random(1, 7); singleCrosser.dx = -cos(ang)*0.8; singleCrosser.dy = (random(0,2)==0?1:-1)*sin(ang)*0.8; }
              else if (wall == 2) { singleCrosser.x = random(1, 7); singleCrosser.y = 7; singleCrosser.dx = (random(0,2)==0?1:-1)*sin(ang)*0.8; singleCrosser.dy = -cos(ang)*0.8; }
              else { singleCrosser.x = random(1, 7); singleCrosser.y = 0; singleCrosser.dx = (random(0,2)==0?1:-1)*sin(ang)*0.8; singleCrosser.dy = cos(ang)*0.8; }
            }
          } else {
            int oldCrossX = (int)round(singleCrosser.x), oldCrossY = (int)round(singleCrosser.y);
            if (oldCrossX >= 0 && oldCrossX < 8 && oldCrossY >= 0 && oldCrossY < 8) {
              int idx = (7 - oldCrossY) * 8 + oldCrossX;
              trailRed[idx] = (singleCrosser.color >> 16) & 0xFF; trailGreen[idx] = (singleCrosser.color >> 8) & 0xFF; trailBlue[idx] = singleCrosser.color & 0xFF;
            }
            singleCrosser.x += singleCrosser.dx; singleCrosser.y += singleCrosser.dy;
            if (singleCrosser.x < -1 || singleCrosser.x > 8 || singleCrosser.y < -1 || singleCrosser.y > 8) { singleCrosser.active = false; singleCrosser.spawnDelay = random(2000, 5000); singleCrosser.lastSpawnCheck = millis(); }
          }
        }
        for (int idx = 0; idx < 64; idx++) matrix.setPixelColor(idx, matrix.Color(trailRed[idx], trailGreen[idx], trailBlue[idx]));
        int bx = (int)round(singleBouncer.x), by = (int)round(singleBouncer.y); if (bx >= 0 && bx < 8 && by >= 0 && by < 8) matrix.setPixelColor((7 - by) * 8 + bx, singleBouncer.color);
        if (singleCrosser.active) { int cx = (int)round(singleCrosser.x), cy = (int)round(singleCrosser.y); if (cx >= 0 && cx < 8 && cy >= 0 && cy < 8) matrix.setPixelColor((7 - cy) * 8 + cx, singleCrosser.color); }
        matrix.show();
      }
    } 
    else if (isCrossFading) {
      // --- PHASE C: DYNAMIC INTER-FACIAL MATRIX CROSS-FADE BLENDER ---
      unsigned long elapsed = millis() - crossFadeStartTime;
      float progress = (float)elapsed / CROSS_FADE_DURATION;
      
      if (progress >= 1.0) {
        isCrossFading = false;
        matrix.setBrightness(20);
        drawBitmapDirect(currentFace, currentFaceColor);
      } else {
        matrix.clear();
        matrix.setBrightness(20); // Keep global brightness stable while blending values natively
        
        for (int r = 0; r < 8; r++) {
          int targetRow = 7 - r;
          for (int c = 0; c < 8; c++) {
            bool oldPixelActive = previousFace ? ((previousFace[r] >> (7 - c)) & 0x01) : false;
            bool newPixelActive = (currentFace[r] >> (7 - c)) & 0x01;
            
            uint32_t finalPixelColor = 0;
            if (oldPixelActive && newPixelActive) {
              finalPixelColor = blendColors(previousFaceColor, currentFaceColor, progress);
            } else if (oldPixelActive) {
              finalPixelColor = blendColors(previousFaceColor, matrix.Color(0,0,0), progress);
            } else if (newPixelActive) {
              finalPixelColor = blendColors(matrix.Color(0,0,0), currentFaceColor, progress);
              }
              if (finalPixelColor > 0) { 
                matrix.setPixelColor(targetRow * 8 + c, finalPixelColor); 
              }
            }
          }
          matrix.show();
        }
      }
      else { 
        drawBitmapDirect(currentFace, currentFaceColor); 
      }
    }
    delay(30);
  }
  