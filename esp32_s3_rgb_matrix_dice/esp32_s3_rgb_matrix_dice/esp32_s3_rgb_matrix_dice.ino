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
const uint8_t DICE_BRIGHTNESS       = 255;  // 0 to 255 (Max intensity for vivid moving pips)
const uint8_t BACKGROUND_BRIGHTNESS = 10;   // Level 10 intensity for complementary background tile

// --- EASY SET TIMING CONFIGURATION VARIABLES ---
const unsigned long ROLL_DURATION_MS = 10000; // 10 seconds total deceleration timeline
const unsigned long SCREENSAVER_TIMEOUT_MS = 5000; // 5 seconds of absolute stillness

// Physical limits and Threshold parameters
const float SHAKE_THRESHOLD      = 1700.0; // Total acceleration threshold in mg
const float TILT_THRESHOLD       = 180.0;  // Threshold to detect intentional tilt movement
const int FASTEST_FRAME_RATE     = 50;     // Minimum millisecond delay between roll values (Max speed)
const int STOPPED_FRAME_RATE     = 650;    // Maximum millisecond delay before values lock (Full stop)
const float TILT_ACCEL_FORCE     = 0.005;  // Acceleration multiplier applied by tilt forces
const float DAMPING_FACTOR       = 0.85;   // Kinetic bounce loss factor off walls

float currentFrameDelay = STOPPED_FRAME_RATE; 
unsigned long lastFrameTime = 0;
bool isBoardShaking = false;

// Time tracking for the linear deceleration calculations
unsigned long shakeStopTimestamp = 0;

// Color Gradient Hues Tracking
uint16_t colorHueCounter = 0;

// Current tracking states for face patterns
int activeDiceVal1 = 1;
int activeDiceVal2 = 6;

// State machine indicators to tightly control face value swaps
bool isRollActive = false; 

// Screensaver and Idle Timer State Tracking
unsigned long lastActivityTime = 0;
bool isScreensaverActive = false;

// Moving Pixel Screensaver Physics Variables
float ssX = 3.5; float ssY = 3.5;   // Sub-pixel positions
float ssDx = 0.0; float ssDy = 0.0; // Vector trajectories
unsigned long lastScreensaverMoveTime = 0;
const int SCREENSAVER_SPEED_MS = 100; // Faster frame interval for a punchier trail look
uint16_t ssHue = 0;

// Trail Buffer variables to store recent coordinates
const int TRAIL_LENGTH = 4;
int trailX[TRAIL_LENGTH];
int trailY[TRAIL_LENGTH];

// True Free-Floating Physics States (Tracked from center point of 3x3 layout)
float posX1 = 1.5, posY1 = 1.5;
float velX1 = 0.0, velY1 = 0.0;

float posX2 = 5.5, posY2 = 5.5;
float velX2 = 0.0, velY2 = 0.0;

// 3x3 layout bitmasks (Bits 0-8 map to a 3x3 grid)
const uint16_t dicePatterns[] = {
  0b000010000, // 1: Center pixel only
  0b100000001, // 2: Opposite corners (Top-Left & Bottom-Right)
  0b100010001, // 3: Three pixels forming a perfect diagonal line
  0b101000101, // 4: Four corners
  0b101010101, // 5: Two intersecting diagonals (4 corners + center)
  0b101101101  // 6: Left and right vertical columns filled entirely
};

// Forward declarations to bridge tabs cleanly
void renderFloatingDice(int val1, int val2, float x1, float y1, float x2, float y2, uint16_t currentHue, bool forceWhite = false);
int getPixelIndex(int x, int y);
void triggerBlinkIndicator();
void initScreensaverTrajectory();
void updateAndRenderScreensaver(unsigned long currentTime);
void handleMatrixPhysics();

void setup() {
  Serial.begin(115200);
  matrix.begin();
  matrix.setBrightness(25); 
  matrix.show(); 
  
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

    // Step 1: Manage Deceleration Easing Timeline and Value Swaps
    if (currentTime - lastFrameTime > (unsigned long)currentFrameDelay) {
      if (isRollActive) {
        activeDiceVal1 = random(1, 7);
        randomSeed(micros() + (long)(ax * 100.0f)); 
        activeDiceVal2 = random(1, 7);

        if (isBoardShaking) {
          velX1 = random(-40, 41) / 10.0f; velY1 = random(-40, 41) / 10.0f;
          velX2 = random(-40, 41) / 10.0f; velY2 = random(-40, 41) / 10.0f;
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
        
        if (isRollActive) {
          lastFrameTime = currentTime;
        }
      }
    }

    // Step 2: Handle Active Simulation or Screensaver Rendering Logic Split
    if (!isRollActive && (currentTime - lastActivityTime > SCREENSAVER_TIMEOUT_MS)) {
      if (!isScreensaverActive) {
        isScreensaverActive = true;
        initScreensaverTrajectory(); 
      }
      updateAndRenderScreensaver(currentTime);
    } else {
      float forceX = -ay * TILT_ACCEL_FORCE;
      float forceY =  ax * TILT_ACCEL_FORCE; 

      velX1 += forceX; velY1 += forceY;
      velX2 += forceX; velY2 += forceY;

      posX1 += velX1 * 0.1f; posY1 += velY1 * 0.1f;
      posX2 += velX2 * 0.1f; posY2 += velY2 * 0.1f;

      handleMatrixPhysics();
      
      if (isRollActive) {
        colorHueCounter += 140;
      }

      renderFloatingDice(activeDiceVal1, activeDiceVal2, posX1, posY1, posX2, posY2, colorHueCounter);
    }
  }
  delay(12); 
}
// Map 2D coordinates into NeoPixel sequential index layout
int getPixelIndex(int x, int y) {
  return (y * 8) + x;
}

// Generates a random vector direction angle and initializes the trail buffer coordinates to offscreen values (-1)
void initScreensaverTrajectory() {
  float angle = random(0, 360) * (PI / 180.0f); 
  ssDx = cos(angle) * 0.45f; // Slightly increased base speed for sharper trail look
  ssDy = sin(angle) * 0.45f;
  ssX = random(1, 7);
  ssY = random(1, 7);

  // Initialize trailing trace history to empty coordinates
  for (int i = 0; i < TRAIL_LENGTH; i++) {
    trailX[i] = -1;
    trailY[i] = -1;
  }
}

// Update particle positions, manage bounce angles, and shift trail coordinates down the buffer history stack
void updateAndRenderScreensaver(unsigned long currentTime) {
  if (currentTime - lastScreensaverMoveTime > SCREENSAVER_SPEED_MS) {
    // Shift old coordinates down the trail stack history
    for (int i = TRAIL_LENGTH - 1; i > 0; i--) {
      trailX[i] = trailX[i - 1];
      trailY[i] = trailY[i - 1];
    }
    // Record the current integer index position right before updating position floats
    trailX[0] = (int)(ssX + 0.5f);
    trailY[0] = (int)(ssY + 0.5f);

    ssX += ssDx;
    ssY += ssDy;

    // Hard boundary edge wall bounce logic
    if (ssX < 0.0f) { ssX = 0.0f; ssDx = -ssDx; }
    if (ssX > 7.0f) { ssX = 7.0f; ssDx = -ssDx; }
    if (ssY < 0.0f) { ssY = 0.0f; ssDy = -ssDy; }
    if (ssY > 7.0f) { ssY = 7.0f; ssDy = -ssDy; }

    ssHue += 800; // Color shift counter rotation speed
    lastScreensaverMoveTime = currentTime;
  }

  matrix.clear();

  // Draw Fading Trace Trail (Fades from oldest to newest)
  for (int i = TRAIL_LENGTH - 1; i >= 0; i--) {
    if (trailX[i] >= 0 && trailX[i] < 8 && trailY[i] >= 0 && trailY[i] < 8) {
      // Calculate a decreasing brightness multiplier index step
      uint8_t brightness = 200 / (i + 2); // Deeper indexes get lower light intensity values
      matrix.setPixelColor(getPixelIndex(trailX[i], trailY[i]), matrix.ColorHSV(ssHue - (i * 2000), 255, brightness));
    }
  }

  // Draw Core Leading Head Pixel (Full max brightness value 255)
  int drawX = (int)(ssX + 0.5f);
  int drawY = (int)(ssY + 0.5f);
  matrix.setPixelColor(getPixelIndex(drawX, drawY), matrix.ColorHSV(ssHue, 255, 255));
  
  matrix.show();
}

void handleMatrixPhysics() {
  if (posX1 < 1.0f) { posX1 = 1.0f; velX1 = -velX1 * DAMPING_FACTOR; }
  if (posX1 > 6.0f) { posX1 = 6.0f; velX1 = -velX1 * DAMPING_FACTOR; }
  if (posY1 < 1.0f) { posY1 = 1.0f; velY1 = -velY1 * DAMPING_FACTOR; }
  if (posY1 > 6.0f) { posY1 = 6.0f; velY1 = -velY1 * DAMPING_FACTOR; }

  if (posX2 < 1.0f) { posX2 = 1.0f; velX2 = -velX2 * DAMPING_FACTOR; }
  if (posX2 > 6.0f) { posX2 = 6.0f; velX2 = -velX2 * DAMPING_FACTOR; }
  if (posY2 < 1.0f) { posY2 = 1.0f; velY2 = -velY2 * DAMPING_FACTOR; }
  if (posY2 > 6.0f) { posY2 = 6.0f; velY2 = -velY2 * DAMPING_FACTOR; }

  float dx = posX2 - posX1; float dy = posY2 - posY1;
  float distance = sqrt(dx * dx + dy * dy);
  float minAllowedDistance = 3.0f; 

  if (distance < minAllowedDistance && distance > 0.0f) {
    float nx = dx / distance; float ny = dy / distance;
    float overlap = minAllowedDistance - distance;
    posX1 -= nx * overlap * 0.5f; posY1 -= ny * overlap * 0.5f;
    posX2 += nx * overlap * 0.5f; posY2 += ny * overlap * 0.5f;

    float kx = velX1 - velX2; float ky = velY1 - velY2;
    float p = 2.0f * (nx * kx + ny * ky) / 2.0f;
    velX1 -= p * nx * DAMPING_FACTOR; velY1 -= p * ny * DAMPING_FACTOR;
    velX2 += p * nx * DAMPING_FACTOR; velY2 += p * ny * DAMPING_FACTOR;
  }
}

void triggerBlinkIndicator() {
  for (int i = 0; i < 2; i++) {
    matrix.clear();
    matrix.show();
    delay(80);
    renderFloatingDice(activeDiceVal1, activeDiceVal2, posX1, posY1, posX2, posY2, colorHueCounter, true);
    delay(100);
  }
}

// Draw patterns dynamically at the calculated floating locations
void renderFloatingDice(int val1, int val2, float x1, float y1, float x2, float y2, uint16_t currentHue, bool forceWhite) {
  matrix.clear();

  uint32_t color1 = forceWhite ? matrix.Color(255, 255, 255) : matrix.ColorHSV(currentHue, 255, DICE_BRIGHTNESS);
  uint32_t color2 = forceWhite ? matrix.Color(255, 255, 255) : matrix.ColorHSV(currentHue + 10000, 255, DICE_BRIGHTNESS);

  uint32_t bg1 = forceWhite ? matrix.Color(15, 15, 15) : matrix.ColorHSV(currentHue + 32768, 255, BACKGROUND_BRIGHTNESS);
  uint32_t bg2 = forceWhite ? matrix.Color(15, 15, 15) : matrix.ColorHSV(currentHue + 10000 + 32768, 255, BACKGROUND_BRIGHTNESS);

  uint16_t pat1 = dicePatterns[val1 - 1];
  uint16_t pat2 = dicePatterns[val2 - 1];

  int startX1 = (int)(x1 + 0.5f) - 1; int startY1 = (int)(y1 + 0.5f) - 1;
  int startX2 = (int)(x2 + 0.5f) - 1; int startY2 = (int)(y2 + 0.5f) - 1;

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int tx = startX1 + col; int ty = startY1 + row;
      if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8) {
        if ((pat1 >> (row * 3 + col)) & 0x01) {
          matrix.setPixelColor(getPixelIndex(tx, ty), color1);
        } else {
          matrix.setPixelColor(getPixelIndex(tx, ty), bg1);
        }
      }
    }
  }

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      int tx = startX2 + col; int ty = startY2 + row;
      if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8) {
        if ((pat2 >> (row * 3 + col)) & 0x01) {
          matrix.setPixelColor(getPixelIndex(tx, ty), color2);
        } else {
          if (matrix.getPixelColor(getPixelIndex(tx, ty)) != color1) {
            matrix.setPixelColor(getPixelIndex(tx, ty), bg2);
          }
        }
      }
    }
  }
  matrix.show();
}
