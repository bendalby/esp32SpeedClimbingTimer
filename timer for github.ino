// Last edited: 2026-06-13
// Changes: Added client-side RAF timer interpolation to main page and projector page
//          for smooth display between WebSocket updates.

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <FastLED.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// Hardware Config
#define START_BUTTON 19

#define FOOT_SENSOR_LEFT 13
#define STOP_SENSOR_LEFT_KIDS 14
#define STOP_SENSOR_LEFT 27

#define FOOT_SENSOR_RIGHT 32
#define STOP_SENSOR_RIGHT_KIDS 26
#define STOP_SENSOR_RIGHT 33


#define AUDIO_PIN 22

// LED Strip Config
#define LED_PIN_LEFT 17
#define LED_PIN_RIGHT 18
#define NUM_LEDS_PER_STRIP 60

CRGB ledsLeft[NUM_LEDS_PER_STRIP];
CRGB ledsRight[NUM_LEDS_PER_STRIP];

// KID MODE STUFF
bool kidsModeSensorsEnabled = true;
bool stopLeftKidsPressed = false;
bool stopRightKidsPressed = false;
bool lastKidsModeSensorsEnabled = true;

bool friendlyFalseStartsEnabled = false;

// WiFi Config
const char* ap_ssid = "Gravity Worx Speed Timer";
const char* ap_password = "";  // Min 8 chars, or "" for open network

// ── Gym WiFi + Google Sheets ───────────────────────────────────────────────
const char* sta_ssid      = 
const char* sta_password  = 
const char* appsScriptUrlDefault = 
String appsScriptUrl = "";   // loaded from NVS at boot
bool   sheetsLinked  = false;
Preferences prefs;

// ── Event display (editable from operator page) ────────────────────────────
String eventTitle    = "Gravity Worx Speed Timer";
String eventSubtitle = "";

// ── Heat tracking ──────────────────────────────────────────────────────────
int    currentHeatNum  = 0;
String wallAName       = "";
String wallBName       = "";
String currentCategory = "";
String currentRound    = "";
bool   restWarnA       = false;
bool   restWarnB       = false;
int    restSecsA       = 0;
int    restSecsB       = 0;

bool   sheetsConnected   = false;
bool   heatDataLoaded    = false;
bool   pendingHeatFetch  = false; // set to trigger a fetch on next loop iteration

struct HeatInfo { int num; String round; String wallA; String wallB; String category; String bibA; String bibB; bool restWarnA; bool restWarnB; int restSecsA; int restSecsB; };
HeatInfo upcomingHeats[10];
int upcomingHeatCount = 0;

unsigned long lastHeatFetch = 0;
unsigned long lastWifiRetry = 0;
const unsigned long HEAT_FETCH_INTERVAL = 60000;
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Forward declarations (needed because raw string literals confuse the Arduino preprocessor)
void sendWebSocketUpdate();
void fetchNextHeats();
void loadHeatsFromJson(JsonArray arr);
void advanceHeat();
void handleApiReset();
void handleApiStart();
void handleApiStop();
void handleApiStatus();
void handleApiToggleKidsMode();
void handleApiConfirmHeat();
void handleApiSkipHeat();
void handleApiFetchHeats();
void handleApiSetTitle();
void handleApiGetSettings();
void handleApiSetSettings();
void handleRoot();
void handleProjector();
void handleCaptivePortal();

//loop check
unsigned long loopCount = 0;
unsigned long lastTime = 0;
unsigned long lastLoopTime = 0;

// Audio Config
#define LEDC_CHANNEL 0
#define LEDC_RESOLUTION 8

struct AudioStep {
  int frequency;
  unsigned long duration;
};

const AudioStep audioSequence[] = {
  { 0, 500 }, { 880, 250 }, { 0, 750 }, { 880, 250 }, { 0, 750 }, { 1760, 150 }
};

const AudioStep falseStartSequence[] = {
  { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }, { 1568, 100 }, { 0, 100 }
};

const int AUDIO_SEQUENCE_LENGTH = sizeof(audioSequence) / sizeof(AudioStep);
const int FALSE_START_SEQUENCE_LENGTH = sizeof(falseStartSequence) / sizeof(AudioStep);

// Global Variables
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Timer state
bool isTimerRunningLeft = false;
bool isTimerRunningRight = false;
unsigned long timerStartTime = 0;
unsigned long currentElapsedTime = 0;

// Audio state
bool isPlayingAudio = false;
bool isPlayingFalseStart = false;
int currentAudioStep = 0;
unsigned long audioStepStartTime = 0;
unsigned long audioSequenceStartTime = 0;
unsigned long audioEndTime = 0;

// Button/sensor state
bool startButtonPressed = false;
bool stopLeftPressed = false;
bool stopRightPressed = false;
bool footLeftPressed = false;
bool footRightPressed = false;

// False start tracking
unsigned long startSignalTime = 0;
long leftReactionTime = 0;
long rightReactionTime = 0;
bool leftReactionCalculated = false;
bool rightReactionCalculated = false;
bool falseStartOccurred = false;
bool leftFalseStart = false;
bool rightFalseStart = false;
bool leftFootValidDuringAudio = true;
bool rightFootValidDuringAudio = true;
bool falseStartAudioPlayed = false;
unsigned long leftFalseStartTime = 0;
unsigned long rightFalseStartTime = 0;
const unsigned long FALSE_START_REACTION_TIME_CUTOFF = 100;  //if reaction is under this time a false start is detected - 100ms

// Mode and lane tracking
bool singlePlayerMode = true;
bool leftLaneActive = false;
bool rightLaneActive = false;

// Timing results
long reactionTimeLeft = 0;
long reactionTimeRight = 0;
unsigned long completionTimeLeft = 0;
unsigned long completionTimeRight = 0;
bool leftFinished = false;
bool rightFinished = false;

// State tracking for updates
bool lastTimerRunning = false;
bool lastPlayingAudio = false;
bool lastPlayingFalseStart = false;
bool lastFalseStartOccurred = false;
bool lastLeftFalseStart = false;
bool lastRightFalseStart = false;
bool lastFootLeftPressed = false;
bool lastFootRightPressed = false;
bool lastSinglePlayerMode = false;
bool lastLeftFinished = false;
bool lastRightFinished = false;
long lastReactionTimeLeft = 0;
long lastReactionTimeRight = 0;
unsigned long lastCompletionTimeLeft = 0;
unsigned long lastCompletionTimeRight = 0;
bool lastTimerRunningLeft = false;
bool lastTimerRunningRight = false;

// Timing control
unsigned long lastEventTime = 0;
bool resetTimeoutActive = false;
const unsigned long RESET_TIMEOUT = 1300;
unsigned long lastButtonCheck = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long BUTTON_DEBOUNCE = 10;
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 111;

// Auto-start
unsigned long footPressStartTime = 0;
bool footHeldForAutoStart = false;
const unsigned long AUTO_START_DELAY = 3000;

// WiFi timeout
unsigned long startTime = millis();
unsigned long timeout = 30000;

void handleCaptivePortal() {
  // Redirect all requests to the main page for captive portal
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
  handleRoot();
}

// Utility Functions
bool isAnyTimerRunning() {
  return isTimerRunningLeft || isTimerRunningRight;
}

bool isReadyState() {
  return !isPlayingAudio && !isPlayingFalseStart && !isAnyTimerRunning();
}

bool canStartCompetition() {
  if (singlePlayerMode) {
    return (footLeftPressed || footRightPressed) && isReadyState();
  }
  return footLeftPressed && footRightPressed && isReadyState();
}

void setLaneActivity() {
  if (singlePlayerMode) {
    leftLaneActive = footLeftPressed;
    rightLaneActive = footRightPressed;
  } else {
    leftLaneActive = true;
    rightLaneActive = true;
  }
}

void resetCompetitionState() {
  falseStartOccurred = false;
  leftFalseStart = false;
  rightFalseStart = false;
  leftFootValidDuringAudio = true;
  rightFootValidDuringAudio = true;
  falseStartAudioPlayed = false;
  leftFalseStartTime = 0;
  rightFalseStartTime = 0;
  reactionTimeLeft = 0;
  reactionTimeRight = 0;
  completionTimeLeft = 0;
  completionTimeRight = 0;
  leftFinished = false;
  rightFinished = false;
  resetTimeoutActive = false;
  lastEventTime = 0;
  startSignalTime = 0;
  leftReactionTime = 0;
  rightReactionTime = 0;
  leftReactionCalculated = false;
  rightReactionCalculated = false;
}

// LED Functions
void initializeLEDs() {
  FastLED.addLeds<WS2812B, LED_PIN_LEFT, GRB>(ledsLeft, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_RIGHT, GRB>(ledsRight, NUM_LEDS_PER_STRIP);
  FastLED.setBrightness(128);
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, CRGB::Black);
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, CRGB::Black);
  FastLED.show();
}

void setLeftLEDs(CRGB color) {
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, color);
  FastLED.show();
}

void setRightLEDs(CRGB color) {
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, color);
  FastLED.show();
}

void turnOffAllLEDs() {
  fill_solid(ledsLeft, NUM_LEDS_PER_STRIP, CRGB::Black);
  fill_solid(ledsRight, NUM_LEDS_PER_STRIP, CRGB::Black);
  FastLED.show();
}

void setLEDsBasedOnState(bool isLeft, bool falseStart, CRGB normalColor) {
  bool showRed = falseStart && !friendlyFalseStartsEnabled;
  if (isLeft) {
    setLeftLEDs(showRed ? CRGB::Red : normalColor);
  } else {
    setRightLEDs(showRed ? CRGB::Red : normalColor);
  }
}

// Audio Functions
void playTone(int frequency) {
  ledcChangeFrequency(AUDIO_PIN, frequency, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 128);
}

void stopTone() {
  ledcWrite(AUDIO_PIN, 0);
}

void startAudioSequence() {
  isPlayingAudio = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();
  audioSequenceStartTime = millis();
  setLeftLEDs(CRGB::Black);
  setRightLEDs(CRGB::Black);

  if (audioSequence[0].frequency > 0) {
    playTone(audioSequence[0].frequency);
  } else {
    stopTone();
  }
}

void startFalseStartSequence() {
  if (friendlyFalseStartsEnabled) {
      completeFalseStartSequence();
      return;
    }
  isPlayingFalseStart = true;
  currentAudioStep = 0;
  audioStepStartTime = millis();

  if (falseStartSequence[0].frequency > 0) {
    playTone(falseStartSequence[0].frequency);
  } else {
    stopTone();
  }
}

void handleAudioLEDs(const AudioStep* sequence, int frequency) {
  if (isPlayingAudio) {
    if (frequency == 880 || frequency == 1760) {
      setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Green);
      setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Green);
    } else {
      setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Black);
      setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Black);
    }
  } else if (isPlayingFalseStart && !friendlyFalseStartsEnabled) {
    if (frequency == 1568) {
      if (leftFalseStart) setLeftLEDs(CRGB::Red);
      if (rightFalseStart) setRightLEDs(CRGB::Red);
      if (!leftFalseStart) setLeftLEDs(CRGB::Black);
      if (!rightFalseStart) setRightLEDs(CRGB::Black);
    } else {
      if (leftFalseStart) setLeftLEDs(CRGB::Black);
      if (rightFalseStart) setRightLEDs(CRGB::Black);
    }
  }
}

void updateAudioSequence() {
  if (!isPlayingAudio && !isPlayingFalseStart) return;

  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - audioStepStartTime;

  const AudioStep* sequence = isPlayingAudio ? audioSequence : falseStartSequence;
  int sequenceLength = isPlayingAudio ? AUDIO_SEQUENCE_LENGTH : FALSE_START_SEQUENCE_LENGTH;

  if (elapsed >= sequence[currentAudioStep].duration) {
    currentAudioStep++;

    if (currentAudioStep >= sequenceLength) {
      stopTone();
      if (isPlayingAudio) {
        isPlayingAudio = false;
        audioEndTime = millis();
        completeAudioSequence();
      } else if (isPlayingFalseStart) {
        isPlayingFalseStart = false;
        completeFalseStartSequence();
      }
      currentAudioStep = 0;
      return;
    }

    audioStepStartTime = currentTime;

    // START TIMER WHEN 1760Hz TONE BEGINS (step 5 in the sequence)
    if (isPlayingAudio && currentAudioStep == 5 && sequence[currentAudioStep].frequency == 1760) {
      startTimer();
      audioEndTime = millis();  // Set this for reaction time calculations
    }

    if (sequence[currentAudioStep].frequency > 0) {
      playTone(sequence[currentAudioStep].frequency);
      handleAudioLEDs(sequence, sequence[currentAudioStep].frequency);
    } else {
      stopTone();
      handleAudioLEDs(sequence, 0);
    }
  }
}

void completeAudioSequence() {
  isPlayingAudio = false;
  currentAudioStep = 0;

  if (falseStartOccurred && !falseStartAudioPlayed) {
    falseStartAudioPlayed = true;
    if (!friendlyFalseStartsEnabled) {
      startFalseStartSequence();
    } else {
      completeFalseStartSequence();
    }
  }

  sendWebSocketUpdate();
}

void completeFalseStartSequence() {
  isPlayingFalseStart = false;
  currentAudioStep = 0;

  setLEDsBasedOnState(true, !friendlyFalseStartsEnabled && leftFalseStart, CRGB::Black);
  setLEDsBasedOnState(false, !friendlyFalseStartsEnabled && rightFalseStart, CRGB::Black);

  resetTimeoutActive = true;
  lastEventTime = millis();

  sendWebSocketUpdate();
}

// Timer Functions
void startTimer() {
  if (!singlePlayerMode || leftLaneActive) {
    isTimerRunningLeft = true;
  }
  if (!singlePlayerMode || rightLaneActive) {
    isTimerRunningRight = true;
  }

  timerStartTime = millis();
  startSignalTime = millis();  // Mark start signal time for reaction calculation
  sendWebSocketUpdate();
}

void stopTimer() {
  isTimerRunningLeft = false;
  isTimerRunningRight = false;
  currentElapsedTime = millis() - timerStartTime;
  sendWebSocketUpdate();
}

void resetTimer() {
  isTimerRunningLeft = false;
  isTimerRunningRight = false;
  currentElapsedTime = 0;
  timerStartTime = 0;
  audioEndTime = 0;
  leftLaneActive = false;
  rightLaneActive = false;

  resetCompetitionState();

  // Reset state tracking
  lastTimerRunningLeft = false;
  lastTimerRunningRight = false;
  lastPlayingAudio = false;
  lastPlayingFalseStart = false;
  lastFalseStartOccurred = false;
  lastLeftFalseStart = false;
  lastRightFalseStart = false;
  lastFootLeftPressed = footLeftPressed;
  lastFootRightPressed = footRightPressed;
  lastSinglePlayerMode = singlePlayerMode;
  lastLeftFinished = false;
  lastRightFinished = false;
  lastReactionTimeLeft = 0;
  lastReactionTimeRight = 0;
  lastCompletionTimeLeft = 0;
  lastCompletionTimeRight = 0;
  lastKidsModeSensorsEnabled = kidsModeSensorsEnabled;

  turnOffAllLEDs();
  sendWebSocketUpdate();
}

void updateTimer() {
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }
}

// Button/Sensor Handling
void handleFootSensorPress(bool isLeft) {
  if (singlePlayerMode && isAnyTimerRunning()) {
    if (resetTimeoutActive && (millis() - lastEventTime < RESET_TIMEOUT)) {
      return;
    }
    handleApiReset();
    return;
  }

  if (singlePlayerMode && isReadyState()) {
    bool otherFootPressed = isLeft ? footRightPressed : footLeftPressed;
    if (!otherFootPressed) {
      footPressStartTime = millis();
      footHeldForAutoStart = true;
    }
  }

  if (isReadyState()) {
    if (isLeft) {
      setLeftLEDs(CRGB::Green);
    } else {
      setRightLEDs(CRGB::Green);
    }
  }
}

void handleFootSensorRelease(bool isLeft) {
  if (singlePlayerMode && footHeldForAutoStart) {
    bool otherFootPressed = isLeft ? footLeftPressed : footRightPressed;
    if (!otherFootPressed) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
    }
  }

  if (isReadyState()) {
    if (isLeft) {
      setLeftLEDs(CRGB::Black);
    } else {
      setRightLEDs(CRGB::Black);
    }
  }

  // Check for false start if audio is playing but timer hasn't started yet
  if (isPlayingAudio && startSignalTime == 0) {
    // Calculate negative reaction time for pre-start movement
    unsigned long audioStartTime = audioSequenceStartTime;
    unsigned long expectedStartTime = audioStartTime + 500 + 250 + 750 + 250 + 750;  // When 1760Hz should start
    long negativeReactionTime = (long)millis() - (long)expectedStartTime;

    if (isLeft) {
      leftReactionTime = negativeReactionTime;  // Store negative reaction time
      leftReactionCalculated = true;
      leftFalseStart = true;
      falseStartOccurred = true;
      if (!friendlyFalseStartsEnabled) setLeftLEDs(CRGB::Red);
    } else {
      rightReactionTime = negativeReactionTime;  // Store negative reaction time
      rightReactionCalculated = true;
      rightFalseStart = true;
      falseStartOccurred = true;
      if (!friendlyFalseStartsEnabled) setRightLEDs(CRGB::Red);
    }
    resetTimeoutActive = true;
    lastEventTime = millis();
    sendWebSocketUpdate();
    return;
  }

  // IFSC reaction time calculation (after start signal)
  if (startSignalTime > 0 && isAnyTimerRunning()) {
    unsigned long releaseTime = millis();
    long reactionTime = (long)releaseTime - (long)startSignalTime;

    if (isLeft && !leftReactionCalculated) {
      leftReactionTime = reactionTime;
      leftReactionCalculated = true;

      if (reactionTime < FALSE_START_REACTION_TIME_CUTOFF) {  // False start < 100ms
        leftFalseStart = true;
        falseStartOccurred = true;
        if (!friendlyFalseStartsEnabled) setLeftLEDs(CRGB::Red);
      }
    } else if (!isLeft && !rightReactionCalculated) {
      rightReactionTime = reactionTime;
      rightReactionCalculated = true;

      if (reactionTime < FALSE_START_REACTION_TIME_CUTOFF) {  // False start < 100ms
        rightFalseStart = true;
        falseStartOccurred = true;
        if (!friendlyFalseStartsEnabled) setRightLEDs(CRGB::Red);
      }
    }

    // Handle both competitors false starting
    if (leftFalseStart && rightFalseStart && leftReactionCalculated && rightReactionCalculated) {
      if (leftReactionTime == rightReactionTime) {
        // Equal reaction times - both false started
      } else if (leftReactionTime < rightReactionTime) {
        rightFalseStart = false;
        setRightLEDs(CRGB::Black);
      } else {
        leftFalseStart = false;
        setLeftLEDs(CRGB::Black);
      }
    }

    sendWebSocketUpdate();
  }
}

void checkAutoStart() {
  if (singlePlayerMode && footHeldForAutoStart && isReadyState()) {
    if (millis() - footPressStartTime >= AUTO_START_DELAY) {
      footHeldForAutoStart = false;
      footPressStartTime = 0;
      setLaneActivity();
      resetCompetitionState();
      startAudioSequence();
    }
  }
}

void checkButtons() {
  if (millis() - lastButtonCheck < BUTTON_DEBOUNCE) return;

  bool startPressed = !digitalRead(START_BUTTON);
  bool stopLeftNow = !digitalRead(STOP_SENSOR_LEFT);
  bool stopRightNow = !digitalRead(STOP_SENSOR_RIGHT);
  bool footLeftNow = (digitalRead(FOOT_SENSOR_LEFT) == LOW);
  bool footRightNow = (digitalRead(FOOT_SENSOR_RIGHT) == LOW);

  // Handle foot sensor changes
  if (footLeftNow && !footLeftPressed) {
    footLeftPressed = true;
    handleFootSensorPress(true);
  } else if (!footLeftNow && footLeftPressed) {
    footLeftPressed = false;
    handleFootSensorRelease(true);
  }

  if (footRightNow && !footRightPressed) {
    footRightPressed = true;
    handleFootSensorPress(false);
  } else if (!footRightNow && footRightPressed) {
    footRightPressed = false;
    handleFootSensorRelease(false);
  }

  checkAutoStart();

  // Handle start button
  if (startPressed && !startButtonPressed) {
    startButtonPressed = true;
    handleStartButton();
  } else if (!startPressed) {
    startButtonPressed = false;
  }

  // Handle stop sensors
  if (stopLeftNow && !stopLeftPressed) {
    stopLeftPressed = true;
    handleStopSensor(true);
  } else if (!stopLeftNow) {
    stopLeftPressed = false;
  }

  if (stopRightNow && !stopRightPressed) {
    stopRightPressed = true;
    handleStopSensor(false);
  } else if (!stopRightNow) {
    stopRightPressed = false;
  }

  // Handle kids mode stop sensors (only if enabled)
  if (kidsModeSensorsEnabled) {
    bool stopLeftKidsNow = !digitalRead(STOP_SENSOR_LEFT_KIDS);
    bool stopRightKidsNow = !digitalRead(STOP_SENSOR_RIGHT_KIDS);

    if (stopLeftKidsNow && !stopLeftKidsPressed) {
      stopLeftKidsPressed = true;
      handleStopSensor(true);  // Use same handler as regular stop sensor
    } else if (!stopLeftKidsNow) {
      stopLeftKidsPressed = false;
    }

    if (stopRightKidsNow && !stopRightKidsPressed) {
      stopRightKidsPressed = true;
      handleStopSensor(false);  // Use same handler as regular stop sensor
    } else if (!stopRightKidsNow) {
      stopRightKidsPressed = false;
    }
  }
  lastButtonCheck = millis();
}

void handleStartButton() {
  if (canStartCompetition()) {
    setLaneActivity();
    resetCompetitionState();
    startAudioSequence();
  } else {
    handleApiReset();
    if (isPlayingAudio) {
      isPlayingAudio = false;
      currentAudioStep = 0;
      stopTone();
    }
    if (isPlayingFalseStart) {
      isPlayingFalseStart = false;
      currentAudioStep = 0;
      stopTone();
    }
  }
}

void determineWinner() {
  bool leftDNF = !leftFinished && !leftFalseStart && completionTimeLeft == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  bool rightDNF = !rightFinished && !rightFalseStart && completionTimeRight == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  
  if ((leftFinished || rightFinished || leftDNF || rightDNF) && !falseStartOccurred) {
    if (leftFinished && !rightFinished && !rightDNF) {
      setLeftLEDs(CRGB::Green);
      setRightLEDs(CRGB::Red);
    } else if (rightFinished && !leftFinished && !leftDNF) {
      setRightLEDs(CRGB::Green);
      setLeftLEDs(CRGB::Red);
    } else if (leftFinished && rightDNF) {
      setLeftLEDs(CRGB::Green);
      setRightLEDs(CRGB::Orange); // Keep orange for DNF
    } else if (rightFinished && leftDNF) {
      setRightLEDs(CRGB::Green);
      setLeftLEDs(CRGB::Orange); // Keep orange for DNF
    } else if (leftFinished && rightFinished) {
      if (completionTimeLeft < completionTimeRight) {
        setLeftLEDs(CRGB::Green);
        setRightLEDs(CRGB::Red);
      } else if (completionTimeRight < completionTimeLeft) {
        setRightLEDs(CRGB::Green);
        setLeftLEDs(CRGB::Red);
      } else {
        setLeftLEDs(CRGB::Green);
        setRightLEDs(CRGB::Green);
      }
    }
    // Both DNF case - no winner
    else if (leftDNF && rightDNF) {
      setLeftLEDs(CRGB::Orange);
      setRightLEDs(CRGB::Orange);
    }
  } else if (falseStartOccurred) {
    if (!leftFalseStart && rightFalseStart && leftFinished) {
      setLeftLEDs(CRGB::Green);
    } else if (!rightFalseStart && leftFalseStart && rightFinished) {
      setRightLEDs(CRGB::Green);
    } else if (!leftFalseStart && rightFalseStart && leftDNF) {
      setLeftLEDs(CRGB::Orange);
    } else if (!rightFalseStart && leftFalseStart && rightDNF) {
      setRightLEDs(CRGB::Orange);
    }
  }
}

void handleStopSensor(bool isLeft) {
  if (isAnyTimerRunning()) {
    unsigned long completionTime = millis() - timerStartTime;

    if (isLeft && !leftFinished) {
      completionTimeLeft = completionTime;
      leftFinished = true;
      isTimerRunningLeft = false;
    } else if (!isLeft && !rightFinished) {
      completionTimeRight = completionTime;
      rightFinished = true;
      isTimerRunningRight = false;
    }

    if (!isAnyTimerRunning()) {
      currentElapsedTime = millis() - timerStartTime;
      resetTimeoutActive = true;
      lastEventTime = millis();
    }

    determineWinner();

    if (leftFinished && rightFinished) {
      stopTimer();
    }

    sendWebSocketUpdate();
  }
}

// WebSocket Functions
bool hasStateChanged() {
  bool footSensorChanged = (footLeftPressed != lastFootLeftPressed || footRightPressed != lastFootRightPressed);
  bool timerStateChanged = (isTimerRunningLeft != lastTimerRunningLeft || isTimerRunningRight != lastTimerRunningRight);
  bool leftLaneStateChanged = (!singlePlayerMode || leftLaneActive) && (leftFalseStart != lastLeftFalseStart || leftFinished != lastLeftFinished || reactionTimeLeft != lastReactionTimeLeft || completionTimeLeft != lastCompletionTimeLeft);
  bool rightLaneStateChanged = (!singlePlayerMode || rightLaneActive) && (rightFalseStart != lastRightFalseStart || rightFinished != lastRightFinished || reactionTimeRight != lastReactionTimeRight || completionTimeRight != lastCompletionTimeRight);
  bool otherStateChanged = (isPlayingAudio != lastPlayingAudio || isPlayingFalseStart != lastPlayingFalseStart || falseStartOccurred != lastFalseStartOccurred || singlePlayerMode != lastSinglePlayerMode);
  bool kidsModeSensorsChanged = (kidsModeSensorsEnabled != lastKidsModeSensorsEnabled);

  return footSensorChanged || timerStateChanged || leftLaneStateChanged || rightLaneStateChanged || otherStateChanged || kidsModeSensorsChanged;
}

void updateLastState() {
  lastTimerRunningLeft = isTimerRunningLeft;
  lastTimerRunningRight = isTimerRunningRight;
  lastPlayingAudio = isPlayingAudio;
  lastPlayingFalseStart = isPlayingFalseStart;
  lastFalseStartOccurred = falseStartOccurred;
  lastLeftFalseStart = leftFalseStart;
  lastRightFalseStart = rightFalseStart;
  lastFootLeftPressed = footLeftPressed;
  lastFootRightPressed = footRightPressed;
  lastSinglePlayerMode = singlePlayerMode;
  lastLeftFinished = leftFinished;
  lastRightFinished = rightFinished;
  lastReactionTimeLeft = reactionTimeLeft;
  lastReactionTimeRight = reactionTimeRight;
  lastCompletionTimeLeft = completionTimeLeft;
  lastCompletionTimeRight = completionTimeRight;
  lastKidsModeSensorsEnabled = kidsModeSensorsEnabled;
}

void updateWebSocket() {
  // Skip processing if no clients connected
  if (!hasConnectedClients()) {
    // Still update last state to prevent flooding when clients reconnect
    updateLastState();
    return;
  }

  unsigned long currentTime = millis();
  bool shouldUpdate = false;

  if (singlePlayerMode) {
    shouldUpdate = (leftLaneActive && isTimerRunningLeft) || (rightLaneActive && isTimerRunningRight) || isPlayingAudio || isPlayingFalseStart || hasStateChanged();
  } else {
    shouldUpdate = isAnyTimerRunning() || isPlayingAudio || isPlayingFalseStart || hasStateChanged();
  }

  // Only send update if shouldUpdate is true AND at least WEBSOCKET_UPDATE_INTERVAL ms have passed
  if (shouldUpdate && (currentTime - lastWebSocketUpdate >= WEBSOCKET_UPDATE_INTERVAL)) {
    sendWebSocketUpdate();
    updateLastState();
    lastWebSocketUpdate = currentTime;
  }
}

DynamicJsonDocument buildStatusJson() {
  if (isAnyTimerRunning()) {
    currentElapsedTime = millis() - timerStartTime;
  }

  DynamicJsonDocument doc(3072);

  doc["is_timer_running"] = isAnyTimerRunning();
  doc["is_timer_running_left"] = isTimerRunningLeft;  // Added for DNF button logic
  doc["is_timer_running_right"] = isTimerRunningRight; // Added for DNF button logic
  doc["is_playing_audio"] = isPlayingAudio;
  doc["is_playing_false_start"] = isPlayingFalseStart;
  doc["false_start_occurred"] = falseStartOccurred;
  doc["left_false_start"] = leftFalseStart;
  doc["right_false_start"] = rightFalseStart;
  doc["single_player_mode"] = singlePlayerMode;

  doc["foot_left_pressed"] = footLeftPressed;
  doc["foot_right_pressed"] = footRightPressed;
  doc["both_feet_ready"] = footLeftPressed && footRightPressed;
  doc["ready_to_start"] = singlePlayerMode ? (footLeftPressed || footRightPressed) : (footLeftPressed && footRightPressed);

  doc["elapsed_time"] = currentElapsedTime;
  doc["formatted_time"] = formatTime(currentElapsedTime);

  doc["completion_time_left"] = completionTimeLeft;
  doc["formatted_completion_time_left"] = formatTime(completionTimeLeft);
  doc["left_finished"] = leftFinished;

  doc["completion_time_right"] = completionTimeRight;
  doc["formatted_completion_time_right"] = formatTime(completionTimeRight);
  doc["right_finished"] = rightFinished;

  doc["reaction_time_left"] = leftReactionCalculated ? leftReactionTime : 0;
  doc["formatted_reaction_time_left"] = formatSignedTime(leftReactionCalculated ? leftReactionTime : 0);
  doc["reaction_time_right"] = rightReactionCalculated ? rightReactionTime : 0;
  doc["formatted_reaction_time_right"] = formatSignedTime(rightReactionCalculated ? rightReactionTime : 0);
  doc["kids_mode_sensors_enabled"] = kidsModeSensorsEnabled;
  doc["competition_complete"] = (leftFinished || rightFinished) && !isAnyTimerRunning();

  doc["left_dnf"] = !leftFinished && !leftFalseStart && completionTimeLeft == 0 && (currentElapsedTime > 0 || resetTimeoutActive);
  doc["right_dnf"] = !rightFinished && !rightFalseStart && completionTimeRight == 0 && (currentElapsedTime > 0 || resetTimeoutActive);

  doc["friendly_false_starts"] = friendlyFalseStartsEnabled;

  doc["uptime"] = millis();

  doc["event_title"]      = eventTitle;
  doc["event_subtitle"]   = eventSubtitle;
  doc["current_heat_num"] = currentHeatNum;
  doc["current_round"]    = currentRound;
  doc["current_category"] = currentCategory;
  doc["wall_a_name"]      = wallAName;
  doc["wall_b_name"]      = wallBName;
  doc["sheets_connected"] = sheetsConnected;
  doc["heat_data_loaded"] = heatDataLoaded;

  doc["rest_warn_a"] = restWarnA;
  doc["rest_warn_b"] = restWarnB;
  doc["rest_secs_a"] = restSecsA;
  doc["rest_secs_b"] = restSecsB;

  JsonArray nxt = doc.createNestedArray("next_heats");
  for (int i = 0; i < upcomingHeatCount; i++) {
    JsonObject h = nxt.createNestedObject();
    h["heatNum"]  = upcomingHeats[i].num;
    h["wallA"]    = upcomingHeats[i].wallA;
    h["wallB"]    = upcomingHeats[i].wallB;
    h["category"] = upcomingHeats[i].category;
  }

  return doc;
}

bool hasConnectedClients() {
  uint8_t totalClients = webSocket.connectedClients();
  
  for (uint8_t i = 0; i < totalClients; i++) {
    if (webSocket.clientIsConnected(i)) {
      return true;
    }
  }
  return false;
}

String formatTime(unsigned long milliseconds) {
  unsigned long totalSeconds = milliseconds / 1000;
  unsigned long ms = milliseconds % 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = totalSeconds / 60;

  char timeStr[16];
  sprintf(timeStr, "%lu:%02lu.%03lu", minutes, seconds, ms);
  return String(timeStr);
}

String formatSignedTime(long milliseconds) {
  if (milliseconds == 0) {
    return "0:00.000";
  }

  bool isNegative = milliseconds < 0;
  unsigned long absMilliseconds = abs(milliseconds);

  unsigned long totalSeconds = absMilliseconds / 1000;
  unsigned long ms = absMilliseconds % 1000;
  unsigned long seconds = totalSeconds % 60;
  unsigned long minutes = totalSeconds / 60;

  char timeStr[17];
  if (isNegative) {
    sprintf(timeStr, "-%lu:%02lu.%03lu", minutes, seconds, ms);
  } else {
    sprintf(timeStr, "%lu:%02lu.%03lu", minutes, seconds, ms);
  }
  return String(timeStr);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("WebSocket client #%u disconnected (Total clients: %u)\n", num, webSocket.connectedClients());
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WebSocket client #%u connected from %d.%d.%d.%d (Total clients: %u)\n", num, ip[0], ip[1], ip[2], ip[3], webSocket.connectedClients());
        sendWebSocketUpdate();
      }
      break;

    case WStype_TEXT:
      {
        String command = String((char*)payload);
        Serial.printf("WebSocket received: %s\n", command.c_str());

        if (command == "start") {
          if (canStartCompetition()) {
            setLaneActivity();
            resetCompetitionState();
            startAudioSequence();
          }
        } else if (command == "stop") {
          if (isAnyTimerRunning()) {
            stopTimer();
          }
        } else if (command == "reset") {
          resetTimer();
          if (isPlayingAudio) {
            isPlayingAudio = false;
            currentAudioStep = 0;
            stopTone();
          }
          if (isPlayingFalseStart) {
            isPlayingFalseStart = false;
            currentAudioStep = 0;
            stopTone();
          }
        } else if (command == "toggle_mode") {
          singlePlayerMode = !singlePlayerMode;
        } else if (command == "toggle_kids_mode") {
          kidsModeSensorsEnabled = !kidsModeSensorsEnabled;
        } else if (command == "dnf_left") {
          handleDNF(true);  // true for left climber
        } else if (command == "dnf_right") {
          handleDNF(false); // false for right climber
        } else if (command == "toggle_friendly_fs") {
          friendlyFalseStartsEnabled = !friendlyFalseStartsEnabled;
        }
        sendWebSocketUpdate();
      }
      break;

    default:
      break;
  }
}

void handleDNF(bool isLeft) {
  // Only allow DNF if timer is running and climber hasn't already finished
  if (isAnyTimerRunning()) {
    if (isLeft && !leftFinished && !leftFalseStart) {
      // Stop left timer but don't set completion time
      isTimerRunningLeft = false;
      leftFinished = false;  // Keep as false to indicate DNF
      completionTimeLeft = 0; // Ensure it stays 0 for DNF
      
      // Set LEDs to indicate DNF
      setLeftLEDs(CRGB::Orange);
      
    } else if (!isLeft && !rightFinished && !rightFalseStart) {
      // Stop right timer but don't set completion time  
      isTimerRunningRight = false;
      rightFinished = false;  // Keep as false to indicate DNF
      completionTimeRight = 0; // Ensure it stays 0 for DNF
      
      // Set LEDs to indicate DNF
      setRightLEDs(CRGB::Orange);
    }
    
    // If both timers stopped, end the competition
    if (!isAnyTimerRunning()) {
      currentElapsedTime = millis() - timerStartTime;
      resetTimeoutActive = true;
      lastEventTime = millis();
      
      // Determine winner (the one who didn't DNF)
      determineWinner();
    }
    
    sendWebSocketUpdate();
  }
}

// Web Server Handlers
// ── Heat management ────────────────────────────────────────────────────────

void advanceHeat() {
  if (upcomingHeatCount > 0) {
    currentHeatNum  = upcomingHeats[0].num;
    currentRound    = upcomingHeats[0].round;
    wallAName       = upcomingHeats[0].wallA;
    wallBName       = upcomingHeats[0].wallB;
    currentCategory = upcomingHeats[0].category;
    restWarnA       = upcomingHeats[0].restWarnA;
    restWarnB       = upcomingHeats[0].restWarnB;
    restSecsA       = upcomingHeats[0].restSecsA;
    restSecsB       = upcomingHeats[0].restSecsB;
    for (int i = 0; i < upcomingHeatCount - 1; i++) upcomingHeats[i] = upcomingHeats[i + 1];
    upcomingHeatCount--;
    // Proactively fetch more heats when buffer is getting low
    if (upcomingHeatCount < 3) pendingHeatFetch = true;
  } else {
    currentHeatNum  = 0;
    currentRound    = "";
    wallAName       = "";
    wallBName       = "";
    currentCategory = "";
    restWarnA = false; restWarnB = false;
    restSecsA = 0;    restSecsB = 0;
  }
}

void loadHeatsFromJson(JsonArray arr) {
  upcomingHeatCount = 0;
  bool first = true;
  for (JsonObject h : arr) {
    int    n    = h["heatNum"] | 0;
    String rnd  = h["round"]   | "";
    String wA   = h["nameA"].isNull() ? (h["wallA"] | "") : (h["nameA"] | "");
    String wB   = h["nameB"].isNull() ? (h["wallB"] | "") : (h["nameB"] | "");
    String cat  = h["category"] | "";
    String bibA = h["bibA"]    | "";
    String bibB = h["bibB"]    | "";
    bool   rwA  = h["restWarnA"] | false;
    bool   rwB  = h["restWarnB"] | false;
    int    rsA  = h["restSecsA"] | 0;
    int    rsB  = h["restSecsB"] | 0;
    if (first) {
      currentHeatNum = n; currentRound = rnd; wallAName = wA; wallBName = wB; currentCategory = cat;
      restWarnA = rwA; restWarnB = rwB; restSecsA = rsA; restSecsB = rsB;
      first = false;
    } else if (upcomingHeatCount < 10) {
      upcomingHeats[upcomingHeatCount++] = { n, rnd, wA, wB, cat, bibA, bibB, rwA, rwB, rsA, rsB };
    }
  }
  if (first) { currentHeatNum = 0; currentRound = ""; wallAName = ""; wallBName = ""; currentCategory = "";
               restWarnA = false; restWarnB = false; restSecsA = 0; restSecsB = 0; }
  heatDataLoaded = true;
  sheetsConnected = true;
}

void fetchNextHeats() {
  Serial.printf("fetchNextHeats: WiFi=%d urlLen=%d\n", WiFi.status(), appsScriptUrl.length());
  if (WiFi.status() != WL_CONNECTED || appsScriptUrl.length() < 30) { Serial.println("fetchNextHeats: bailing (not connected or no URL)"); return; }
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = String(appsScriptUrl) + "?action=getNextHeats";
  if (!http.begin(client, url)) { http.end(); return; }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    DynamicJsonDocument doc(4096);
    String payload = http.getString();
    if (!deserializeJson(doc, payload) && doc.is<JsonArray>()) {
      loadHeatsFromJson(doc.as<JsonArray>());
      sendWebSocketUpdate();
      Serial.printf("fetchNextHeats: loaded heat %d (%s vs %s)\n",
        currentHeatNum, wallAName.c_str(), wallBName.c_str());
    }
  } else {
    Serial.printf("fetchNextHeats: HTTP %d\n", code);
  }
  http.end();
  lastHeatFetch = millis();
}

bool submitTimeToSheets(int heatNum, const String& tA, const String& tB, long rA = 0, long rB = 0) {
  if (WiFi.status() != WL_CONNECTED || appsScriptUrl.length() < 30) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, appsScriptUrl)) { http.end(); return false; }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"action\":\"submitTime\",\"heatNum\":" + String(heatNum) +
                ",\"timeA\":\"" + tA + "\",\"timeB\":\"" + tB + "\"" +
                ",\"reactionA\":" + String(rA) + ",\"reactionB\":" + String(rB) + "}";
  int code = http.POST(body);
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    // Try to parse nextHeats from response — Apps Script may return JSON directly
    // or redirect to the display page HTML. Either way, a 200 means the write succeeded.
    DynamicJsonDocument doc(2048);
    String payload = http.getString();
    if (!deserializeJson(doc, payload) && doc["success"].as<bool>()) {
      loadHeatsFromJson(doc["nextHeats"].as<JsonArray>());
      Serial.printf("submitTimeToSheets: ok (JSON), next heat %d\n", currentHeatNum);
    } else {
      // Got 200 but response wasn't our JSON (redirect landed on display page).
      // Write still happened — advance locally. A fresh fetch will follow shortly.
      Serial.println("submitTimeToSheets: ok (200, non-JSON response, advancing locally)");
      advanceHeat();
    }
    ok = true;
  } else {
    Serial.printf("submitTimeToSheets: HTTP %d\n", code);
  }
  http.end();
  lastHeatFetch = millis();
  return ok;
}

// ── New API handlers ───────────────────────────────────────────────────────

void handleApiConfirmHeat() {
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
  DynamicJsonDocument req(256);
  deserializeJson(req, server.arg("plain"));
  int    heatNum = req["heatNum"]   | currentHeatNum;
  String tA      = req["timeA"]     | String("");
  String tB      = req["timeB"]     | String("");
  long   rA      = req["reactionA"] | (long)0;
  long   rB      = req["reactionB"] | (long)0;
  bool attempted = submitTimeToSheets(heatNum, tA, tB, rA, rB);
  if (!attempted) advanceHeat(); // no WiFi / no URL — advance locally
  pendingHeatFetch = true; // re-fetch next heat after response is sent
  sendWebSocketUpdate();
  // Always return 200 — if the ESP32 attempted the write it almost certainly succeeded.
  // Genuine failures (no WiFi, no URL) are handled by advancing locally above.
  server.send(200, "application/json", attempted ? "{\"status\":\"ok\"}" : "{\"status\":\"local\",\"note\":\"no sheets connection\"}");
}

void handleApiSkipHeat() {
  advanceHeat();
  sendWebSocketUpdate();
  server.send(200, "application/json", "{\"status\":\"skipped\"}");
}

void handleApiFetchHeats() {
  fetchNextHeats();
  server.send(200, "application/json", "{\"status\":\"fetched\"}");
}

void handleApiSetTitle() {
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
  DynamicJsonDocument req(256);
  deserializeJson(req, server.arg("plain"));
  if (req.containsKey("title"))    eventTitle    = req["title"].as<String>();
  if (req.containsKey("subtitle")) eventSubtitle = req["subtitle"].as<String>();
  sendWebSocketUpdate();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiGetSettings() {
  DynamicJsonDocument doc(512);
  doc["linked"]     = sheetsLinked;
  doc["script_url"] = sheetsLinked ? appsScriptUrl : "";
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiSetSettings() {
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
  DynamicJsonDocument req(1024);
  if (deserializeJson(req, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
  }

  if (req.containsKey("action")) {
    String action = req["action"].as<String>();

    if (action == "link") {
      String url = req.containsKey("url") ? req["url"].as<String>() : "";
      if (url.length() < 30) {
        server.send(400, "application/json", "{\"error\":\"URL too short\"}"); return;
      }
      appsScriptUrl   = url;
      sheetsLinked    = true;
      pendingHeatFetch = true; // fetch after response is sent
      prefs.putString("scriptUrl", url);
      Serial.println("Apps Script URL saved to NVS: " + url);
      server.send(200, "application/json", "{\"status\":\"linked\"}");
      return;
    }

    if (action == "unlink") {
      appsScriptUrl  = "";
      sheetsLinked   = false;
      sheetsConnected = false;
      heatDataLoaded  = false;
      currentHeatNum  = 0;
      wallAName = ""; wallBName = ""; currentCategory = ""; currentRound = "";
      restWarnA = false; restWarnB = false; restSecsA = 0; restSecsB = 0;
      upcomingHeatCount = 0;
      prefs.remove("scriptUrl");
      Serial.println("Apps Script URL cleared from NVS");
      sendWebSocketUpdate();
      server.send(200, "application/json", "{\"status\":\"unlinked\"}");
      return;
    }
  }

  server.send(400, "application/json", "{\"error\":\"unknown action\"}");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Gravity Worx Speed Timer</title>
  <meta charset="UTF-8">
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    :root {
      --g: #43B75C;
      --gd: #2E8B57;
      --gg: rgba(67,183,92,0.25);
      --gold: #FFD700;
      --red: #dc2626;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Courier New', monospace;
      max-width: 960px;
      margin: 0 auto;
      padding: 16px;
      min-height: 100vh;
      background: linear-gradient(155deg, #081510 0%, #0f2318 45%, #071410 100%);
      color: white;
    }
    .container {
      background: rgba(255,255,255,0.04);
      padding: 20px;
      border-radius: 20px;
      border: 1px solid rgba(67,183,92,0.2);
      box-shadow: 0 8px 40px rgba(0,0,0,0.5);
    }
    h1 {
      text-align: center;
      font-size: clamp(1.4em, 4vw, 2.3em);
      font-weight: 900;
      letter-spacing: 5px;
      text-transform: uppercase;
      margin-bottom: 6px;
      color: var(--g);
      text-shadow: 0 0 20px var(--gg);
    }
    .title-rule {
      display: flex;
      align-items: center;
      gap: 10px;
      margin: 10px auto 20px;
      max-width: 500px;
    }
    .title-rule::before,.title-rule::after {
      content: '';
      flex: 1;
      height: 1px;
      background: linear-gradient(90deg, transparent, var(--g), transparent);
    }
    .title-rule-dot {
      width: 7px; height: 7px;
      background: var(--g);
      border-radius: 50%;
      box-shadow: 0 0 10px var(--gg);
    }
    h2 {
      margin-top: 0;
      font-size: clamp(0.85em, 2.5vw, 1.1em);
      letter-spacing: 3px;
      color: var(--g);
      text-shadow: 0 0 10px var(--gg);
      text-transform: uppercase;
    }
    .competition-layout {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
    }
    #status,.button-group,.instructions,.times-log { grid-column: 1 / -1; }
    .status {
      text-align: center;
      margin: 4px 0;
      font-size: clamp(0.65em, 1.8vw, 0.88em);
      letter-spacing: 2px;
      padding: 14px 18px;
      border-radius: 10px;
      background: rgba(0,60,0,0.6);
      border: 1px solid rgba(67,183,92,0.4);
      word-wrap: break-word;
      text-transform: uppercase;
    }
    .running { background: rgba(0,130,0,0.7); border-color: #00e84a; animation: pg 2s ease-in-out infinite alternate; }
    .stopped { background: rgba(0,60,0,0.6); }
    .playing { background: rgba(200,140,0,0.65); border-color: #ffc107; animation: py 1s ease-in-out infinite alternate; }
    .false-start { background: rgba(180,20,20,0.75); border-color: var(--red); }
    @keyframes pg { from{box-shadow:0 0 10px rgba(0,232,74,0.2)} to{box-shadow:0 0 30px rgba(0,232,74,0.5)} }
    @keyframes py { from{box-shadow:0 0 10px rgba(255,193,7,0.3)} to{box-shadow:0 0 25px rgba(255,193,7,0.6)} }
    @keyframes pr { from{box-shadow:0 4px 15px rgba(220,38,38,0.4)} to{box-shadow:0 8px 30px rgba(220,38,38,0.8)} }
    @keyframes wg { from{box-shadow:0 0 10px rgba(255,215,0,0.3)} to{box-shadow:0 0 30px rgba(255,215,0,0.65)} }
    .climber-panel {
      background: rgba(255,255,255,0.05);
      padding: 22px 18px;
      border-radius: 16px;
      text-align: center;
      border: 1px solid rgba(67,183,92,0.25);
      box-shadow: 0 8px 30px rgba(0,0,0,0.4);
      position: relative;
      overflow: hidden;
      transition: border-color 0.3s, box-shadow 0.3s;
    }
    .climber-panel::before {
      content: '';
      position: absolute;
      top: 0; left: 0;
      width: 36px; height: 36px;
      border-top: 2px solid rgba(67,183,92,0.5);
      border-left: 2px solid rgba(67,183,92,0.5);
      border-radius: 16px 0 0 0;
    }
    .climber-panel::after {
      content: '';
      position: absolute;
      bottom: 0; right: 0;
      width: 36px; height: 36px;
      border-bottom: 2px solid rgba(67,183,92,0.5);
      border-right: 2px solid rgba(67,183,92,0.5);
      border-radius: 0 0 16px 0;
    }
    .disqualified { opacity: 0.8; background: rgba(220,38,38,0.12) !important; border: 2px solid var(--red) !important; }
    .winner { background: rgba(255,215,0,0.08) !important; border: 2px solid var(--gold) !important; animation: wg 1s ease-in-out infinite alternate; }
    .timer-display {
      font-size: clamp(2em, 5.5vw, 3em);
      font-weight: 900;
      margin: 16px 0;
      letter-spacing: 2px;
      text-shadow: 0 0 20px rgba(120,255,120,0.5);
      background: rgba(0,0,0,0.5);
      padding: 18px 10px;
      border-radius: 12px;
      border: 1px solid rgba(67,183,92,0.35);
      box-shadow: inset 0 2px 14px rgba(0,0,0,0.5);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
      min-height: 1.2em;
      color: #ffffff;
    }
    .timer-display.false-start-text { font-size: clamp(1.1em,3vw,1.8em); color: #ff8a80; text-shadow: 0 0 20px rgba(255,138,128,0.5); }
    .winner-time { background: rgba(255,215,0,0.15) !important; border: 2px solid var(--gold) !important; color: var(--gold) !important; text-shadow: 0 0 20px rgba(255,215,0,0.6) !important; animation: wg 1s ease-in-out infinite alternate; }
    .foot-sensor { padding: 10px 14px; border-radius: 8px; font-weight: 700; font-size: 0.82em; letter-spacing: 1px; text-transform: uppercase; margin: 12px 0; border: 1px solid rgba(255,255,255,0.15); transition: all 0.3s; }
    .foot-pressed { background: rgba(34,197,94,0.2); border-color: rgba(34,197,94,0.5); color: #86efac; }
    .foot-released { background: rgba(239,68,68,0.15); border-color: rgba(239,68,68,0.4); color: #fca5a5; }
    .reaction-time,.completion-time { padding: 9px 12px; border-radius: 8px; margin: 8px 0; font-size: 0.83em; font-weight: 600; border: 1px solid rgba(255,255,255,0.12); transition: all 0.3s; }
    .reaction-time { background: rgba(251,191,36,0.15); color: #fde68a; }
    .reaction-time.negative { background: rgba(220,38,38,0.25); color: #ff8a80; font-weight: bold; }
    .completion-time { background: rgba(34,197,94,0.15); color: #86efac; }
    .dnf-button { background: rgba(220,38,38,0.2); border: 1px solid rgba(220,38,38,0.55); color: #ff8a80; font-family: inherit; font-weight: 700; font-size: 0.88em; letter-spacing: 1px; text-transform: uppercase; padding: 8px 16px; margin: 10px auto 0; border-radius: 8px; width: 150px; display: block; cursor: pointer; transition: all 0.25s; }
    .dnf-button:hover { background: rgba(220,38,38,0.35); transform: translateY(-1px); }
    .button-group { text-align: center; margin: 8px 0; display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; }
    button {
      font-family: inherit;
      font-weight: 700;
      font-size: 14px;
      letter-spacing: 1px;
      text-transform: uppercase;
      background: rgba(255,255,255,0.1);
      color: white;
      border: 1px solid rgba(255,255,255,0.3);
      padding: 12px 22px;
      margin: 0;
      border-radius: 12px;
      cursor: pointer;
      transition: all 0.25s;
      text-shadow: 0 1px 2px rgba(0,0,0,0.4);
      box-shadow: 0 4px 14px rgba(0,0,0,0.25);
      min-height: 44px;
    }
    button:hover { background: rgba(255,255,255,0.18); transform: translateY(-2px); border-color: rgba(255,255,255,0.5); }
    button:disabled { opacity: 0.35; cursor: not-allowed; transform: none; }
    #startBtn { background: linear-gradient(135deg, var(--g), var(--gd)); border-color: var(--g); box-shadow: 0 4px 20px rgba(67,183,92,0.4); font-size: 15px; padding: 13px 28px; }
    #startBtn:hover { box-shadow: 0 6px 28px rgba(67,183,92,0.55); }
    .mode-toggle { border-radius: 30px; max-width: 240px; }
    .mode-toggle.single-mode { background: linear-gradient(135deg,#ff6b6b,#ff4f4f); border-color: #ff4757; box-shadow: 0 4px 16px rgba(255,107,107,0.4); }
    .mode-toggle.competition-mode { background: linear-gradient(135deg, var(--g), var(--gd)); border-color: var(--g); box-shadow: 0 4px 16px rgba(67,183,92,0.4); }
    .mode-toggle:hover { transform: translateY(-2px) scale(1.02); }
    .kids-mode-toggle { border-radius: 30px; max-width: 240px; }
    .kids-mode-toggle.enabled { background: linear-gradient(135deg,#FF6B35,#FF8E53); border-color: #FF5722; box-shadow: 0 4px 16px rgba(255,107,53,0.4); }
    .kids-mode-toggle.disabled { background: rgba(70,70,70,0.6); border-color: rgba(120,120,120,0.4); }
    .kids-mode-toggle:hover { transform: translateY(-2px) scale(1.02); }
    .friendly-fs-toggle { border-radius: 30px; max-width: 240px; }
    .friendly-fs-toggle.enabled { background: linear-gradient(135deg,#9C27B0,#CE93D8); border-color: #7B1FA2; box-shadow: 0 4px 16px rgba(156,39,176,0.4); }
    .friendly-fs-toggle.disabled { background: rgba(70,70,70,0.6); border-color: rgba(120,120,120,0.4); }
    .friendly-fs-toggle:hover { transform: translateY(-2px) scale(1.02); }
    .help-button { background: rgba(59,130,246,0.25); border: 1px solid rgba(59,130,246,0.55); border-radius: 20px; font-size: 12px; max-width: 120px; min-height: 36px; }
    .help-button:hover { background: rgba(59,130,246,0.4); }
    .instructions { background: rgba(255,255,255,0.06); padding: 16px 18px; border-radius: 12px; margin: 4px 0; font-size: 0.88em; border: 1px solid rgba(67,183,92,0.2); line-height: 1.6; display: none; color: rgba(255,255,255,0.85); }
    .times-log { background: rgba(255,255,255,0.04); padding: 20px; border-radius: 16px; margin: 4px 0 0 0; border: 1px solid rgba(67,183,92,0.2); box-shadow: 0 8px 28px rgba(0,0,0,0.3); }
    .log-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; padding-bottom: 12px; border-bottom: 1px solid rgba(67,183,92,0.2); }
    .log-header h2 { margin-bottom: 0; }
    .log-controls button { font-size: 12px; padding: 8px 14px; margin: 0 0 0 8px; min-height: 36px; border-radius: 8px; }
    .log-entry { background: rgba(0,0,0,0.2); padding: 12px; margin: 8px 0; border-radius: 10px; border: 1px solid rgba(255,255,255,0.07); font-size: 0.9em; }
    .log-entry:hover { border-color: rgba(67,183,92,0.3); }
    .log-entry.winner-left,.log-entry.winner-right { border-left: 4px solid var(--gold); background: rgba(255,215,0,0.07); }
    .log-entry.single-player { border-left: 4px solid var(--g); }
    .log-entry-header { font-weight: bold; margin-bottom: 6px; display: flex; justify-content: space-between; align-items: center; }
    .winner-badge { color: var(--gold); font-weight: bold; }
    .log-times { margin-top: 8px; }
    .log-times.two-column { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .log-times.single-column { display: flex; justify-content: center; }
    .log-times.single-column .log-time { max-width: 300px; width: 100%; }
    .log-time { background: rgba(0,0,0,0.3); padding: 8px; border-radius: 6px; text-align: center; border: 1px solid rgba(255,255,255,0.08); font-size: 0.82em; }
    .log-time.false-start { background: rgba(220,38,38,0.2); color: #ff8a80; border-color: rgba(220,38,38,0.4); }
    .empty-log { text-align: center; color: rgba(255,255,255,0.4); font-style: italic; padding: 24px; }
    @media (max-width: 580px) {
      .competition-layout { grid-template-columns: 1fr; }
      .timer-display { font-size: 2.2em; padding: 16px; }
      .log-times.two-column { grid-template-columns: 1fr; gap: 8px; }
      .log-entry-header { flex-direction: column; align-items: flex-start; gap: 4px; }
      .winner-badge { align-self: flex-end; }
    }
    @media (max-width: 360px) {
      body { padding: 6px; }
      .container { padding: 10px; }
      .timer-display { font-size: 1.8em; padding: 12px; margin: 12px 0; }
      h1 { font-size: 1.3em; }
      h2 { font-size: 1em; }
      .instructions { font-size: 0.78em; padding: 10px; }
      button { font-size: 12px; padding: 10px 14px; }
      .climber-panel { padding: 14px; }
      .help-button { font-size: 11px; padding: 6px 10px; max-width: 100px; min-height: 32px; }
    }
    /* ── Editable titles ───────────────────────────────────────────── */
    .edit-title { cursor: text; border-radius: 6px; transition: outline .15s; }
    .edit-title:hover { outline: 1px dashed rgba(67,183,92,0.4); }
    .edit-title:focus { outline: 2px solid var(--g); background: rgba(67,183,92,0.07); }
    /* ── Heat banner ────────────────────────────────────────────────── */
    #heat-banner { background: rgba(0,0,0,0.4); border: 1px solid rgba(67,183,92,0.3); border-radius: 12px; padding: 10px 18px; margin-bottom: 14px; text-align: center; display: none; }
    .rest-warn { margin-top: 6px; font-size: .72em; font-weight: 700; letter-spacing: 1px; color: #ffbb33; background: rgba(255,170,0,0.12); border: 1px solid rgba(255,170,0,0.4); border-radius: 8px; padding: 4px 12px; display: inline-block; }
    .hb-label { font-size: .65em; letter-spacing: 3px; color: rgba(67,183,92,0.75); text-transform: uppercase; margin-bottom: 5px; }
    .hb-names { font-size: clamp(1.1em,3vw,1.6em); font-weight: 900; letter-spacing: 1px; }
    .hb-vs { color: rgba(67,183,92,0.5); margin: 0 10px; }
    /* ── Confirm bar ─────────────────────────────────────────────────── */
    #confirm-bar { position: fixed; bottom: 0; left: 0; right: 0; background: rgba(6,18,12,0.97); border-top: 2px solid rgba(67,183,92,0.5); padding: 12px 20px; display: none; align-items: center; gap: 10px; z-index: 999; flex-wrap: wrap; }
    #confirm-bar.show { display: flex; }
    .cb-info { flex: 1; min-width: 200px; }
    .cb-title { font-weight: 700; color: var(--g); letter-spacing: 1px; margin-bottom: 8px; font-size: .88em; }
    .cb-fields { display: flex; gap: 12px; flex-wrap: wrap; }
    .cb-field { display: flex; flex-direction: column; gap: 3px; }
    .cb-field label { font-size: .72em; color: rgba(255,255,255,0.5); letter-spacing: 1px; text-transform: uppercase; }
    .cb-field input { background: rgba(255,255,255,0.08); border: 1px solid rgba(67,183,92,0.4); border-radius: 6px; color: white; font-family: inherit; font-size: .9em; font-weight: 700; padding: 5px 10px; width: 110px; text-align: center; }
    .cb-field input:focus { outline: none; border-color: var(--g); background: rgba(67,183,92,0.1); }
    #cRecBtn { background: linear-gradient(135deg, var(--g), var(--gd)); border-color: var(--g); padding: 10px 20px; }
    /* ── Toast ─────────────────────────────────────────────────────── */
    #toast { position: fixed; bottom: 80px; left: 50%; transform: translateX(-50%); background: rgba(220,38,38,0.9); color: white; padding: 10px 20px; border-radius: 10px; font-size: .85em; letter-spacing: 1px; pointer-events: none; opacity: 0; transition: opacity .3s; z-index: 1000; }
    /* ── Settings panel ────────────────────────────────────────────── */
    #settings-panel { background: rgba(255,255,255,0.05); border: 1px solid rgba(67,183,92,0.25); border-radius: 14px; padding: 16px 18px; margin: 8px 0; display: none; }
    #settings-panel.show { display: block; }
    .sp-row { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; margin-bottom: 10px; }
    .sp-row:last-child { margin-bottom: 0; }
    .sp-label { font-size: .72em; letter-spacing: 2px; text-transform: uppercase; color: rgba(255,255,255,0.55); min-width: 80px; }
    .sp-status { font-size: .82em; padding: 5px 12px; border-radius: 20px; font-weight: 700; letter-spacing: 1px; }
    .sp-status.linked { background: rgba(67,183,92,0.2); color: var(--g); border: 1px solid rgba(67,183,92,0.4); }
    .sp-status.unlinked { background: rgba(180,30,30,0.2); color: #ff8a80; border: 1px solid rgba(220,38,38,0.3); }
    .sp-url-display { font-size: .72em; color: rgba(255,255,255,0.4); word-break: break-all; }
    #sp-url-input { flex: 1; min-width: 200px; background: rgba(255,255,255,0.07); border: 1px solid rgba(67,183,92,0.3); border-radius: 8px; color: white; font-family: inherit; font-size: .82em; padding: 7px 12px; }
    #sp-url-input:focus { outline: none; border-color: var(--g); background: rgba(67,183,92,0.08); }
    .sp-btn { font-family: inherit; font-weight: 700; font-size: 12px; letter-spacing: 1px; text-transform: uppercase; padding: 7px 16px; border-radius: 10px; cursor: pointer; border: 1px solid; min-height: 34px; transition: all .2s; }
    .sp-btn.link { background: linear-gradient(135deg, var(--g), var(--gd)); border-color: var(--g); color: white; }
    .sp-btn.unlink { background: rgba(220,38,38,0.2); border-color: rgba(220,38,38,0.5); color: #ff8a80; }
    .sp-btn:hover { opacity: 0.85; transform: translateY(-1px); }
  </style>
</head>
<body>
  <div class='container'>
    <h1 id='event-title' class='edit-title' contenteditable='true' spellcheck='false' onblur='saveTitle()' onkeydown='titleKey(event)'>Gravity Worx Speed Timer</h1>
    <div id='event-subtitle' class='edit-title' style='font-size:clamp(.7em,2vw,.9em);letter-spacing:3px;color:rgba(67,183,92,0.8);text-align:center;min-height:1.3em;margin:-2px 0 4px;padding:3px 6px;border-radius:6px' contenteditable='true' spellcheck='false' onblur='saveTitle()' onkeydown='titleKey(event)'></div>
    <div class='title-rule'><div class='title-rule-dot'></div></div>
    <div id='heat-banner'>
      <div class='hb-label' id='hb-label'>Heat —</div>
      <div class='hb-names'><span id='hb-a'>—</span><span class='hb-vs'>VS</span><span id='hb-b'>—</span></div>
      <div id='rest-warn-a' class='rest-warn' style='display:none'></div>
      <div id='rest-warn-b' class='rest-warn' style='display:none'></div>
    </div>

    <div class='competition-layout'>

      <div id='status' class='status stopped'>System Ready - Both Climbers Place Feet</div>

      <div class='climber-panel left-panel'>
        <h2>LEFT CLIMBER</h2>
        <div id='timer-left' class='timer-display'>0:00.000</div>
        <div id='foot-status-left' class='foot-sensor foot-released'>Foot Sensor: None</div>
        <div id='reaction-time-left' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-left' class='completion-time' style='display:none'>Time: 0:00.000</div>
        <button onclick='markDNF("left")' id='dnf-left-btn' class='dnf-button' style='display:none'>❌ Mark DNF</button>
      </div>

      <div class='climber-panel right-panel'>
        <h2>RIGHT CLIMBER</h2>
        <div id='timer-right' class='timer-display'>0:00.000</div>
        <div id='foot-status-right' class='foot-sensor foot-released'>Foot Sensor: None</div>
        <div id='reaction-time-right' class='reaction-time' style='display:none'>Reaction: 0:00.000</div>
        <div id='completion-time-right' class='completion-time' style='display:none'>Time: 0:00.000</div>
        <button onclick='markDNF("right")' id='dnf-right-btn' class='dnf-button' style='display:none'>❌ Mark DNF</button>
      </div>

      <div class='button-group'>
        <button onclick='startSequence()' id='startBtn'>🚀 Start Competition</button>
        <button onclick='resetTimer()' id='resetBtn'>🔄 Reset</button>
        <button onclick='toggleMode()' id='modeBtn' class='mode-toggle competition-mode'>🏆 Competition Mode</button>
        <button onclick='toggleKidsMode()' id='kidsModeBtn' class='kids-mode-toggle disabled'>🤸 Blue Sensors: OFF</button>
        <button onclick='toggleFriendlyFS()' id='friendlyFSBtn' class='friendly-fs-toggle disabled'>😊 Friendly False Starts: OFF</button>
        <button onclick='toggleHelp()' id='helpBtn' class='help-button'>❓ Help</button>
        <button onclick='syncHeats()' class='help-button'>📡 Sync Heats</button>
        <button onclick='toggleSettings()' id='settingsBtn' class='help-button'>⚙ Sheets</button>
        <a href='/projector' target='_blank' style='text-decoration:none'><button class='help-button'>📺 Projector</button></a>
      </div>

      <div id='settings-panel'>
        <div class='sp-row'>
          <span class='sp-label'>Status</span>
          <span id='sp-status' class='sp-status unlinked'>Unlinked</span>
          <span id='sp-url-display' class='sp-url-display'></span>
        </div>
        <div class='sp-row'>
          <input id='sp-url-input' type='text' placeholder='Paste Apps Script URL here…'>
          <button class='sp-btn link' onclick='linkSheets()'>Link</button>
          <button class='sp-btn unlink' onclick='unlinkSheets()'>Unlink</button>
        </div>
      </div>

      <div class='instructions' id='instructions'>
        <strong id='instructions-title'>Single Player Instructions:</strong><br>
        <span id='instructions-text'>
          1. Press and hold <b>ONE</b> foot sensor<br>
          2. Press Start to begin audio countdown or <b>stand on foot sensor for 3 seconds</b><br>
          3. Keep foot sensor pressed during entire audio countdown<br>
          4. Start climbing on the high pitched start tone<br>
          5. Hit your stop sensor when you reach the top<br>
          False starts as per IFSC are calculated at 0.1 seconds after the start tone sounds
        </span>
      </div>

      <div class='times-log'>
        <div class='log-header'>
          <h2>Times Log</h2>
          <div class='log-controls'>
            <button onclick='clearLog()'>Clear Log</button>
            <button onclick='exportLog()'>Export CSV</button>
          </div>
        </div>
        <div id='log-entries'>
          <div class='empty-log'>No times recorded yet. Complete a competition to see results here.</div>
        </div>
      </div>

    </div>
  </div>
  <div id='toast'></div>
  <div id='confirm-bar'>
    <div class='cb-info'>
      <div class='cb-title' id='cb-title'>Heat — complete</div>
      <div class='cb-fields'>
        <div class='cb-field'>
          <label id='cb-label-a'>Wall A</label>
          <input id='cb-time-a' type='text' placeholder='e.g. 8.23 or FS'>
        </div>
        <div class='cb-field'>
          <label id='cb-label-b'>Wall B</label>
          <input id='cb-time-b' type='text' placeholder='e.g. 9.10 or DNF'>
        </div>
      </div>
    </div>
    <button id='cRecBtn' onclick='confirmRecord()'>✓ Record to Sheet</button>
    <button onclick='confirmSkip()' style='padding:10px 18px'>⊘ Skip</button>
  </div>
  <script>
    let ws;
    let _ltStart=null,_ltRunning=false,_lDone=false,_rDone=false,_raf=null;
    function _fmtLocal(ms){var m=Math.floor(ms/60000),s=Math.floor(ms/1000)%60,r=ms%1000;return m+':'+('0'+s).slice(-2)+'.'+('00'+r).slice(-3);}
    function _tick(){if(!_ltRunning)return;var e=performance.now()-_ltStart;if(!_lDone)document.getElementById('timer-left').textContent=_fmtLocal(e);if(!_rDone)document.getElementById('timer-right').textContent=_fmtLocal(e);_raf=requestAnimationFrame(_tick);}
    function _startLocal(offset){_ltStart=performance.now()-(offset||0);_ltRunning=true;_lDone=false;_rDone=false;_tick();}
    function _stopLocal(){_ltRunning=false;if(_raf)cancelAnimationFrame(_raf);_raf=null;}
    let timesLog = [];
    let lastCompetitionComplete = false;
    let helpVisible = false;
    let titleInit = false, confirmShown = false, confirmPending = null;

    // ── Toast ──────────────────────────────────────────────────────────────
    var toastTimer;
    function showToast(msg) {
      var t = document.getElementById('toast');
      t.textContent = msg; t.style.opacity = '1';
      clearTimeout(toastTimer);
      toastTimer = setTimeout(function() { t.style.opacity = '0'; }, 4000);
    }

    // ── Title editing ──────────────────────────────────────────────────────
    function titleKey(e) { if (e.key === 'Enter') { e.preventDefault(); e.target.blur(); } }
    function saveTitle() {
      var t = document.getElementById('event-title').textContent.trim();
      var s = document.getElementById('event-subtitle').textContent.trim();
      fetch('/api/set_title', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ title: t, subtitle: s }) }).catch(function(){});
    }

    // ── Heat banner ────────────────────────────────────────────────────────
    function updateHeatBanner(d) {
      var b = document.getElementById('heat-banner');
      if (!d.heat_data_loaded || !d.current_heat_num) {
        b.style.display = 'none';
        document.querySelector('.left-panel h2').textContent = 'LEFT CLIMBER';
        document.querySelector('.right-panel h2').textContent = 'RIGHT CLIMBER';
        return;
      }
      b.style.display = 'block';
      var hbParts = ['Heat ' + d.current_heat_num];
      if (d.current_round)    hbParts.push(d.current_round);
      if (d.current_category) hbParts.push(d.current_category);
      if (d.sheets_connected) hbParts.push('Live');
      document.getElementById('hb-label').textContent = hbParts.join(' \u00b7 ');
      var a = d.wall_a_name || '\u2014', bv = d.wall_b_name || '\u2014';
      document.getElementById('hb-a').textContent = a;
      document.getElementById('hb-b').textContent = bv;
      document.querySelector('.left-panel h2').textContent  = (a  !== '\u2014') ? a  : 'LEFT CLIMBER';
      document.querySelector('.right-panel h2').textContent = (bv !== '\u2014') ? bv : 'RIGHT CLIMBER';
      // Rest time warnings
      var rwA = document.getElementById('rest-warn-a');
      var rwB = document.getElementById('rest-warn-b');
      function readyTime(secs) {
        var t = new Date(Date.now() + secs * 1000);
        return t.getHours().toString().padStart(2,'0') + ':' +
               t.getMinutes().toString().padStart(2,'0') + ':' +
               t.getSeconds().toString().padStart(2,'0');
      }
      if (d.rest_warn_a && d.rest_secs_a > 0) {
        rwA.textContent = '\u26a0 ' + a + ' can begin at ' + readyTime(d.rest_secs_a);
        rwA.style.display = 'block';
      } else { rwA.style.display = 'none'; }
      if (d.rest_warn_b && d.rest_secs_b > 0) {
        rwB.textContent = '\u26a0 ' + bv + ' can begin at ' + readyTime(d.rest_secs_b);
        rwB.style.display = 'block';
      } else { rwB.style.display = 'none'; }
    }

    // ── Confirm bar ────────────────────────────────────────────────────────
    function fmtSheet(d, s) {
      var f = d[s + '_false_start'], ok = d[s + '_finished'], ms = d['completion_time_' + s];
      if (f) return 'FS';
      if (!ok || !ms) return 'DNF';
      return (ms / 1000).toFixed(2);
    }
    function showConfirmBar(d) {
      if (confirmShown) return;
      confirmShown = true; confirmPending = d;
      var tA = fmtSheet(d, 'left'), tB = fmtSheet(d, 'right');
      var nA = d.wall_a_name || 'Wall A', nB = d.wall_b_name || 'Wall B';
      var cbParts = ['Heat ' + d.current_heat_num];
      if (d.current_round)    cbParts.push(d.current_round);
      if (d.current_category) cbParts.push(d.current_category);
      document.getElementById('cb-title').textContent = cbParts.join(' \u00b7 ') + ' \u2014 edit times if needed, then record';
      document.getElementById('cb-label-a').textContent = nA;
      document.getElementById('cb-label-b').textContent = nB;
      document.getElementById('cb-time-a').value = tA;
      document.getElementById('cb-time-b').value = tB;
      document.getElementById('confirm-bar').classList.add('show');
    }
    function hideConfirmBar() {
      confirmShown = false; confirmPending = null;
      document.getElementById('confirm-bar').classList.remove('show');
    }
    async function confirmRecord() {
      var btn = document.getElementById('cRecBtn');
      btn.disabled = true; btn.textContent = '\u23f3 Saving\u2026';
      var d = confirmPending;
      var tA = document.getElementById('cb-time-a').value.trim();
      var tB = document.getElementById('cb-time-b').value.trim();
      hideConfirmBar();
      fetch('/api/confirm_heat', { method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ heatNum: d.current_heat_num, timeA: tA, timeB: tB,
          reactionA: d.reaction_time_left || 0, reactionB: d.reaction_time_right || 0 }) })
        .then(function(r) {
          if (!r.ok) showToast('Sheet write may have failed \u2014 check connection');
        })
        .catch(function() { /* timed out — write likely still succeeded */ });
      btn.disabled = false; btn.textContent = '\u2713 Record to Sheet';
    }
    async function confirmSkip() {
      try { await fetch('/api/skip_heat', { method: 'POST' }); } catch(e) {}
      hideConfirmBar();
    }
    async function syncHeats() {
      try { await fetch('/api/fetch_heats', { method: 'POST' }); } catch(e) {}
    }

    // ── Settings panel ─────────────────────────────────────────────────────
    var settingsVisible = false;
    function toggleSettings() {
      settingsVisible = !settingsVisible;
      document.getElementById('settings-panel').classList.toggle('show', settingsVisible);
      document.getElementById('settingsBtn').textContent = settingsVisible ? '\u2716 Close' : '\u2699 Sheets';
      if (settingsVisible) loadSettings();
    }
    async function loadSettings() {
      try {
        var r = await fetch('/api/get_settings');
        var d = await r.json();
        var st = document.getElementById('sp-status');
        var disp = document.getElementById('sp-url-display');
        if (d.linked) {
          st.textContent = 'Linked'; st.className = 'sp-status linked';
          disp.textContent = d.script_url || '';
        } else {
          st.textContent = 'Unlinked'; st.className = 'sp-status unlinked';
          disp.textContent = '';
        }
      } catch(e) {}
    }
    async function linkSheets() {
      var url = document.getElementById('sp-url-input').value.trim();
      if (!url) { alert('Paste an Apps Script URL first.'); return; }
      try {
        var r = await fetch('/api/set_settings', { method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'link', url: url }) });
        var d = await r.json();
        if (d.status === 'linked') {
          document.getElementById('sp-url-input').value = '';
          loadSettings();
        } else {
          alert('Link failed: ' + (d.error || 'unknown error'));
        }
      } catch(e) { alert('Link failed: ' + e); }
    }
    async function unlinkSheets() {
      if (!confirm('Unlink the spreadsheet? Heat data will be cleared.')) return;
      try {
        await fetch('/api/set_settings', { method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'unlink' }) });
        loadSettings();
      } catch(e) {}
    }

    // Toggle help instructions
    function toggleHelp() {
      const instructionsDiv = document.getElementById('instructions');
      const helpBtn = document.getElementById('helpBtn');
      
      helpVisible = !helpVisible;
      
      if (helpVisible) {
        instructionsDiv.style.display = 'block';
        helpBtn.textContent = '❌ Hide Help';
      } else {
        instructionsDiv.style.display = 'none';
        helpBtn.textContent = '❓ Help';
      }
    }

    function toggleFriendlyFS() { ws.send('toggle_friendly_fs'); }

    function markDNF(side) {
      if (confirm(`Mark ${side} climber as Did Not Finish?`)) {
        ws.send(`dnf_${side}`);
      }
    }

    // Load saved log from localStorage
    function loadLog() {
      const saved = localStorage.getItem('gravityWorxTimesLog');
      if (saved) {
        timesLog = JSON.parse(saved);
        renderLog();
      }
    }

    // Save log to localStorage
    function saveLog() {
      localStorage.setItem('gravityWorxTimesLog', JSON.stringify(timesLog));
    }

    // Add entry to log
    function addLogEntry(data) {
      const timestamp = new Date();
      const entry = {
        timestamp: timestamp.toISOString(),
        date: timestamp.toLocaleDateString(),
        time: timestamp.toLocaleTimeString(),
        heatNum:   data.current_heat_num  || 0,
        round:     data.current_round     || '',
        category:  data.current_category  || '',
        nameA:     data.wall_a_name       || '',
        nameB:     data.wall_b_name       || '',
        singlePlayerMode: data.single_player_mode,
        leftTime: data.completion_time_left || 0,
        rightTime: data.completion_time_right || 0,
        leftReaction: data.reaction_time_left || 0,
        rightReaction: data.reaction_time_right || 0,
        leftFalseStart: data.left_false_start || false,
        rightFalseStart: data.right_false_start || false,
        leftFinished: data.left_finished || false,
        rightFinished: data.right_finished || false,
        formattedLeftTime: data.formatted_completion_time_left || '0:00.000',
        formattedRightTime: data.formatted_completion_time_right || '0:00.000',
        formattedLeftReaction: data.formatted_reaction_time_left || '0:00.000',
        formattedRightReaction: data.formatted_reaction_time_right || '0:00.000'
      };

      timesLog.unshift(entry); // Add to beginning
      if (timesLog.length > 100) { // Keep only last 100 entries
        timesLog = timesLog.slice(0, 100);
      }
      
      saveLog();
      renderLog();
    }

    // Render log entries
    function renderLog() {
      const logContainer = document.getElementById('log-entries');
      
      if (timesLog.length === 0) {
        logContainer.innerHTML = '<div class="empty-log">No times recorded yet. Complete a competition to see results here.</div>';
        return;
      }

      let html = '';
      timesLog.forEach((entry, index) => {
        let entryClass = 'log-entry';
        let winner = '';
        let showWinner = false;
        let leftIsWinner = false;
        let rightIsWinner = false;
        
        if (entry.singlePlayerMode) {
          entryClass += ' single-player';
          // For single player, only show winner styling if both lanes have valid times (indicating a race between two single players)
          const leftHasTime = entry.leftFinished && !entry.leftFalseStart && entry.leftTime > 0;
          const rightHasTime = entry.rightFinished && !entry.rightFalseStart && entry.rightTime > 0;
          
          if (leftHasTime && rightHasTime) {
            showWinner = true;
            if (entry.leftTime < entry.rightTime) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (entry.rightTime < entry.leftTime) {
              winner = 'Right Climber';
              rightIsWinner = true;
            } else {
              winner = 'Tie';
              leftIsWinner = true;
              rightIsWinner = true;
            }
          } else if (leftHasTime) {
            winner = 'Left Climber';
            leftIsWinner = true;
          } else if (rightHasTime) {
            winner = 'Right Climber';
            rightIsWinner = true;
          }
        } else {
          // Competition mode - keep existing logic but only show winner for valid completions
          const leftValid = entry.leftFinished && !entry.leftFalseStart;
          const rightValid = entry.rightFinished && !entry.rightFalseStart;
          
          if (leftValid || rightValid) {
            showWinner = true;
            if (entry.leftFalseStart && !entry.rightFalseStart && entry.rightFinished) {
              winner = 'Right Climber';
              rightIsWinner = true;
            } else if (entry.rightFalseStart && !entry.leftFalseStart && entry.leftFinished) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (leftValid && rightValid) {
              if (entry.leftTime < entry.rightTime) {
                winner = 'Left Climber';
                leftIsWinner = true;
              } else if (entry.rightTime < entry.leftTime) {
                winner = 'Right Climber';
                rightIsWinner = true;
              } else {
                winner = 'Tie';
                leftIsWinner = true;
                rightIsWinner = true;
              }
            } else if (leftValid) {
              winner = 'Left Climber';
              leftIsWinner = true;
            } else if (rightValid) {
              winner = 'Right Climber';
              rightIsWinner = true;
            }
          }
        }

        // Determine which times to show based on mode and actual data
        let leftTimeDisplay = '';
        let rightTimeDisplay = '';
        let showLeftTime = false;
        let showRightTime = false;

        var lLabel = entry.nameA || 'Left';
        var rLabel = entry.nameB || 'Right';

        if (entry.singlePlayerMode) {
          // In single player, only show times that actually have data
          if (entry.leftFinished || entry.leftFalseStart || entry.leftTime > 0) {
            showLeftTime = true;
            if (entry.leftFinished && entry.leftTime > 0) {
              leftTimeDisplay = `
                <div class="log-time ${leftIsWinner ? 'winner-time' : ''}">
                  <strong>${lLabel}:</strong><br>
                  Time: ${entry.formattedLeftTime}<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            } else if (entry.leftFalseStart) {
              leftTimeDisplay = `
                <div class="log-time">
                  <strong>${lLabel}:</strong><br>
                  Time: DQ (False Start)<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            } else {
              leftTimeDisplay = `
                <div class="log-time">
                  <strong>${lLabel}:</strong><br>
                  Time: DNF<br>
                  Reaction: ${entry.formattedLeftReaction}
                </div>
              `;
            }
          }

          if (entry.rightFinished || entry.rightFalseStart || entry.rightTime > 0) {
            showRightTime = true;
            if (entry.rightFinished && entry.rightTime > 0) {
              rightTimeDisplay = `
                <div class="log-time ${rightIsWinner ? 'winner-time' : ''}">
                  <strong>${rLabel}:</strong><br>
                  Time: ${entry.formattedRightTime}<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            } else if (entry.rightFalseStart) {
              rightTimeDisplay = `
                <div class="log-time">
                  <strong>${rLabel}:</strong><br>
                  Time: DQ (False Start)<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            } else {
              rightTimeDisplay = `
                <div class="log-time">
                  <strong>${rLabel}:</strong><br>
                  Time: DNF<br>
                  Reaction: ${entry.formattedRightReaction}
                </div>
              `;
            }
          }
        } else {
          // Competition mode - show both times
          showLeftTime = true;
          showRightTime = true;

          leftTimeDisplay = `
            <div class="log-time ${entry.leftFalseStart ? 'false-start' : ''} ${leftIsWinner ? 'winner-time' : ''}">
              <strong>${lLabel}:</strong><br>
              Time: ${entry.leftFinished ? entry.formattedLeftTime : 'DNF'}${entry.leftFalseStart ? ' (DQ)' : ''}<br>
              Reaction: ${entry.formattedLeftReaction}
            </div>
          `;

          rightTimeDisplay = `
            <div class="log-time ${entry.rightFalseStart ? 'false-start' : ''} ${rightIsWinner ? 'winner-time' : ''}">
              <strong>${rLabel}:</strong><br>
              Time: ${entry.rightFinished ? entry.formattedRightTime : 'DNF'}${entry.rightFalseStart ? ' (DQ)' : ''}<br>
              Reaction: ${entry.formattedRightReaction}
            </div>
          `;
        }

        // Determine grid layout based on what's being shown
        let gridClass = 'log-times';
        if (showLeftTime && showRightTime) {
          gridClass = 'log-times two-column';
        } else {
          gridClass = 'log-times single-column';
        }

        var heatLabel = '';
        if (entry.heatNum) {
          var parts = ['Heat ' + entry.heatNum];
          if (entry.round)    parts.push(entry.round);
          if (entry.category) parts.push(entry.category);
          heatLabel = parts.join(' \u00b7 ') + ' \u2014 ';
        }
        var namesLabel = '';
        if (entry.nameA || entry.nameB) {
          namesLabel = '<span style="color:rgba(255,255,255,0.5);font-weight:normal;font-size:.9em"> (' + (entry.nameA||'?') + ' vs ' + (entry.nameB||'?') + ')</span>';
        }

        html += `
          <div class="${entryClass}">
            <div class="log-entry-header">
              <span>${heatLabel}${entry.date} ${entry.time}${namesLabel}</span>
            </div>
            <div class="${gridClass}">
              ${showLeftTime ? leftTimeDisplay : ''}
              ${showRightTime ? rightTimeDisplay : ''}
            </div>
          </div>
        `;
      });
      
      logContainer.innerHTML = html;
    }

    // Clear log
    function clearLog() {
      if (confirm('Are you sure you want to clear all logged times? This cannot be undone.')) {
        timesLog = [];
        saveLog();
        renderLog();
      }
    }

    // Export log as CSV
    function exportLog() {
      if (timesLog.length === 0) {
        alert('No data to export.');
        return;
      }

      let csv = 'Date,Time,Heat,Round,Category,Name A,Name B,Mode,Left Time,Left Reaction,Left False Start,Right Time,Right Reaction,Right False Start,Winner\n';
      
      timesLog.forEach(entry => {
        const leftTime = entry.leftFinished ? entry.leftTime : '';
        const rightTime = entry.rightFinished ? entry.rightTime : '';
        let winner = '';
        
        if (entry.singlePlayerMode) {
          if (entry.leftFinished && !entry.leftFalseStart) winner = 'Left';
          else if (entry.rightFinished && !entry.rightFalseStart) winner = 'Right';
        } else {
          if (entry.leftFalseStart && !entry.rightFalseStart && entry.rightFinished) winner = 'Right';
          else if (entry.rightFalseStart && !entry.leftFalseStart && entry.leftFinished) winner = 'Left';
          else if (!entry.leftFalseStart && !entry.rightFalseStart && entry.leftFinished && entry.rightFinished) {
            winner = entry.leftTime < entry.rightTime ? 'Left' : entry.rightTime < entry.leftTime ? 'Right' : 'Tie';
          } else if (entry.leftFinished) winner = 'Left';
          else if (entry.rightFinished) winner = 'Right';
        }
        
        csv += `"${entry.date}","${entry.time}",${entry.heatNum||''},"${entry.round||''}","${entry.category||''}","${entry.nameA||''}","${entry.nameB||''}","${entry.singlePlayerMode ? 'Single' : 'Competition'}",${leftTime},${entry.leftReaction},${entry.leftFalseStart},${rightTime},${entry.rightReaction},${entry.rightFalseStart},"${winner}"\n`;
      });

      const blob = new Blob([csv], { type: 'text/csv' });
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `gravity-worx-times-${new Date().toISOString().split('T')[0]}.csv`;
      a.click();
      window.URL.revokeObjectURL(url);
    }

    function toggleKidsMode() { ws.send('toggle_kids_mode'); }
    
    function connectWebSocket() {
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
      ws = new WebSocket(protocol + '://' + location.hostname + ':81');
      ws.onopen = function() { console.log('WebSocket connected'); };
      ws.onmessage = function(event) {
        const data = JSON.parse(event.data);
        const footLeftDiv = document.getElementById('foot-status-left');
        const footRightDiv = document.getElementById('foot-status-right');

        if(data.is_timer_running || data.competition_complete) {
          footLeftDiv.style.display = 'none';
          footRightDiv.style.display = 'none';
        } else 
        {
          // Show foot sensor status when timer is not running
          footLeftDiv.style.display = 'block';
          
          if(data.foot_left_pressed) {
            footLeftDiv.textContent = 'Foot Sensor: Pressed';
            footLeftDiv.className = 'foot-sensor foot-pressed';
          } else {
            footLeftDiv.textContent = 'Foot Sensor: None';
            footLeftDiv.className = 'foot-sensor foot-released';
          }
          
          footRightDiv.style.display = 'block';
          
          if(data.foot_right_pressed) {
            footRightDiv.textContent = 'Foot Sensor: Pressed';
            footRightDiv.className = 'foot-sensor foot-pressed';
          } else {
            footRightDiv.textContent = 'Foot Sensor: None';
            footRightDiv.className = 'foot-sensor foot-released';
          }
        }

        // Check if competition just completed and auto-save
        if (data.competition_complete && !lastCompetitionComplete) {
          if (data.completion_time_left > 0 || data.completion_time_right > 0) {
            addLogEntry(data);
            if (data.heat_data_loaded && data.current_heat_num > 0) showConfirmBar(data);
          }
        }

        const dnfLeftBtn = document.getElementById('dnf-left-btn');
        const dnfRightBtn = document.getElementById('dnf-right-btn');

        if (data.is_timer_running && data.is_timer_running_left && !data.left_finished && !data.left_false_start) {
          dnfLeftBtn.style.display = 'block';
        } else {
          dnfLeftBtn.style.display = 'none';
        }

        if (data.is_timer_running && data.is_timer_running_right && !data.right_finished && !data.right_false_start) {
          dnfRightBtn.style.display = 'block';
        } else {
          dnfRightBtn.style.display = 'none';
        }

        lastCompetitionComplete = data.competition_complete;
        if (!titleInit) {
          document.getElementById('event-title').textContent = data.event_title || 'Gravity Worx Speed Timer';
          document.getElementById('event-subtitle').textContent = data.event_subtitle || '';
          titleInit = true;
        }
        updateHeatBanner(data);
        if (!data.competition_complete && confirmShown) hideConfirmBar();
        
        const leftTimer = document.getElementById('timer-left');
        const rightTimer = document.getElementById('timer-right');

        // Local timer management
        if(data.is_timer_running && !_ltRunning) _startLocal(data.elapsed_time);
        if(!data.is_timer_running && _ltRunning) _stopLocal();
        if(data.left_finished && data.completion_time_left > 0) _lDone = true;
        if(data.right_finished && data.completion_time_right > 0) _rDone = true;
        if(!data.is_timer_running && data.elapsed_time === 0 && !data.left_finished && !data.right_finished && !data.left_false_start && !data.right_false_start) { _lDone = false; _rDone = false; }

        if(!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0 || (data.is_timer_running && data.foot_left_pressed)) {
          if(data.left_finished && data.completion_time_left > 0) {
            leftTimer.textContent = data.formatted_completion_time_left;
            leftTimer.style.background = 'rgba(34,197,94,0.2)';
          } else if(data.left_false_start) {
            leftTimer.textContent = data.friendly_false_starts
              ? (data.completion_time_left > 0 ? data.formatted_completion_time_left : data.formatted_time) + ' ⚑'
              : 'FALSE START';
            leftTimer.style.background = 'rgba(220,38,38,0.3)';
            leftTimer.classList.add('false-start-text');
          } else if(_ltRunning) {
            leftTimer.style.background = 'rgba(255,255,255,0.1)';
            leftTimer.classList.remove('false-start-text');
            // RAF handles textContent
          } else {
            leftTimer.textContent = data.formatted_time;
            leftTimer.style.background = 'rgba(255,255,255,0.1)';
            leftTimer.classList.remove('false-start-text');
          }
        } else {
          leftTimer.textContent = '0:00.000';
          leftTimer.style.background = 'rgba(0,0,0,0.5)';
          leftTimer.classList.remove('false-start-text');
        }

        if(!data.single_player_mode || data.right_finished || data.right_false_start || data.reaction_time_right != 0 || (data.is_timer_running && data.foot_right_pressed)) {
          if(data.right_finished && data.completion_time_right > 0) {
            rightTimer.textContent = data.formatted_completion_time_right;
            rightTimer.style.background = 'rgba(34,197,94,0.2)';
          } else if(data.right_false_start) {
            rightTimer.textContent = data.friendly_false_starts
              ? (data.completion_time_right > 0 ? data.formatted_completion_time_right : data.formatted_time) + ' ⚑'
              : 'FALSE START';
            rightTimer.style.background = 'rgba(220,38,38,0.3)';
            rightTimer.classList.add('false-start-text');
          } else if(_ltRunning) {
            rightTimer.style.background = 'rgba(255,255,255,0.1)';
            rightTimer.classList.remove('false-start-text');
            // RAF handles textContent
          } else {
            rightTimer.textContent = data.formatted_time;
            rightTimer.style.background = 'rgba(255,255,255,0.1)';
            rightTimer.classList.remove('false-start-text');
          }
        } else {
          rightTimer.textContent = '0:00.000';
          rightTimer.style.background = 'rgba(0,0,0,0.5)';
          rightTimer.classList.remove('false-start-text');
        }
                
        const modeBtn = document.getElementById('modeBtn');
        if(data.single_player_mode) {
          modeBtn.textContent = '👤 Single Player Mode';
          modeBtn.className = 'mode-toggle single-mode';
        } else {
          modeBtn.textContent = '🏆 Competition Mode';
          modeBtn.className = 'mode-toggle competition-mode';
        }

        const kidsModeBtn = document.getElementById('kidsModeBtn');
        if(data.kids_mode_sensors_enabled) {
          kidsModeBtn.textContent = '🤸 Blue Sensors: ON';
          kidsModeBtn.className = 'kids-mode-toggle enabled';
        } else {
          kidsModeBtn.textContent = '🤸 Blue Sensors: OFF';
          kidsModeBtn.className = 'kids-mode-toggle disabled';
        }
        
        const friendlyFSBtn = document.getElementById('friendlyFSBtn');
        if(data.friendly_false_starts) {
          friendlyFSBtn.textContent = 'Friendly False Starts: ON';
          friendlyFSBtn.className = 'friendly-fs-toggle enabled';
        } else {
          friendlyFSBtn.textContent = 'Friendly False Starts: OFF';
          friendlyFSBtn.className = 'friendly-fs-toggle disabled';
        }

        const instructionsTitle = document.getElementById('instructions-title');
        const instructionsText = document.getElementById('instructions-text');
        if(data.single_player_mode) {
          instructionsTitle.textContent = 'Single Player Instructions:';
          instructionsText.innerHTML = `
            1. Press and hold <b>ONE</b> foot sensor<br>
            2. Press Start to begin audio countdown or <b>stand on foot sensor for 3 seconds</b><br>
            3. Keep foot sensor pressed during entire audio countdown<br>
            4. Start climbing on the high picthed start tone<br>
            5. Hit your stop sensor when you reach the top<br>
            False starts as per IFSC are calucated at 0.1 seconds after the start tone sounds
          `;
        } else {
          instructionsTitle.textContent = 'Competition Instructions:';
          instructionsText.innerHTML = `
            1. Both climbers press and hold foot sensors<br>
            2. Press Start to begin audio countdown<br>
            3. Keep foot sensors pressed during entire audio countdown<br>
            4. Start climbing on the high picthed start tone<br>
            5. Hit your stop sensor when you reach the top<br>
            False starts as per IFSC are calucated at 0.1 seconds after the start tone sounds<br>
            <strong>Both climbers can still finish even if one false starts</strong><br>
          `;
        }
        
        const reactionLeftDiv = document.getElementById('reaction-time-left');
        const reactionRightDiv = document.getElementById('reaction-time-right');
        
        if((!data.single_player_mode || data.left_finished || data.left_false_start || data.reaction_time_left != 0) && 
           (data.reaction_time_left != 0 || (data.elapsed_time > 0 && !data.is_playing_audio))) {
          if(data.reaction_time_left != 0) {
            reactionLeftDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_left;
            if(data.reaction_time_left < 0) {
              reactionLeftDiv.className = 'reaction-time negative';
            } else {
              reactionLeftDiv.className = 'reaction-time';
            }
          } else {
            reactionLeftDiv.textContent = 'Reaction: Waiting...';
            reactionLeftDiv.className = 'reaction-time';
          }
          reactionLeftDiv.style.display = 'block';
        } else {
          reactionLeftDiv.style.display = 'none';
        }
        
        if((!data.single_player_mode || data.right_finished || data.right_false_start || data.reaction_time_right != 0) && 
           (data.reaction_time_right != 0 || (data.elapsed_time > 0 && !data.is_playing_audio))) {
          if(data.reaction_time_right != 0) {
            reactionRightDiv.textContent = 'Reaction: ' + data.formatted_reaction_time_right;
            if(data.reaction_time_right < 0) {
              reactionRightDiv.className = 'reaction-time negative';
            } else {
              reactionRightDiv.className = 'reaction-time';
            }
          } else {
            reactionRightDiv.textContent = 'Reaction: Waiting...';
            reactionRightDiv.className = 'reaction-time';
          }
          reactionRightDiv.style.display = 'block';
        } else {
          reactionRightDiv.style.display = 'none';
        }
        
        const completionLeftDiv = document.getElementById('completion-time-left');
        const completionRightDiv = document.getElementById('completion-time-right');
        const leftPanel = document.querySelector('.left-panel');
        const rightPanel = document.querySelector('.right-panel');
        
        leftPanel.classList.remove('winner', 'disqualified');
        rightPanel.classList.remove('winner', 'disqualified');
        
        if(data.left_false_start) {
          leftPanel.classList.add('disqualified');
        }
        if(data.right_false_start) {
          rightPanel.classList.add('disqualified');
        }
        
        if(data.completion_time_left > 0) {
          if(data.left_false_start) {
            completionLeftDiv.textContent = 'FINISH: ' + data.formatted_completion_time_left + ' (DQ)';
            completionLeftDiv.style.background = 'rgba(220,38,38,0.3)';
          } else {
            completionLeftDiv.textContent = 'FINISH: ' + data.formatted_completion_time_left;
            completionLeftDiv.style.background = 'rgba(34,197,94,0.2)';
          }
          completionLeftDiv.style.display = 'block';
        } else {
          completionLeftDiv.style.display = 'none';
        }
        
        if(data.completion_time_right > 0) {
          if(data.right_false_start) {
            completionRightDiv.textContent = 'FINISH: ' + data.formatted_completion_time_right + ' (DQ)';
            completionRightDiv.style.background = 'rgba(220,38,38,0.3)';
          } else {
            completionRightDiv.textContent = 'FINISH: ' + data.formatted_completion_time_right;
            completionRightDiv.style.background = 'rgba(34,197,94,0.2)';
          }
          completionRightDiv.style.display = 'block';
        } else {
          completionRightDiv.style.display = 'none';
        }
        
        if(!data.left_false_start && !data.right_false_start) {
          if(data.completion_time_left > 0 && data.completion_time_right > 0) {
            if(data.completion_time_left < data.completion_time_right) {
              leftPanel.classList.add('winner');
            } else if(data.completion_time_right < data.completion_time_left) {
              rightPanel.classList.add('winner');
            }
          } else if(data.completion_time_left > 0) {
            leftPanel.classList.add('winner');
          } else if(data.completion_time_right > 0) {
            rightPanel.classList.add('winner');
          }
        } else if(data.left_false_start && !data.right_false_start) {
          if(data.completion_time_right > 0) {
            rightPanel.classList.add('winner');
          }
        } else if(!data.left_false_start && data.right_false_start) {
          if(data.completion_time_left > 0) {
            leftPanel.classList.add('winner');
          }
        }
        
        const statusDiv = document.getElementById('status');
        const startBtn = document.getElementById('startBtn');

        if(data.is_playing_false_start) {
          let falseStartMsg = 'FALSE START!';
          if(data.left_false_start && data.right_false_start) {
            falseStartMsg = 'FALSE START - BOTH COMPETITORS!';
          } else if(data.left_false_start) {
            falseStartMsg = 'FALSE START - LEFT COMPETITOR!';
          } else if(data.right_false_start) {
            falseStartMsg = 'FALSE START - RIGHT COMPETITOR!';
          }
          statusDiv.textContent = falseStartMsg;
          statusDiv.className = 'status false-start';
          startBtn.disabled = true;
        } else if(data.is_playing_audio) {
          statusDiv.textContent = 'Audio Sequence Playing - Keep Feet Pressed!';
          statusDiv.className = 'status playing';
          startBtn.disabled = true; 
        } else if(data.is_timer_running) {
          let runningMsg = 'CLIMB! Timer Running';
          if(data.false_start_occurred) {
            if(data.left_false_start && data.right_false_start) {
              runningMsg = 'CLIMB! (Both False Started)';
            } else if(data.left_false_start) {
              runningMsg = 'CLIMB! (Left False Started)';
            } else if(data.right_false_start) {
              runningMsg = 'CLIMB! (Right False Started)';
            }
          }
          statusDiv.textContent = runningMsg;
          statusDiv.className = 'status running';
          startBtn.disabled = true;
        } else {
          if(data.false_start_occurred) {
            let falseStartMsg = 'False Start Occurred';
            if(data.left_false_start && data.right_false_start) {
              falseStartMsg = 'Both Competitors False Started';
            } else if(data.left_false_start) {
              falseStartMsg = 'Left Competitor False Started';
            } else if(data.right_false_start) {
              falseStartMsg = 'Right Competitor False Started';
            }
            statusDiv.textContent = falseStartMsg + ' - Reset to Try Again';
            statusDiv.className = 'status false-start';
          } else if(data.completion_time_left > 0 || data.completion_time_right > 0) {
            statusDiv.textContent = 'Competition Complete!';
            statusDiv.className = 'status stopped';
          } else {
            statusDiv.textContent = data.ready_to_start ? 'Ready to Start!' : (data.single_player_mode ? 'Waiting for One Foot Sensor' : 'Waiting for Both Foot Sensors');
            statusDiv.className = 'status stopped';
          }
          startBtn.disabled = !data.ready_to_start;
        }
      };
      ws.onclose = function() { console.log('WebSocket disconnected'); setTimeout(connectWebSocket, 3000); };
      ws.onerror = function(error) { console.log('WebSocket error:', error); };
    }

    function startSequence() { ws.send('start'); }
    function resetTimer() { ws.send('reset'); }
    function toggleMode() { ws.send('toggle_mode'); }

    // Load log on page load
    loadLog();
    connectWebSocket();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleProjector() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Speed Timer — Projector</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    :root{--g:#43B75C;--gd:#2E8B57;--gold:#FFD700;--red:#dc2626}
    html,body{height:100%;overflow:hidden}
    body{font-family:'Courier New',monospace;background:#060d0a;color:white;position:relative;width:100vw;height:100vh;overflow:hidden}
    /* ── Layout blocks ── */
    .p-block{position:absolute;box-sizing:border-box;z-index:1}
    #blk-hdr,#blk-heat,#blk-wall-a,#blk-wall-b,#blk-name-a,#blk-name-b,#blk-vs,#blk-next{z-index:2}
    /* ── Edit mode ── */
    body.edit-mode .p-block{outline:2px dashed rgba(255,255,255,0.4);cursor:move}
    body.edit-mode .p-block:hover{outline-color:#fff}
    .lbl-edit[contenteditable=true]{outline:1px dashed rgba(255,255,255,0.5);border-radius:3px;padding:0 4px;cursor:text;min-width:20px;display:inline-block}
    .lbl-edit[contenteditable=true]:focus{outline:2px solid #fff;background:rgba(255,255,255,0.08)}
    .resize-handle{display:none;position:absolute;bottom:0;right:0;width:18px;height:18px;cursor:se-resize;background:linear-gradient(135deg,transparent 50%,rgba(255,255,255,0.6) 50%)}
    body.edit-mode .resize-handle{display:block}
    .font-handle{display:none;position:absolute;top:2px;left:2px;background:rgba(0,0,0,0.7);color:white;border:1px solid rgba(255,255,255,0.4);border-radius:4px;font-size:11px;padding:2px 6px;cursor:pointer;white-space:nowrap;user-select:none;z-index:10}
    body.edit-mode .font-handle{display:block}
    /* ── Edit toggle button ── */
    #edit-btn{position:fixed;top:10px;right:10px;z-index:999;background:rgba(0,0,0,0.7);color:rgba(255,255,255,0.5);border:1px solid rgba(255,255,255,0.25);border-radius:8px;padding:6px 12px;font-family:'Courier New',monospace;font-size:12px;cursor:pointer;letter-spacing:1px}
    #edit-btn.active{color:#fff;border-color:#fff;background:rgba(255,255,255,0.15)}
    /* ── Reset button ── */
    #reset-btn{position:fixed;top:10px;right:120px;z-index:999;display:none;background:rgba(180,0,0,0.7);color:#fff;border:1px solid rgba(255,100,100,0.5);border-radius:8px;padding:6px 12px;font-family:'Courier New',monospace;font-size:12px;cursor:pointer;letter-spacing:1px}
    body.edit-mode #reset-btn{display:block}
    /* Header block */
    .p-hdr{text-align:center;padding:10px 16px;border-bottom:2px solid rgba(255,255,255,0.15);width:100%}
    #p-title{font-size:2.2em;font-weight:900;letter-spacing:6px;text-transform:uppercase;color:#ffffff;text-shadow:0 2px 12px rgba(0,0,0,0.8);white-space:nowrap}
    #p-subtitle{font-size:1em;letter-spacing:3px;color:#ffffff;margin-top:4px;min-height:1em;white-space:nowrap}
    /* Heat label */
    #p-heat{text-align:center;font-size:1.1em;letter-spacing:4px;color:#ffffff;text-transform:uppercase;font-weight:700;padding:6px 0;width:100%}
    /* Names block */
    .p-names{display:flex;align-items:center;justify-content:center;gap:24px;width:100%}
    .p-climber{flex:1;min-width:0}
    .p-climber-l{text-align:right}
    .p-climber-r{text-align:left}
    .p-wall-lbl{font-size:0.6em;letter-spacing:2px;color:#ffffff;text-transform:uppercase;margin-bottom:4px}
    .p-name{font-size:3em;font-weight:900;letter-spacing:2px;color:#ffffff;text-shadow:0 2px 16px rgba(0,0,0,0.9);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .p-vs{font-size:1.4em;font-weight:900;color:#ffffff;letter-spacing:3px;flex-shrink:0}
    /* Timer panels */
    .p-panel{display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.5);border:3px solid rgba(255,255,255,0.2);border-radius:22px;transition:border-color .3s,background .3s,box-shadow .3s;width:100%;height:100%}
    .p-panel.winner{border-color:var(--gold);background:rgba(255,215,0,0.15);box-shadow:0 0 60px rgba(255,215,0,0.35)}
    .p-panel.fs{border-color:var(--red);background:rgba(220,38,38,0.2)}
    .p-time{font-size:5em;font-weight:900;letter-spacing:2px;color:#ffffff;text-shadow:0 0 40px rgba(100,255,100,0.6),0 2px 8px rgba(0,0,0,0.9)}
    .p-react{font-size:2em;font-weight:700;letter-spacing:3px;color:#ffffff;text-shadow:0 0 20px rgba(100,255,100,0.4);text-align:center}
    .p-time.fs-txt{font-size:2.5em;color:#ff6060;text-shadow:0 0 30px rgba(255,80,80,0.7),0 2px 8px rgba(0,0,0,0.9);text-align:center;padding:0 12px}
    .p-time.win{color:var(--gold);text-shadow:0 0 40px rgba(255,215,0,0.7),0 2px 8px rgba(0,0,0,0.9)}
    /* Next up */
    .p-next{display:none;width:100%}
    .p-next-lbl{font-size:0.65em;letter-spacing:3px;color:#ffffff;text-transform:uppercase;margin-bottom:5px}
    .p-next-item{font-size:0.95em;color:#ffffff;font-weight:600}
    .p-next-h{color:#ffffff;margin-right:8px;font-weight:400}
    /* Waiting */
    #p-wait{display:none}
  </style>
</head>
<body>
  <button id="edit-btn" onclick="toggleEdit()">✦ EDIT LAYOUT</button>
  <button id="reset-btn" onclick="resetLayout()">↺ RESET</button>

  <div id="blk-hdr" class="p-block p-hdr">
    <div id="p-title">Gravity Worx Speed Timer</div>
    <div id="p-subtitle"></div>
    <div class="font-handle" onclick="scaleFonts('blk-hdr',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-hdr',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-heat" class="p-block">
    <div id="p-heat"></div>
    <div class="font-handle" onclick="scaleFonts('blk-heat',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-heat',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-wall-a" class="p-block" style="text-align:center">
    <div id="lbl-wall-a" class="p-wall-lbl lbl-edit">Wall A</div>
    <div class="font-handle" onclick="scaleFonts('blk-wall-a',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-wall-a',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-name-a" class="p-block" style="text-align:center">
    <div id="p-name-a" class="p-name" style="white-space:normal;overflow:visible;text-overflow:clip">—</div>
    <div class="font-handle" onclick="scaleFonts('blk-name-a',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-name-a',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-vs" class="p-block" style="text-align:center;display:flex;align-items:center;justify-content:center">
    <div id="lbl-vs" class="p-vs lbl-edit">VS</div>
    <div class="font-handle" onclick="scaleFonts('blk-vs',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-vs',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-wall-b" class="p-block" style="text-align:center">
    <div id="lbl-wall-b" class="p-wall-lbl lbl-edit">Wall B</div>
    <div class="font-handle" onclick="scaleFonts('blk-wall-b',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-wall-b',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-name-b" class="p-block" style="text-align:center">
    <div id="p-name-b" class="p-name" style="white-space:normal;overflow:visible;text-overflow:clip">—</div>
    <div class="font-handle" onclick="scaleFonts('blk-name-b',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-name-b',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-timer-l" class="p-block">
    <div id="p-panel-l" class="p-panel">
      <div id="p-time-l" class="p-time">0:00.00</div>
    </div>
    <div class="font-handle" onclick="scaleFonts('blk-timer-l',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-timer-l',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-timer-r" class="p-block">
    <div id="p-panel-r" class="p-panel">
      <div id="p-time-r" class="p-time">0:00.00</div>
    </div>
    <div class="font-handle" onclick="scaleFonts('blk-timer-r',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-timer-r',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-react-l" class="p-block" style="display:none">
    <div id="p-react-l" class="p-react"></div>
    <div class="font-handle" onclick="scaleFonts('blk-react-l',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-react-l',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-react-r" class="p-block" style="display:none">
    <div id="p-react-r" class="p-react"></div>
    <div class="font-handle" onclick="scaleFonts('blk-react-r',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-react-r',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="blk-next" class="p-block p-next">
    <div id="lbl-next" class="p-next-lbl lbl-edit">Next Up</div>
    <div id="p-next-items"></div>
    <div class="font-handle" onclick="scaleFonts('blk-next',1.1)">A+ <span onclick="event.stopPropagation();scaleFonts('blk-next',0.9)">A-</span></div>
    <div class="resize-handle"></div>
  </div>

  <div id="p-wait"></div>
<script>
  var ws;
  var _ltStart=null,_ltRunning=false,_lDone=false,_rDone=false,_raf=null;
  function fmt(ms){var t=ms/1000,m=Math.floor(t/60),s=(t%60).toFixed(2).padStart(5,'0');return m+':'+s;}
  function _tick(){
    if(!_ltRunning)return;
    var e=performance.now()-_ltStart;
    if(!_lDone)document.getElementById('p-time-l').textContent=fmt(e);
    if(!_rDone)document.getElementById('p-time-r').textContent=fmt(e);
    _raf=requestAnimationFrame(_tick);
  }
  function _startLocal(offset){_ltStart=performance.now()-(offset||0);_ltRunning=true;_lDone=false;_rDone=false;_tick();}
  function _stopLocal(){_ltRunning=false;if(_raf)cancelAnimationFrame(_raf);_raf=null;}
  function fmtReact(ms){return (ms>=0?'+':'')+ms+'ms';}
  function conn(){
    var pr=location.protocol==='https:'?'wss':'ws';
    ws=new WebSocket(pr+'://'+location.hostname+':81');
    ws.onmessage=function(e){upd(JSON.parse(e.data));};
    ws.onclose=function(){setTimeout(conn,3000);};
  }
  function upd(d){
    document.getElementById('p-title').textContent=d.event_title||'Speed Timer';
    var sub=d.event_subtitle||'';
    var subEl=document.getElementById('p-subtitle');
    subEl.textContent=sub; subEl.style.display=sub?'block':'none';
    var has=d.heat_data_loaded&&d.current_heat_num>0;
    document.getElementById('blk-heat').style.visibility=has?'visible':'hidden';
    ['blk-wall-a','blk-name-a','blk-vs','blk-wall-b','blk-name-b'].forEach(function(id){
      document.getElementById(id).style.visibility=has?'visible':'hidden';
    });
    var pParts=['HEAT '+d.current_heat_num];
    if(d.current_round)    pParts.push(d.current_round.toUpperCase());
    if(d.current_category) pParts.push(d.current_category.toUpperCase());
    document.getElementById('p-heat').textContent=has?pParts.join(' \u00b7 '):'';
    document.getElementById('p-name-a').textContent=has?(d.wall_a_name||'—'):'—';
    document.getElementById('p-name-b').textContent=has?(d.wall_b_name||'—'):'—';
    var pL=document.getElementById('p-panel-l'),tL=document.getElementById('p-time-l'),rL=document.getElementById('p-react-l'),bRL=document.getElementById('blk-react-l');
    var pR=document.getElementById('p-panel-r'),tR=document.getElementById('p-time-r'),rR=document.getElementById('p-react-r'),bRR=document.getElementById('blk-react-r');
    pL.className='p-panel'; tL.className='p-time';
    pR.className='p-panel'; tR.className='p-time';
    var leftActive=!d.single_player_mode||d.left_finished||d.left_false_start||d.reaction_time_left!=0||(d.is_timer_running&&d.foot_left_pressed);
    var rightActive=!d.single_player_mode||d.right_finished||d.right_false_start||d.reaction_time_right!=0||(d.is_timer_running&&d.foot_right_pressed);
    // Local timer: start RAF loop on first running update; stop when server says done
    if(d.is_timer_running&&!_ltRunning)_startLocal(d.elapsed_time);
    if(!d.is_timer_running&&_ltRunning)_stopLocal();
    if(d.left_finished&&d.completion_time_left>0)_lDone=true;
    if(d.right_finished&&d.completion_time_right>0)_rDone=true;
    // Full reset: clear done flags so next run starts fresh
    if(!d.is_timer_running&&d.elapsed_time===0&&!d.left_finished&&!d.right_finished&&!d.left_false_start&&!d.right_false_start){_lDone=false;_rDone=false;}
    if(d.left_false_start){
      pL.classList.add('fs');
      if(d.friendly_false_starts){
        tL.textContent=(d.left_finished&&d.completion_time_left>0)?fmt(d.completion_time_left):fmt(d.elapsed_time);
      } else {
        tL.classList.add('fs-txt'); tL.textContent='FALSE START';
      }
    } else if(d.left_finished&&d.completion_time_left>0)tL.textContent=fmt(d.completion_time_left);
    else if(!leftActive)tL.textContent='0:00.00';
    // else: timer running — RAF handles display
    if(d.right_false_start){
      pR.classList.add('fs');
      if(d.friendly_false_starts){
        tR.textContent=(d.right_finished&&d.completion_time_right>0)?fmt(d.completion_time_right):fmt(d.elapsed_time);
      } else {
        tR.classList.add('fs-txt'); tR.textContent='FALSE START';
      }
    } else if(d.right_finished&&d.completion_time_right>0)tR.textContent=fmt(d.completion_time_right);
    else if(!rightActive)tR.textContent='0:00.00';
    // else: timer running — RAF handles display
    // Reaction times — show when known, hide when not applicable
    if(leftActive&&d.reaction_time_left!=0){rL.textContent='REACTION '+fmtReact(d.reaction_time_left);bRL.style.display='block';}else{bRL.style.display='none';}
    if(rightActive&&d.reaction_time_right!=0){rR.textContent='REACTION '+fmtReact(d.reaction_time_right);bRR.style.display='block';}else{bRR.style.display='none';}
    if(d.competition_complete){
      if(!d.left_false_start&&!d.right_false_start){
        if(d.completion_time_left>0&&d.completion_time_right>0){
          if(d.completion_time_left<d.completion_time_right){pL.classList.add('winner');tL.classList.add('win');}
          else if(d.completion_time_right<d.completion_time_left){pR.classList.add('winner');tR.classList.add('win');}
        } else if(d.completion_time_left>0){pL.classList.add('winner');tL.classList.add('win');}
        else if(d.completion_time_right>0){pR.classList.add('winner');tR.classList.add('win');}
      } else if(d.left_false_start&&!d.right_false_start&&d.right_finished){pR.classList.add('winner');tR.classList.add('win');}
      else if(d.right_false_start&&!d.left_false_start&&d.left_finished){pL.classList.add('winner');tL.classList.add('win');}
    }
    var nx=d.next_heats||[],nd=document.getElementById('blk-next');
    if(nx.length>0){
      nd.style.display='block';
      var h=nx[0];
      document.getElementById('p-next-items').innerHTML=
        '<div class="p-next-item"><span class="p-next-h">Next \u00b7 Heat '+h.heatNum+'</span>'+
        (h.wallA||'\u2014')+' vs '+(h.wallB||'\u2014')+'</div>';
    } else nd.style.display='none';
  }
  // ── Layout engine ──────────────────────────────────────────────────────
  var LABELS=['lbl-wall-a','lbl-vs','lbl-wall-b','lbl-next'];
  var LABEL_DEFAULTS={'lbl-wall-a':'Wall A','lbl-vs':'VS','lbl-wall-b':'Wall B','lbl-next':'Next Up'};
  var BLOCKS=['blk-hdr','blk-heat','blk-wall-a','blk-name-a','blk-vs','blk-wall-b','blk-name-b','blk-timer-l','blk-timer-r','blk-react-l','blk-react-r','blk-next'];
  var DEFAULTS={
    'blk-hdr':   {x:0,   y:0,   w:100, h:12,  fs:1},
    'blk-heat':  {x:0,   y:12,  w:100, h:8,   fs:1},
    'blk-wall-a':{x:2,   y:20,  w:35,  h:6,   fs:1},
    'blk-name-a':{x:2,   y:26,  w:35,  h:16,  fs:1},
    'blk-vs':    {x:41,  y:28,  w:18,  h:10,  fs:1},
    'blk-wall-b':{x:63,  y:20,  w:35,  h:6,   fs:1},
    'blk-name-b':{x:63,  y:26,  w:35,  h:16,  fs:1},
    'blk-timer-l':{x:1,  y:40,  w:48,  h:34,  fs:1},
    'blk-timer-r':{x:51, y:40,  w:48,  h:34,  fs:1},
    'blk-react-l':{x:1,  y:75,  w:48,  h:10,  fs:1},
    'blk-react-r':{x:51, y:75,  w:48,  h:10,  fs:1},
    'blk-next':  {x:0,   y:88,  w:100, h:10,  fs:1}
  };
  // units are % of viewport
  var layout={};
  var editMode=false;
  var drag=null; // {id,ox,oy}
  var resz=null; // {id,ox,oy,ow,oh}

  function pct(v,dim){return v/dim*100;}
  function applyBlock(id){
    var b=layout[id],el=document.getElementById(id);
    el.style.left=b.x+'%'; el.style.top=b.y+'%';
    el.style.width=b.w+'%'; el.style.height=b.h+'%';
    el.style.fontSize=b.fs+'em';
  }
  function saveLabels(){
    var lbls={};
    LABELS.forEach(function(id){lbls[id]=document.getElementById(id).textContent;});
    localStorage.setItem('pLabels',JSON.stringify(lbls));
  }
  function loadLabels(){
    var s=localStorage.getItem('pLabels');
    var lbls=s?JSON.parse(s):LABEL_DEFAULTS;
    LABELS.forEach(function(id){
      var el=document.getElementById(id);
      if(el)el.textContent=lbls[id]||LABEL_DEFAULTS[id];
    });
  }
  function saveLayout(){localStorage.setItem('pLayout',JSON.stringify(layout));}
  function loadLayout(){
    var s=localStorage.getItem('pLayout');
    layout=s?JSON.parse(s):JSON.parse(JSON.stringify(DEFAULTS));
    BLOCKS.forEach(applyBlock);
    loadLabels();
  }
  function resetLayout(){
    layout=JSON.parse(JSON.stringify(DEFAULTS));
    BLOCKS.forEach(applyBlock);
    LABELS.forEach(function(id){document.getElementById(id).textContent=LABEL_DEFAULTS[id];});
    saveLayout(); saveLabels();
  }
  function scaleFonts(id,factor){
    layout[id].fs=Math.round(layout[id].fs*factor*100)/100;
    applyBlock(id); saveLayout();
  }
  function toggleEdit(){
    editMode=!editMode;
    document.body.classList.toggle('edit-mode',editMode);
    document.getElementById('edit-btn').classList.toggle('active',editMode);
    document.getElementById('edit-btn').textContent=editMode?'✦ DONE':'✦ EDIT LAYOUT';
    LABELS.forEach(function(id){
      var el=document.getElementById(id);
      if(!el)return;
      el.contentEditable=editMode?'true':'false';
      if(editMode){
        el.addEventListener('blur',saveLabels);
        el.addEventListener('keydown',function(e){if(e.key==='Enter'){e.preventDefault();el.blur();}});
      }
    });
  }
  // Drag
  document.addEventListener('mousedown',function(e){
    if(!editMode)return;
    var bl=e.target.closest('.p-block');
    if(!bl)return;
    if(e.target.classList.contains('resize-handle')){
      resz={id:bl.id,ox:e.clientX,oy:e.clientY,
            ow:bl.offsetWidth,oh:bl.offsetHeight}; e.preventDefault(); return;
    }
    if(e.target.classList.contains('font-handle')||e.target.parentElement.classList.contains('font-handle'))return;
    drag={id:bl.id,ox:e.clientX-bl.offsetLeft,oy:e.clientY-bl.offsetTop};
    e.preventDefault();
  });
  document.addEventListener('mousemove',function(e){
    if(drag){
      var W=window.innerWidth,H=window.innerHeight;
      layout[drag.id].x=Math.max(0,Math.min(95,pct(e.clientX-drag.ox,W)));
      layout[drag.id].y=Math.max(0,Math.min(95,pct(e.clientY-drag.oy,H)));
      applyBlock(drag.id);
    }
    if(resz){
      var W=window.innerWidth,H=window.innerHeight;
      var nw=Math.max(5,resz.ow+(e.clientX-resz.ox));
      var nh=Math.max(3,resz.oh+(e.clientY-resz.oy));
      layout[resz.id].w=Math.min(100,pct(nw,W));
      layout[resz.id].h=Math.min(100,pct(nh,H));
      applyBlock(resz.id);
    }
  });
  document.addEventListener('mouseup',function(){
    if(drag||resz)saveLayout();
    drag=null; resz=null;
  });
  // Touch support
  document.addEventListener('touchstart',function(e){
    if(!editMode)return;
    var t=e.touches[0];
    var bl=t.target.closest('.p-block');
    if(!bl)return;
    if(t.target.classList.contains('resize-handle')){
      resz={id:bl.id,ox:t.clientX,oy:t.clientY,
            ow:bl.offsetWidth,oh:bl.offsetHeight}; e.preventDefault(); return;
    }
    drag={id:bl.id,ox:t.clientX-bl.offsetLeft,oy:t.clientY-bl.offsetTop};
    e.preventDefault();
  },{passive:false});
  document.addEventListener('touchmove',function(e){
    if(!drag&&!resz)return;
    var t=e.touches[0]; e.preventDefault();
    var W=window.innerWidth,H=window.innerHeight;
    if(drag){
      layout[drag.id].x=Math.max(0,Math.min(95,pct(t.clientX-drag.ox,W)));
      layout[drag.id].y=Math.max(0,Math.min(95,pct(t.clientY-drag.oy,H)));
      applyBlock(drag.id);
    }
    if(resz){
      var nw=Math.max(5,resz.ow+(t.clientX-resz.ox));
      var nh=Math.max(3,resz.oh+(t.clientY-resz.oy));
      layout[resz.id].w=Math.min(100,pct(nw,W));
      layout[resz.id].h=Math.min(100,pct(nh,H));
      applyBlock(resz.id);
    }
  },{passive:false});
  document.addEventListener('touchend',function(){
    if(drag||resz)saveLayout();
    drag=null; resz=null;
  });

  loadLayout();
  conn();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleApiStatus() {
  DynamicJsonDocument doc = buildStatusJson();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void sendWebSocketUpdate() {
  // Quick check - don't build JSON if no clients
  uint8_t totalClients = webSocket.connectedClients();
  if (totalClients == 0) {
    return;
  }

  DynamicJsonDocument doc = buildStatusJson();
  String message;
  serializeJson(doc, message);
  
  unsigned long updateStart = millis(); // Track total update time
  
  // Send to each client individually with timeout protection
  for(uint8_t i = 0; i < totalClients; i++) {
    if(webSocket.clientIsConnected(i)) {
      // Force break if we've already spent too much time
      if(millis() - updateStart > 100) {
        Serial.printf("Update timeout reached, skipping remaining %u clients\n", totalClients - i);
        break;
      }
      
      unsigned long sendStart = millis();
      
      if(!webSocket.sendTXT(i, message)) {
        Serial.printf("Client %u send failed, disconnecting\n", i);
        webSocket.disconnect(i);
      } else {
        unsigned long sendTime = millis() - sendStart;
        if(sendTime > 50) {
          Serial.printf("Slow send to client %u: %lu ms\n", i, sendTime);
        }
      }
    }
  }
  
  unsigned long totalTime = millis() - updateStart;
  if(totalTime > 100) {
    Serial.printf("WebSocket update took %lu ms total\n", totalTime);
  }
}

void handleApiStart() {
  if (canStartCompetition()) {
    setLaneActivity();
    resetCompetitionState();
    startAudioSequence();
    server.send(200, "application/json", "{\"status\":\"started\"}");
  } else if (singlePlayerMode && !footLeftPressed && !footRightPressed) {
    server.send(400, "application/json", "{\"error\":\"at least one foot sensor must be pressed in single player mode\"}");
  } else if (!singlePlayerMode && (!footLeftPressed || !footRightPressed)) {
    server.send(400, "application/json", "{\"error\":\"both foot sensors must be pressed in competition mode\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"audio already playing\"}");
  }
}

void handleApiStop() {
  if (isAnyTimerRunning()) {
    stopTimer();
    server.send(200, "application/json", "{\"status\":\"stopped\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"timer not running\"}");
  }
}

void handleApiReset() {
  resetTimer();
  if (isPlayingAudio) {
    isPlayingAudio = false;
    currentAudioStep = 0;
    stopTone();
  }
  if (isPlayingFalseStart) {
    isPlayingFalseStart = false;
    currentAudioStep = 0;
    stopTone();
  }
  sendWebSocketUpdate();
  server.send(200, "application/json", "{\"status\":\"reset\"}");
}

void handleApiToggleKidsMode() {
  kidsModeSensorsEnabled = !kidsModeSensorsEnabled;
  server.send(200, "application/json", "{\"status\":\"kids_mode_toggled\",\"enabled\":" + String(kidsModeSensorsEnabled ? "true" : "false") + "}");
  sendWebSocketUpdate();
}

void setup() {
  Serial.begin(115200);

  // Load Apps Script URL from NVS (persists across reboots)
  prefs.begin("timer", false);
  String stored = prefs.getString("scriptUrl", "");
  if (stored.length() > 30) {
    appsScriptUrl = stored;
    sheetsLinked  = true;
    Serial.println("Loaded Apps Script URL from NVS");
  } else {
    appsScriptUrl = appsScriptUrlDefault;
    sheetsLinked  = true; // default URL counts as linked
    Serial.println("Using default Apps Script URL");
  }

  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_LEFT, INPUT_PULLUP);
  pinMode(FOOT_SENSOR_RIGHT, INPUT_PULLUP);
  pinMode(STOP_SENSOR_LEFT_KIDS, INPUT_PULLUP);
  pinMode(STOP_SENSOR_RIGHT_KIDS, INPUT_PULLUP);

  initializeLEDs();

  ledcAttach(AUDIO_PIN, 1000, LEDC_RESOLUTION);
  ledcWrite(AUDIO_PIN, 0);

  /////////////WIFI/////////////

  Serial.println("Setting up Access Point...");

  // Configure as Access Point
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  if (strlen(sta_ssid) > 1) {
    IPAddress staticIP(192, 168, 0, 169);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(8, 8, 8, 8);
    WiFi.config(staticIP, gateway, subnet, dns);
    WiFi.begin(sta_ssid, sta_password);
    Serial.print("Connecting to gym WiFi: "); Serial.println(sta_ssid);
    unsigned long wt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wt < 8000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Gym WiFi IP: "); Serial.println(WiFi.localIP());
      fetchNextHeats();
    } else {
      Serial.println("Gym WiFi unavailable, AP-only mode");
    }
  }

  // Get AP IP address
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Access Point IP: ");
  Serial.println(apIP);
  Serial.print("Network Name: ");
  Serial.println(ap_ssid);

  // Start DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started for captive portal");

  /////////////WIFI/////////////

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/start", HTTP_POST, handleApiStart);
  server.on("/api/stop", HTTP_POST, handleApiStop);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/toggle_kids_mode", HTTP_POST, handleApiToggleKidsMode);
  server.on("/projector",            HTTP_GET,  handleProjector);
  server.on("/api/confirm_heat",     HTTP_POST, handleApiConfirmHeat);
  server.on("/api/skip_heat",        HTTP_POST, handleApiSkipHeat);
  server.on("/api/fetch_heats",      HTTP_POST, handleApiFetchHeats);
  server.on("/api/set_title",        HTTP_POST, handleApiSetTitle);
  server.on("/api/get_settings",     HTTP_GET,  handleApiGetSettings);
  server.on("/api/set_settings",     HTTP_POST, handleApiSetSettings);

  server.on("/generate_204", HTTP_GET, handleCaptivePortal);         // Android
  server.on("/fwlink", HTTP_GET, handleCaptivePortal);               // Microsoft
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);  // Apple
  server.onNotFound(handleCaptivePortal);                            // Catch all other requests

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.enableHeartbeat(1000, 500, 2);  // ping every 15s, timeout 3s, disconnect after 2 failed pings

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("WebSocket server started on port 81");
}

void loop() {
  unsigned long loopStart = millis();
  loopCount++;

  server.handleClient();
  webSocket.loop();
  checkButtons();
  updateAudioSequence();
  updateTimer();
  updateWebSocket();
  dnsServer.processNextRequest();

  // WiFi station reconnect — only when truly disconnected/failed, not while connecting
  if (strlen(sta_ssid) > 1 && millis() - lastWifiRetry > 30000) {
    wl_status_t wst = WiFi.status();
    if (wst == WL_DISCONNECTED || wst == WL_CONNECT_FAILED || wst == WL_CONNECTION_LOST || wst == WL_NO_SSID_AVAIL) {
      WiFi.begin(sta_ssid, sta_password);
      lastWifiRetry = millis();
    }
  }
  // Pending fetch (triggered by link or confirm) — fires after response was sent
  if (pendingHeatFetch && WiFi.status() == WL_CONNECTED && appsScriptUrl.length() > 30) {
    pendingHeatFetch = false;
    fetchNextHeats();
  }
  // Periodic heat refresh — only when timer is fully idle
  if (WiFi.status() == WL_CONNECTED && appsScriptUrl.length() > 30 &&
      !isAnyTimerRunning() && !isPlayingAudio && !isPlayingFalseStart &&
      millis() - lastHeatFetch > HEAT_FETCH_INTERVAL) {
    fetchNextHeats();
  }

  lastLoopTime = millis() - loopStart;

  // Display stats every second and checks dns (10 times per second)
  if (millis() - lastTime >= 100) {
    if (loopCount < 80) {
      Serial.print("Loops/sec: ");
      Serial.print(loopCount);
      Serial.print(" | Last loop took: ");
      Serial.print(lastLoopTime);
      Serial.println(" ms");
    }
    loopCount = 0;
    lastTime = millis();
  }
}
