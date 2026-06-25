#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <QMI8658.h> // Official Lahavg Library verified working

// Matrix Configurations
#define MATRIX_PIN  14        
#define NUM_LEDS    64        
Adafruit_NeoPixel matrix = Adafruit_NeoPixel(NUM_LEDS, MATRIX_PIN, NEO_GRB + NEO_KHZ800);

// Board Specific I2C Pin Mapping
#define I2C_SDA 11
#define I2C_SCL 12

// Core Driver Instantiation
QMI8658 imu;

// --- INDEPENDENT VISIBILITY CONFIGURATION VARIABLES ---
const uint8_t DICE_BRIGHTNESS       = 255;  // 0 to 255 (Max intensity for pips)
const uint8_t BACKGROUND_BRIGHTNESS = 10;   // Level 10 intensity for complementary background tiles

// --- EASY SET TIMING CONFIGURATION VARIABLES ---
const unsigned long ROLL_DURATION_MS = 10000; // 10 seconds total deceleration timeline
const unsigned long SCREENSAVER_TIMEOUT_MS = 15000; // 15 seconds of absolute stillness

// Physical limits and Threshold parameters
const float SHAKE_THRESHOLD      = 1700.0; // Total acceleration threshold in mg
const float TILT_THRESHOLD       = 180.0;  // Threshold to detect intentional tilt movement
const int FASTEST_FRAME_RATE     = 50;     // Minimum millisecond delay between roll values (Max speed)
const int STOPPED_FRAME_RATE     = 650;    // Maximum millisecond delay before values lock (Full stop)
const float TILT_ACCEL_FORCE     = 0.005;  // Acceleration multiplier applied by tilt forces
const float DAMPING_FACTOR       = 0.75;   // Kinetic bounce loss factor off walls

float currentFrameDelay = STOPPED_FRAME_RATE; 
unsigned long lastFrameTime = 0;
bool isBoardShaking = false;

// Time tracking for the linear deceleration calculations
unsigned long shakeStopTimestamp = 0;

// Color Gradient Hues Tracking
uint16_t colorHueCounter = 0;

// Track 5 independent active dice items simultaneously
int activeDiceVals[] = {1, 2, 3, 5, 6};

// State machine indicators to tightly control face value swaps
bool isRollActive = false; 

// Screensaver and Idle Timer State Tracking
unsigned long lastActivityTime = 0;
bool isScreensaverActive = false;

// Moving Pixel Screensaver Physics Variables
float ssX = 3.5; float ssY = 3.5;   // Sub-pixel positions
float ssDx = 0.0; float ssDy = 0.0; // Vector trajectories
unsigned long lastScreensaverMoveTime = 0;
const int SCREENSAVER_SPEED_MS = 100; // Frame interval for screensaver travel
uint16_t ssHue = 0;

const int TRAIL_LENGTH = 4;
int trailX[TRAIL_LENGTH];
int trailY[TRAIL_LENGTH];

// Center coordinates for 5 independent particles (3x2 geometry anchors)
float pos_X[] = {1.5, 5.5, 1.5, 5.5, 3.5};
float pos_Y[] = {0.0, 0.0, 6.0, 6.0, 3.0}; 
float vel_X[] = {0.0, 0.0, 0.0, 0.0, 0.0};
float vel_Y[] = {0.0, 0.0, 0.0, 0.0, 0.0};

float currentGravityY = 0.0;

// Symmetrical 3x2 bitmasks (Row-major layout: bits 0-2 = Top Row, bits 3-5 = Bottom Row)
const uint8_t dicePatterns3x2[] = {
  0b010000, // 1: Top-Center pip only
  0b100001, // 2: Top-Left and Bottom-Right pips
  0b100101, // 3: Top-Left, Center-Right, and Bottom-Right pips
  0b110011, // 4: Four corners filled (Top-Left, Top-Right, Bottom-Left, Bottom-Right)
  0b110111, // 5: Four corners + Center-Right pip
  0b111111  // 6: All 6 pixels completely filled
};

// Forward declarations to bridge tabs cleanly
void renderFiveDice(uint16_t currentHue, bool forceWhite = false);
int getPixelIndex(int x, int y);
void triggerBlinkIndicator();
void initScreensaverTrajectory();
void updateAndRenderScreensaver(unsigned long currentTime);
void handleMatrixPhysics();
void displayStartupTextAnimation(); 

void setup() {
  Serial.begin(115200);
  matrix.begin();
  matrix.setBrightness(25); 
  matrix.show(); 
  
  displayStartupTextAnimation();
  
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!imu.begin(I2C_SDA, I2C_SCL, QMI8658_ADDRESS_LOW)) {
    matrix.fill(matrix.Color(150, 0, 0)); 
    matrix.show();
    while (1) { delay(10); } 
  }

  imu.setAccelUnit_mg(true);   
  imu.setGyroUnit_dps(true);   
  imu.setAccelRange(QMI8658_ACCEL_RANGE_4G); 
  imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  
  randomSeed(micros());
  lastActivityTime = millis();
}

void loop() {
  QMI8658_Data sensor;
  
  if (imu.readSensorData(sensor)) {
    float ax = sensor.accelX;
    float ay = sensor.accelY;
    float az = sensor.accelZ;

    float totalAccel = sqrt(ax * ax + ay * ay + az * az);
    unsigned long currentTime = millis();

    bool hasUserActivity = (totalAccel > SHAKE_THRESHOLD) || (abs(ax) > TILT_THRESHOLD) || (abs(ay) > TILT_THRESHOLD);

    if (hasUserActivity) {
      lastActivityTime = currentTime; 
      if (isScreensaverActive) {
        isScreensaverActive = false;  
      }
    }

    if (totalAccel > SHAKE_THRESHOLD) {
      if (!isBoardShaking && !isRollActive) {
        isRollActive = true;
      }
      isBoardShaking = true;
      currentFrameDelay = FASTEST_FRAME_RATE; 
    } else {
      if (isBoardShaking) {
        shakeStopTimestamp = currentTime; 
      }
      isBoardShaking = false;
    }

    if (currentTime - lastFrameTime > (unsigned long)currentFrameDelay) {
      if (isRollActive) {
        for (int i = 0; i < 5; i++) { activeDiceVals[i] = random(1, 7); }

        if (isBoardShaking) {
          for (int i = 0; i < 5; i++) {
            vel_X[i] = random(-40, 41) / 10.0f; 
            vel_Y[i] = random(-40, 41) / 10.0f;
          }
          currentFrameDelay = FASTEST_FRAME_RATE;
        } else {
          unsigned long timeElapsed = currentTime - shakeStopTimestamp;
          
          if (timeElapsed >= ROLL_DURATION_MS) {
            currentFrameDelay = STOPPED_FRAME_RATE;
            isRollActive = false;    
            triggerBlinkIndicator(); 
            unsigned long postFlashTime = millis();
            lastFrameTime = postFlashTime;
            lastActivityTime = postFlashTime; 
          } else {
            float progress = (float)timeElapsed / (float)ROLL_DURATION_MS;
            currentFrameDelay = FASTEST_FRAME_RATE + (progress * (STOPPED_FRAME_RATE - FASTEST_FRAME_RATE));
          }
        }
        if (isRollActive) { lastFrameTime = currentTime; }
      }
    }

    if (!isRollActive && (currentTime - lastActivityTime > SCREENSAVER_TIMEOUT_MS)) {
      if (!isScreensaverActive) {
        isScreensaverActive = true;
        initScreensaverTrajectory(); 
      }
      updateAndRenderScreensaver(currentTime);
    } else {
      float forceX = -ay * TILT_ACCEL_FORCE;
      float forceY =  ax * TILT_ACCEL_FORCE; 
      
      currentGravityY = forceY;

      for (int i = 0; i < 5; i++) {
        vel_X[i] += forceX; 
        vel_Y[i] += forceY;
        pos_X[i] += vel_X[i] * 0.1f; 
        pos_Y[i] += vel_Y[i] * 0.1f;
      }

      handleMatrixPhysics();
      if (isRollActive) { colorHueCounter += 140; }
      renderFiveDice(colorHueCounter);
    }
  }
  delay(12); 
}
void handleMatrixPhysics() {
  for (int i = 0; i < 5; i++) {
    if (pos_X[i] < 0.0f) { pos_X[i] = 0.0f; vel_X[i] = -vel_X[i] * DAMPING_FACTOR; }
    if (pos_X[i] > 5.0f) { pos_X[i] = 5.0f; vel_X[i] = -vel_X[i] * DAMPING_FACTOR; }
    if (pos_Y[i] < 0.0f) { pos_Y[i] = 0.0f; vel_Y[i] = -vel_Y[i] * DAMPING_FACTOR; }
    if (pos_Y[i] > 6.0f) { pos_Y[i] = 6.0f; vel_Y[i] = -vel_Y[i] * DAMPING_FACTOR; }
  }

  for (int i = 0; i < 5; i++) {
    for (int j = i + 1; j < 5; j++) {
      float deltaX = pos_X[j] - pos_X[i];
      float deltaY = pos_Y[j] - pos_Y[i];
      
      float overlapX = 3.0f - abs(deltaX);
      float overlapY = 2.0f - abs(deltaY);

      if (overlapX > 0.0f && overlapY > 0.0f) {
        if (overlapX < overlapY) {
          float signX = (deltaX > 0) ? 1.0f : -1.0f;
          pos_X[i] -= signX * overlapX * 0.5f;
          pos_X[j] += signX * overlapX * 0.5f;
          
          float temp = vel_X[i];
          vel_X[i] = vel_X[j] * DAMPING_FACTOR;
          vel_X[j] = temp * DAMPING_FACTOR;
        } else {
          float signY = (deltaY > 0) ? 1.0f : -1.0f;
          pos_Y[i] -= signY * overlapY * 0.5f;
          pos_Y[j] += signY * overlapY * 0.5f;
          
          float temp = vel_Y[i];
          vel_Y[i] = vel_Y[j] * DAMPING_FACTOR;
          vel_Y[j] = temp * DAMPING_FACTOR;
        }
      }
    }
  }
}

void initScreensaverTrajectory() {
  float angle = random(0, 360) * (PI / 180.0f); 
  ssDx = cos(angle) * 0.45f; 
  ssDy = sin(angle) * 0.45f;
  ssX = random(1, 7);
  ssY = random(1, 7);

  for (int i = 0; i < TRAIL_LENGTH; i++) {
    trailX[i] = -1; trailY[i] = -1;
  }
}

void updateAndRenderScreensaver(unsigned long currentTime) {
  if (currentTime - lastScreensaverMoveTime > SCREENSAVER_SPEED_MS) {
    for (int i = TRAIL_LENGTH - 1; i > 0; i--) {
      trailX[i] = trailX[i - 1]; trailY[i] = trailY[i - 1];
    }
    // FIXED: Added array tracking bracket indexes [0] to solve variable assignment mismatch errors
    trailX[0] = (int)(ssX + 0.5f);
    trailY[0] = (int)(ssY + 0.5f);

    ssX += ssDx; ssY += ssDy;

    if (ssX < 0.0f) { ssX = 0.0f; ssDx = -ssDx; }
    if (ssX > 7.0f) { ssX = 7.0f; ssDx = -ssDx; }
    if (ssY < 0.0f) { ssY = 0.0f; ssDy = -ssDy; }
    if (ssY > 7.0f) { ssY = 7.0f; ssDx = -ssDx; }

    ssHue += 800; 
    lastScreensaverMoveTime = currentTime;
  }

  matrix.clear();
  for (int i = TRAIL_LENGTH - 1; i >= 0; i--) {
    if (trailX[i] >= 0 && trailX[i] < 8 && trailY[i] >= 0 && trailY[i] < 8) {
      uint8_t brightness = 200 / (i + 2); 
      matrix.setPixelColor(getPixelIndex(trailX[i], trailY[i]), matrix.ColorHSV(ssHue - (i * 2000), 255, brightness));
    }
  }
  int drawX = (int)(ssX + 0.5f); int drawY = (int)(ssY + 0.5f);
  matrix.setPixelColor(getPixelIndex(drawX, drawY), matrix.ColorHSV(ssHue, 255, 255));
  matrix.show();
}
int getPixelIndex(int x, int y) {
  return (y * 8) + x;
}

// 2D font matrix for characters ordered specifically as: Y, A, H, M, S
const uint8_t fontYAHMS[5][5] = {
  {0x11, 0x11, 0x0A, 0x04, 0x04}, //: Y
  {0x04, 0x0A, 0x11, 0x1F, 0x11}, //: A
  {0x11, 0x11, 0x1F, 0x11, 0x11}, //: H
  {0x11, 0x1B, 0x15, 0x11, 0x11}, //: M
  {0x0F, 0x10, 0x0E, 0x01, 0x1E}  //: S
};

void displayStartupTextAnimation() {
  uint32_t textCol = matrix.Color(0, 180, 220); 
  int letterSpacing = 6;
  int textLengthPixels = 5 * letterSpacing; 
  
  for (int loopIteration = 0; loopIteration < 3; loopIteration++) {
    for (int offset = -textLengthPixels; offset < 8; offset++) {
      matrix.clear();
      
      for (int charIdx = 0; charIdx < 5; charIdx++) {
        int targetStartX = offset + (charIdx * letterSpacing);
        int actualFontIndex = 4 - charIdx; 
        
        for (int r = 0; r < 5; r++) {
          uint8_t fontRowByte = fontYAHMS[actualFontIndex][4 - r];
          for (int c = 0; c < 5; c++) {
            bool isPixelSet = (fontRowByte >> c) & 0x01; 
            if (isPixelSet) {
              int screenX = targetStartX + c;
              int screenY = r + 1; 
              if (screenX >= 0 && screenX < 8 && screenY >= 0 && screenY < 8) {
                matrix.setPixelColor(getPixelIndex(screenX, screenY), textCol);
              }
            }
          }
        }
      }
      matrix.show();
      delay(75); 
    }
    delay(200); 
  }
}

void triggerBlinkIndicator() {
  for (int i = 0; i < 2; i++) {
    matrix.clear(); matrix.show(); delay(80);
    renderFiveDice(colorHueCounter, true); delay(100);
  }
}

void renderFiveDice(uint16_t currentHue, bool forceWhite) {
  matrix.clear();

  uint32_t settleColors[] = {
    matrix.ColorHSV(0, 255, DICE_BRIGHTNESS),      // 0: Red
    matrix.ColorHSV(21845, 255, DICE_BRIGHTNESS),  // 1: Green
    matrix.ColorHSV(43690, 255, DICE_BRIGHTNESS),  // 2: Blue
    matrix.ColorHSV(32768, 255, DICE_BRIGHTNESS),  // 3: Cyan
    matrix.ColorHSV(10922, 255, DICE_BRIGHTNESS)   // 4: Yellow
  };

  uint16_t settleHues[] = {0, 21845, 43690, 32768, 10922};

  int startX[5], startY[5];
  for (int d = 0; d < 5; d++) {
    startX[d] = (int)(pos_X[d] + 0.5f);
    startY[d] = (int)(pos_Y[d] + 0.5f);
  }

  if (!isRollActive) {
    for (int i = 0; i < 5; i++) {
      int verticalStackCount = 0;
      int laneIndices[5]; 
      for (int j = 0; j < 5; j++) {
        if (abs(startX[i] - startX[j]) < 2) {
          laneIndices[verticalStackCount++] = j;
        }
      }
      if (verticalStackCount >= 4) {
        for (int k = 3; k < verticalStackCount; k++) {
          int targetIdx = laneIndices[k];
          if (startX[targetIdx] <= 2) startX[targetIdx] = 5; 
          else startX[targetIdx] = 0;                        
        }
      }
    }

    bool shiftedBlock = true;
    int loopSafetyGuard = 12;

    while (shiftedBlock && loopSafetyGuard > 0) {
      shiftedBlock = false;
      loopSafetyGuard--;

      for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
          int gapX = abs(startX[j] - startX[i]);
          int gapY = abs(startY[j] - startY[i]);

          if (gapX < 3 && gapY < 2) {
            shiftedBlock = true;

            if ((3 - gapX) < (2 - gapY)) {
              int correctionX = 3 - gapX;
              if (startX[j] >= startX[i]) {
                if (startX[j] + correctionX <= 5) startX[j] += correctionX; else startX[i] -= correctionX;
              } else {
                if (startX[i] + correctionX <= 5) startX[i] += correctionX; else startX[j] -= correctionX;
              }
            } else {
              int correctionY = 2 - gapY;
              if (currentGravityY < 0.0f) {
                if (startY[j] >= startY[i]) {
                  if (startY[j] + correctionY <= 6) startY[j] += correctionY; else startY[i] -= correctionY;
                } else {
                  if (startY[i] + correctionY <= 6) startY[i] += correctionY; else startY[j] -= correctionY;
                }
              } else {
                if (startY[j] <= startY[i]) {
                  if (startY[j] - correctionY >= 0) startY[j] -= correctionY; else startY[i] += correctionY;
                } else {
                  if (startY[i] - correctionY >= 0) startY[i] -= correctionY; else startY[j] += correctionY;
                }
              }
            }
          }
        }
      }
    }
  }

  for (int d = 0; d < 5; d++) {
    if (startX[d] < 0) startX[d] = 0; if (startX[d] > 5) startX[d] = 5;
    if (startY[d] < 0) startY[d] = 0; if (startY[d] > 6) startY[d] = 6;
  }

  for (int ty = 0; ty < 8; ty++) {
    for (int tx = 0; tx < 8; tx++) {
      uint32_t winningColor = 0;
      int highestPriorityTier = 0; 
      float winningDiceY = (currentGravityY >= 0.0f) ? -99.0f : 99.0f; 

      for (int d = 0; d < 5; d++) {
        int colOffset = tx - startX[d];
        int rowOffset = ty - startY[d];

        if (colOffset >= 0 && colOffset < 3 && rowOffset >= 0 && rowOffset < 2) {
          uint8_t pattern = dicePatterns3x2[activeDiceVals[d] - 1];
          int bitIdx = (rowOffset * 3) + colOffset;
          
          bool isPipElement = (pattern & (1 << bitIdx)) != 0;

          if (isPipElement) {
            bool takesForegroundPriority = false;
            if (highestPriorityTier < 2) {
              takesForegroundPriority = true;
            } else if (currentGravityY >= 0.0f && pos_Y[d] > winningDiceY) {
              takesForegroundPriority = true; 
            } else if (currentGravityY < 0.0f && pos_Y[d] < winningDiceY) {
              takesForegroundPriority = true; 
            }

            if (takesForegroundPriority) {
              highestPriorityTier = 2;
              winningDiceY = pos_Y[d]; 
              
              if (forceWhite) {
                winningColor = matrix.Color(255, 255, 255);
              } else if (isRollActive) {
                uint16_t rollingHue = currentHue + (d * 12000);
                winningColor = matrix.ColorHSV(rollingHue, 255, DICE_BRIGHTNESS);
              } else {
                winningColor = settleColors[d];
              }
            }
          } else {
            bool takesBackgroundPriority = false;
            if (highestPriorityTier == 0) {
              takesBackgroundPriority = true;
            } else if (highestPriorityTier == 1) {
              if (currentGravityY >= 0.0f && pos_Y[d] > winningDiceY) takesBackgroundPriority = true;
              if (currentGravityY < 0.0f && pos_Y[d] < winningDiceY) takesBackgroundPriority = true;
            }

            if (takesBackgroundPriority) {
              if (highestPriorityTier == 0) {
                highestPriorityTier = 1;
              }
              winningDiceY = pos_Y[d];
              
              if (forceWhite) {
                winningColor = matrix.Color(15, 15, 15);
              } else if (isRollActive) {
                uint16_t rollingHue = currentHue + (d * 12000);
                winningColor = matrix.ColorHSV(rollingHue + 32768, 255, BACKGROUND_BRIGHTNESS);
              } else {
                winningColor = matrix.ColorHSV(settleHues[d] + 32768, 255, BACKGROUND_BRIGHTNESS);
              }
            }
          }
        }
      }

      if (highestPriorityTier > 0) {
        matrix.setPixelColor(getPixelIndex(tx, ty), winningColor);
      }
    }
  }
  matrix.show();
}
