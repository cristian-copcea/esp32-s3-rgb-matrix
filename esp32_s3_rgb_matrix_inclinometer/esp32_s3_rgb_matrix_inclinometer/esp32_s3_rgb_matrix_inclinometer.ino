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

// --- INCLINOMETER CONFIGURATION PARAMETERS ---
const float MAX_ANGLE_THRESHOLD = 25.0; 
const float LEVEL_DEADZONE      = 1.5;  
const unsigned long BLINK_INTERVAL_MS = 250; 

// --- TIMING & STILLNESS CONFIGURATION ---
const unsigned long STILLNESS_TIMEOUT_MS = 4000;   
const unsigned long SCREENSAVER_TIMEOUT_MS = 120000; //2 MINUTES
const float MOTION_THRESHOLD = 35.0;             
const unsigned long SCROLL_SPEED_MS = 160;       

// --- ACCUMULATIVE SHAKE DETECTION CONFIGURATION ---
const float SHAKE_G_THRESHOLD = 800.0;        
const unsigned long REQUIRED_SHAKE_DURATION_MS = 500; 

// State machine trackers
unsigned long lastBlinkTime = 0; 
bool isBlinkOn = true;
unsigned long lastMotionTime = 0; 
float lastAx = 0, lastAy = 0, lastAz = 0;
bool isShowingTextMode = false; 
int activeOperationMode = 0; 
unsigned long shakeStartTime = 0; 
bool wasShakingLastFrame = false;
int boardOrientation = 0; 

// Marquee text mechanics variables
String marqueeText = ""; 
int scrollX = 8; 
unsigned long lastScrollTime = 0; 

// Screensaver state trackers
unsigned long lastScreensaverSparkTime = 0;
int activeSparkIndex = -1; 
uint32_t sparkColor = 0;

// Geiger indicator state trackers
unsigned long lastGeigerBlinkTime = 0;
bool isGeigerOn = true;

// Custom 3x5 font allocation array
uint16_t font3x5[35]; 
void initializeFont() {
  auto packG = [](int r0_0, int r0_1, int r0_2,
                  int r1_0, int r1_1, int r1_2,
                  int r2_0, int r2_1, int r2_2,
                  int r3_0, int r3_1, int r3_2,
                  int r4_0, int r4_1, int r4_2) -> uint16_t {
    return (r0_0 << 14) | (r0_1 << 13) | (r0_2 << 12) |
           (r1_0 << 11) | (r1_1 << 10) | (r1_2 << 9)  |
           (r2_0 << 8)  | (r2_1 << 7)  | (r2_2 << 6)  |
           (r3_0 << 5)  | (r3_1 << 4)  | (r3_2 << 3)  |
           (r4_0 << 2)  | (r4_1 << 1)  | (r4_2 << 0);
  };

  font3x5[0]  = packG(1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1); // 0
  font3x5[1]  = packG(0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1); // 1
  font3x5[2]  = packG(1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1); // 2
  font3x5[3]  = packG(1,1,1, 0,0,1, 1,1,1, 0,0,1, 1,1,1); // 3
  font3x5[4]  = packG(1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1); // 4
  font3x5[5]  = packG(1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1); // 5
  font3x5[6]  = packG(1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1); // 6
  font3x5[7]  = packG(1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0); // 7
  font3x5[8]  = packG(1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1); // 8
  font3x5[9]  = packG(1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1); // 9
  
  font3x5[10] = packG(0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0); // 10: [Space]
  font3x5[11] = packG(0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,1,0); // 11: . (Period)
  font3x5[12] = packG(0,0,0, 0,0,0, 1,1,1, 0,0,0, 0,0,0); // 12: - (Minus Sign)
  font3x5[13] = packG(1,1,1, 1,0,1, 1,1,1, 0,0,0, 0,0,0); // 13: * (Degree Sign) 
  font3x5[14] = packG(1,0,1, 1,0,1, 0,1,0, 1,0,1, 1,0,1); // 14: X
  font3x5[15] = packG(1,0,1, 1,0,1, 0,1,0, 0,1,0, 0,1,0); // 15: Y
  font3x5[16] = packG(0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0); // 16: :
  font3x5[17] = packG(0,0,0, 0,1,0, 0,0,0, 0,1,0, 1,0,0); // 17: ; 
  font3x5[18] = packG(1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0); // 18: P
  font3x5[19] = packG(1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1); // 18: O
  font3x5[20] = packG(1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1); // 18: L
  font3x5[21] = packG(1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0); // 18: B
  font3x5[22] = packG(1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1); // 18: C
  font3x5[23] = packG(1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0); // 18: D 
  font3x5[24] = packG(0,1,0, 0,1,0, 0,1,0, 0,1,0, 0,1,0); // 18: I
  font3x5[25] = packG(1,1,1, 1,0,0, 1,0,0, 1,0,1, 1,1,1); // 18: G
  font3x5[26] = packG(1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0); // 18: T
  font3x5[27] = packG(1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1); // 18: A
  font3x5[28] = packG(1,0,1, 1,1,1, 1,0,1, 1,0,1, 1,0,1); // 18: M
  font3x5[29] = packG(1,1,1, 1,0,0, 1,1,1, 1,0,0, 1,1,1); // 18: E
  font3x5[30] = packG(1,1,1, 1,0,0, 1,1,1, 1,0,0, 1,0,0); // 18: F
  font3x5[31] = packG(1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1); // 18: U
  font3x5[32] = packG(1,0,1, 1,1,1, 1,0,1, 1,0,1, 1,0,1); // 18: N
  font3x5[33] = packG(1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1); // 18: R
  font3x5[34] = packG(1,0,1, 1,0,1, 1,0,1, 1,0,1, 0,1,0); // 18: V
}

int getCharIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c == ' ') return 10; 
  if (c == '.') return 11; 
  if (c == '-') return 12; 
  if (c == '*') return 13; 
  if (c == 'X' || c == 'x') return 14; 
  if (c == 'Y' || c == 'y') return 15;
  if (c == ':') return 16; 
  if (c == ';') return 17; 
  if (c == 'p' || c == 'P') return 18;
  if (c == 'o' || c == 'O') return 19;
  if (c == 'l' || c == 'L') return 20;
  if (c == 'b' || c == 'B') return 21;
  if (c == 'c' || c == 'C') return 22;
  if (c == 'd' || c == 'D') return 23;
  if (c == 'i' || c == 'I') return 24;
  if (c == 'g' || c == 'G') return 25;
  if (c == 't' || c == 'T') return 26;
  if (c == 'a' || c == 'A') return 27;
  if (c == 'm' || c == 'M') return 28;
  if (c == 'e' || c == 'E') return 29;
  if (c == 'f' || c == 'F') return 30; 
  if (c == 'u' || c == 'U') return 31;
  if (c == 'n' || c == 'N') return 32;
  if (c == 'r' || c == 'R') return 33;
  if (c == 'v' || c == 'V') return 34;
  return 10; 
}
void drawCharRotated(char c, int xOffset, int yOffset, uint32_t color, bool rotateYAxis) {
  if (color == matrix.Color(0,0,0)) return; 
  int idx = getCharIndex(c); uint16_t glyph = font3x5[idx];
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      int bitIdx = (4 - row) * 3 + (2 - col);
      if ((glyph >> bitIdx) & 0x01) {
        int px = 7 - (xOffset + col); int py = 7 - (yOffset + row);
        int finalX = px; int finalY = py;

        if (activeOperationMode == 1) { 
          if (boardOrientation == 1)      { finalX = 7 - py; finalY = px; } 
          else if (boardOrientation == 2) { finalX = 7 - px; finalY = 7 - py; } 
          else if (boardOrientation == 3) { finalX = py; finalY = 7 - px; }
        } else if (rotateYAxis) { 
          finalX = py; finalY = 7 - px;
        }
        if (finalX >= 0 && finalX < 8 && finalY >= 0 && finalY < 8) matrix.setPixelColor((finalY * 8) + finalX, color);
      }
    }
  }
}

int getPixelIndex(int x, int y) {
  if (x < 0 || x > 7 || y < 0 || y > 7) return -1; 
  return (y * 8) + x;
}

void displaytext(String text, int loops, int R, int G, int B) {

    // ---  STARTUP TEXT MARQUEE LAYER ---
  String bootText = text;
  int bootScrollX = 8;
  int textWidth = bootText.length() * 4;
  int totalLoops = loops; // Number of times to scroll the phrase
  
  while (totalLoops > 0) {
    matrix.clear();
    int currentX = bootScrollX;
    
    // Render text string onto canvas
    for (int i = 0; i < bootText.length(); i++) {
      drawCharRotated(bootText[i], currentX, 2, matrix.Color(R, G, B), false); 
      currentX += 4;
    }
    
    matrix.show();
    delay(50); // Controls the scrolling speed at boot
    bootScrollX--;
    
    // Reset string position for next loop round
    if (bootScrollX < -textWidth) {
      bootScrollX = 8;
      totalLoops--;
    }
  }
  
  // Clear display so it is fresh for the main application loops
  matrix.clear();
  matrix.show();

}
void setup() {
  Serial.begin(115200); delay(500); 
  matrix.begin(); matrix.setBrightness(30); matrix.fill(matrix.Color(0, 0, 150)); matrix.show(); delay(300);
  initializeFont();
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(400000); 
  if (!imu.begin(Wire, QMI8658_ADDRESS_HIGH)) {
    if (!imu.begin(Wire, QMI8658_ADDRESS_LOW)) {
      matrix.fill(matrix.Color(150, 0, 0)); matrix.show(); while (1) { delay(10); } 
    }
  }
  matrix.clear(); matrix.show();
  imu.setAccelUnit_mg(true); imu.setGyroUnit_dps(true);   
  imu.setAccelRange(QMI8658_ACCEL_RANGE_4G); imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  randomSeed(micros()); lastMotionTime = millis();

    // ---  STARTUP TEXT MARQUEE LAYER ---
  displaytext("  Poloboc digital 2026 ",1,0,0,200);
}

int currentMarqueeSegment = 0; int continuousShakeFrames = 0;
unsigned long lastSensorPollTime = 0; // Timer for low-frequency wake scans

void loop() {
  QMI8658_Data sensor; unsigned long currentTime = millis();
  bool isScreensaverActive = (currentTime - lastMotionTime > SCREENSAVER_TIMEOUT_MS);
  
  // FIX: If the screensaver is active, slow down the sensor polling to once every 250ms to save power.
  // If the screensaver is not active, poll at max hardware speed (every frame).
  if (!isScreensaverActive || (currentTime - lastSensorPollTime >= 250)) {
    lastSensorPollTime = currentTime;
    
    if (imu.readSensorData(sensor)) {
      float rawAx = sensor.accelX; float rawAy = sensor.accelY; float rawAz = sensor.accelZ;
      float rawJerkDelta = abs(rawAx - lastAx) + abs(rawAy - lastAy) + abs(rawAz - lastAz);

      // Shake detection checks
      if (rawJerkDelta > SHAKE_G_THRESHOLD && !isScreensaverActive) { 
        if (!wasShakingLastFrame) { shakeStartTime = currentTime; continuousShakeFrames = 0; wasShakingLastFrame = true; }
        continuousShakeFrames++;
        if ((currentTime - shakeStartTime >= REQUIRED_SHAKE_DURATION_MS) && (continuousShakeFrames >= 8)) {
          activeOperationMode = (activeOperationMode == 0) ? 1 : 0; 
          wasShakingLastFrame = false; continuousShakeFrames = 0; shakeStartTime = currentTime; 
          lastMotionTime = currentTime; isShowingTextMode = false; currentMarqueeSegment = 0;
          matrix.clear(); uint32_t flashCol = (activeOperationMode == 0) ? matrix.Color(0, 0, 150) : matrix.Color(100, 0, 100);
          matrix.fill(flashCol); matrix.show(); delay(200); matrix.clear(); matrix.show(); 
          
          boardOrientation=0;
          switch (activeOperationMode) {
            case 0: displaytext("  Mod de functionare: Clinometru ",1,100,100,200); break;
            case 1: displaytext("  Mod de functionare: Nivela ",1,100,100,200); break;
            default:  break;
          }
        return; 
        }
      } else {
        if (currentTime - shakeStartTime > 150) { wasShakingLastFrame = false; continuousShakeFrames = 0; }
      }

      float ax = (lastAx * 0.8f) + (rawAx * 0.2f); float ay = (lastAy * 0.8f) + (rawAy * 0.2f); float az = (lastAz * 0.8f) + (rawAz * 0.2f);
      
      // MOTION WAKE TRIGGER: Real kinetic acceleration spikes reset the inactivity clock instantly
      if (abs(ax - lastAx) > MOTION_THRESHOLD || abs(ay - lastAy) > MOTION_THRESHOLD || abs(az - lastAz) > MOTION_THRESHOLD) {
        lastMotionTime = currentTime; 
        if (isShowingTextMode) isShowingTextMode = false; 
        isScreensaverActive = false; // Disable flag immediately to handle instant UI redraws
      }
      lastAx = rawAx; lastAy = rawAy; lastAz = rawAz; 

      if (abs(ax) > abs(ay)) { boardOrientation = (ax > 0) ? 2 : 0; } else { boardOrientation = (ay > 0) ? 3 : 1; }
      float pitch = atan2(-ay, sqrt(ax * ax + az * az)) * 57.2958; float roll = atan2(ax, sqrt(ay * ay + az * az)) * 57.2958;

      float targetTrackAngle = (activeOperationMode == 0) ? max(abs(pitch), abs(roll)) : abs(abs(pitch) > abs(roll) ? abs(pitch) - 90.0f : abs(roll) - 90.0f);
      float tiltRatio = constrain(targetTrackAngle / MAX_ANGLE_THRESHOLD, 0.0f, 1.0f);
      uint32_t liveColor = matrix.Color(0, 0, 0);

      if (targetTrackAngle <= LEVEL_DEADZONE) {
        if (currentTime - lastBlinkTime >= BLINK_INTERVAL_MS) { isBlinkOn = !isBlinkOn; lastBlinkTime = currentTime; }
        liveColor = isBlinkOn ? matrix.Color(255, 0, 0) : matrix.Color(0, 0, 0); 
      } else {
        liveColor = matrix.Color((uint8_t)((1.0f - tiltRatio) * 255.0f), (uint8_t)(tiltRatio * 255.0f), 0); 
      }

      // --- RENDERING ROUTER INTERFACE ---
      if (isScreensaverActive) {
        // MODE C: High-Efficiency Visual Screensaver Mode
        matrix.clear();
        if (currentTime - lastScreensaverSparkTime >= 1000) {
          lastScreensaverSparkTime = currentTime; activeSparkIndex = random(0, 64);
          sparkColor = matrix.Color(random(50, 255), random(50, 255), random(50, 255));
        }
        if (currentTime - lastScreensaverSparkTime < 250 && activeSparkIndex != -1) {
          matrix.setPixelColor(activeSparkIndex, sparkColor);
        }
        matrix.show();
      }
      else if (currentTime - lastMotionTime > STILLNESS_TIMEOUT_MS) {
        // MODE B: Stillness Triggered Marquee Mode
        if (!isShowingTextMode) { 
        isShowingTextMode = true; 
        currentMarqueeSegment = 0; 
        
          // FIX: Check the active operation mode immediately on the first pass
          if (activeOperationMode == 1) {
          // If in Verticality mode, start with V immediately
            marqueeText = "  V:" + String((abs(pitch) > abs(roll)) ? abs(pitch) : abs(roll), 2) + "* ";
          } else {
            // If in Planarity mode, start with X=
            marqueeText = "  X=" + String(pitch, 2) + "* "; 
          }
          scrollX = 8; 
        }
        if (currentTime - lastScrollTime >= SCROLL_SPEED_MS) {
          lastScrollTime = currentTime; scrollX--; int textLengthPixels = marqueeText.length() * 4; 
          if (scrollX < -textLengthPixels) {
            if (activeOperationMode == 1) { marqueeText = "  V:" + String((abs(pitch) > abs(roll)) ? abs(pitch) : abs(roll), 2) + "* "; scrollX = 8; } 
            else {
              currentMarqueeSegment = (currentMarqueeSegment == 0) ? 1 : 0;
              marqueeText = (currentMarqueeSegment == 1) ? "  Y:" + String(roll, 2) + "* " : "  X:" + String(pitch, 2) + "* ";
              scrollX = 8;
            }
          }
        }
        matrix.clear(); bool shouldRotateY = (activeOperationMode == 0 && currentMarqueeSegment == 1); int currentX = scrollX;
        for (int i = 0; i < marqueeText.length(); i++) { drawCharRotated(marqueeText[i], currentX, 2, liveColor, shouldRotateY); currentX += 4; }
        

      } else {
        // MODE A: Realtime Crosshair / Alignment Bar Mode
        matrix.clear();
        if (activeOperationMode == 0) {
          int targetX = round(constrain(3.5 - (pitch / MAX_ANGLE_THRESHOLD) * 3.5, 1.0, 6.0));
          int targetY = round(constrain(3.5 - (roll / MAX_ANGLE_THRESHOLD) * 3.5, 1.0, 6.0));
          int pI;
          pI = getPixelIndex(targetX, targetY);     if (pI != -1) matrix.setPixelColor(pI, liveColor); 
          pI = getPixelIndex(targetX - 1, targetY); if (pI != -1) matrix.setPixelColor(pI, liveColor); 
          pI = getPixelIndex(targetX + 1, targetY); if (pI != -1) matrix.setPixelColor(pI, liveColor); 
          pI = getPixelIndex(targetX, targetY - 1); if (pI != -1) matrix.setPixelColor(pI, liveColor); 
          pI = getPixelIndex(targetX, targetY + 1); if (pI != -1) matrix.setPixelColor(pI, liveColor); 
        } else {
          int centralRowOffset = 3;
          if (boardOrientation == 0) centralRowOffset = round(constrain(3.5 + ((pitch + 90.0f) / MAX_ANGLE_THRESHOLD) * 3.5, 0.0, 7.0));
          else if (boardOrientation == 2) centralRowOffset = round(constrain(3.5 + ((pitch - 90.0f) / MAX_ANGLE_THRESHOLD) * 3.5, 0.0, 7.0));
          else if (boardOrientation == 1) centralRowOffset = round(constrain(3.5 - ((roll + 90.0f) / MAX_ANGLE_THRESHOLD) * 3.5, 0.0, 7.0));
          else if (boardOrientation == 3) centralRowOffset = round(constrain(3.5 - ((roll - 90.0f) / MAX_ANGLE_THRESHOLD) * 3.5, 0.0, 7.0));

          for(int col = 0; col < 8; col++) {
            int pI = (boardOrientation == 0 || boardOrientation == 2) ? getPixelIndex(col, centralRowOffset) : getPixelIndex(centralRowOffset, col);
            if (pI != -1) matrix.setPixelColor(pI, liveColor);
          }
        }
      }
      // --- FIX: VISUAL GEIGER-COUNTER PULSE ENGINE (GLOBAL OUTSIDE EXECUTION LAYER) ---
      // Moved completely outside the Mode conditionals so it executes during marquees too!
      unsigned long geigerInterval = (unsigned long)(tiltRatio * 600.0f);
      if (targetTrackAngle <= LEVEL_DEADZONE) { isGeigerOn = true; } 
      else {
        if (geigerInterval < 60) geigerInterval = 60;
        if (currentTime - lastGeigerBlinkTime >= geigerInterval) { isGeigerOn = !isGeigerOn; lastGeigerBlinkTime = currentTime; }
      }
      
      // Inject Violet indicator light onto physical pixel slot 7 right before committing the canvas frame
      matrix.setPixelColor(7, isGeigerOn ? matrix.Color(0, 130, 180) : matrix.Color(0, 0, 0));
      matrix.show();
      
    }
  }
  
  // Dynamic performance delay throttling:
  // Runs at an ultra-low 4Hz draw cycle when idle to conserve energy, 
  // but instantly scales back up to 60Hz performance when handled!
  delay(isScreensaverActive ? 250 : 15); 
}
