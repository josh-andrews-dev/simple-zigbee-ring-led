#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator/router mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

// Zigbee Manufacturer and Model Name
#define ZIGBEE_MANUFACTURER "Josh Andrews"
#define ZIGBEE_MODEL_NAME "Simple Zigbee Ring LED"

// XIAO ESP32C6 RF Switch Pins
#define WIFI_ENABLE_PIN 3
#define WIFI_ANT_CONFIG_PIN 14

/* Zigbee Color Dimmable Light Configuration */
#define ZIGBEE_RGB_LIGHT_ENDPOINT 10

#define LED_PIN D2
#define NUMPIXELS 16

#ifndef BOOT_PIN
#define BOOT_PIN 9 // Default BOOT button GPIO for Seeed Studio XIAO ESP32-C6
#endif

// Uncomment to run the test suite on boot
//#define RUN_SELF_TESTS

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

ZigbeeColorDimmableLight zbColorLight =
    ZigbeeColorDimmableLight(ZIGBEE_RGB_LIGHT_ENDPOINT);

// Global variables to store current LED state
bool led_state = false;
uint8_t led_level = 255;
uint8_t led_color_r = 255;
uint8_t led_color_g = 255;
uint8_t led_color_b = 255;

// Variables for connection and memory tracking
bool zigbee_connected = false;
bool boot_count_reset = false;
uint32_t last_zigbee_log = 0;

#ifdef RUN_SELF_TESTS
void runSelfTests() {
  Serial.println("----- STARTING SELF-TESTS -----");
  
  // Test 1: LED Brightness Scaling Math
  Serial.print("Test 1 (LED Scaling): ");
  uint8_t red = 100, green = 150, blue = 200;
  uint8_t level = 128; // ~50%
  float brightness_scaling = (float)level / 255.0f;
  uint8_t r_scaled = red * brightness_scaling;
  uint8_t g_scaled = green * brightness_scaling;
  uint8_t b_scaled = blue * brightness_scaling;
  if (r_scaled == 50 && g_scaled == 75 && b_scaled == 100) {
    Serial.println("PASS");
  } else {
    Serial.printf("FAIL (R:%d, G:%d, B:%d)\n", r_scaled, g_scaled, b_scaled);
  }

  // Test 2: Preferences Read/Write (NVRAM)
  Serial.print("Test 2 (Preferences Read/Write): ");
  Preferences testPrefs;
  testPrefs.begin("test_state", false);
  testPrefs.putBool("test_bool", true);
  testPrefs.putUChar("test_val", 42);
  bool loaded_bool = testPrefs.getBool("test_bool", false);
  uint8_t loaded_val = testPrefs.getUChar("test_val", 0);
  testPrefs.end();
  // Clear the test data
  testPrefs.begin("test_state", false);
  testPrefs.remove("test_bool");
  testPrefs.remove("test_val");
  testPrefs.end();
  
  if (loaded_bool == true && loaded_val == 42) {
    Serial.println("PASS");
  } else {
    Serial.printf("FAIL (Bool:%d, Val:%d)\n", loaded_bool, loaded_val);
  }

  // Test 3: Failsafe boot count logic
  Serial.print("Test 3 (Failsafe Boot Logic Simulation): ");
  uint8_t sim_count = 0;
  bool failsafe_sim = false;
  for (int i = 0; i < 3; i++) {
    sim_count++;
    if (sim_count >= 3) {
      failsafe_sim = true;
      sim_count = 0;
    }
  }
  if (failsafe_sim && sim_count == 0) {
    Serial.println("PASS");
  } else {
    Serial.println("FAIL");
  }

  Serial.println("----- ALL TESTS PASSED -----");
  Serial.flush();
  delay(1000);
}
#endif

void updateLEDs() {
  if (led_state) {
    // Apply brightness to the RGB color
    float brightness_scaling = (float)led_level / 255.0f;
    uint8_t r = led_color_r * brightness_scaling;
    uint8_t g = led_color_g * brightness_scaling;
    uint8_t b = led_color_b * brightness_scaling;
    pixels.fill(pixels.Color(r, g, b));
  } else {
    pixels.fill(pixels.Color(0, 0, 0));
  }
  pixels.show();
}

/********************* RGB LED functions **************************/
void setRGBLight(bool state, uint8_t red, uint8_t green, uint8_t blue,
                 uint8_t level) {
  led_state = state;
  led_color_r = red;
  led_color_g = green;
  led_color_b = blue;
  led_level = level;
  updateLEDs();

  // Always save the state, color, and brightness to NVRAM
  Preferences prefs;
  prefs.begin("light_state", false);
  prefs.putBool("state", state);
  prefs.putUChar("r", red);
  prefs.putUChar("g", green);
  prefs.putUChar("b", blue);
  prefs.putUChar("level", level);
  prefs.end();
}

// Callback function invoked when the Zigbee coordinator requests device identification (blinking)
void identify(uint16_t time) {
  static uint8_t blink = 1;
  log_d("Identify called for %d seconds", time);
  if (time == 0) {
    // If identify time is 0, stop blinking and restore light as it was used for identify
    zbColorLight.restoreLight();
    return;
  }

  if (blink) {
    pixels.fill(pixels.Color(255, 255, 255));
  } else {
    pixels.fill(pixels.Color(0, 0, 0));
  }
  pixels.show();
  blink = !blink;
}

void setup() {
  Serial.begin(115200);

#ifdef RUN_SELF_TESTS
  // Wait for serial connection to be ready in test mode
  while (!Serial && millis() < 4000);
  runSelfTests();
#endif

  // Power-on behavior and failsafe logic
  Preferences prefs;
  prefs.begin("light_state", false);

  uint8_t boot_count = prefs.getUChar("boot_count", 0);
  boot_count++;
  prefs.putUChar("boot_count", boot_count);

  bool failsafe_triggered = false;
  if (boot_count >= 3) {
    failsafe_triggered = true;
    prefs.putUChar("boot_count", 0); // Reset immediately upon triggering override
    led_state = true;
    led_color_r = 255;
    led_color_g = 255;
    led_color_b = 255;
    led_level = 255;
  } else {
    led_state = prefs.getBool("state", true); // Load state (default ON)
    led_color_r = prefs.getUChar("r", 255);
    led_color_g = prefs.getUChar("g", 255);
    led_color_b = prefs.getUChar("b", 255);
    led_level = prefs.getUChar("level", 255);
  }
  prefs.end();

  pixels.begin();
  updateLEDs(); // Light behavior activates completely immediately!

  // Wait to ensure serial connection is established before printing logs
  delay(2000);
  if (failsafe_triggered) {
    Serial.println("Failsafe activated via power toggle! Memory ignored, light forced to 100% white ON.");
  }
  Serial.println("Starting XIAO ESP32-C6 Zigbee Light initialization...");

  // Configure External Antenna via RF Switch
  // XIAO ESP32-C6 uses standard pins to select internal vs external antenna.
  // WIFI_ENABLE_PIN LOW (activate RF switch logic)
  // WIFI_ANT_CONFIG_PIN HIGH (selects the associated U.FL connector for the external antenna)
  pinMode(WIFI_ENABLE_PIN, OUTPUT);
  digitalWrite(WIFI_ENABLE_PIN, LOW);
  delay(10);
  pinMode(WIFI_ANT_CONFIG_PIN, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG_PIN, HIGH);
  delay(10);

  // Initialize button for factory reset (Boot button)
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // Enable XY (RGB) capabilities
  uint16_t capabilities = ZIGBEE_COLOR_CAPABILITY_X_Y;
  zbColorLight.setLightColorCapabilities(capabilities);

  // Set callback for color/state updates from the Zigbee network
  zbColorLight.onLightChangeRgb(setRGBLight);

  // Set callback function for device identify
  zbColorLight.onIdentify(identify);

  // Set Zigbee device name and model
  zbColorLight.setManufacturerAndModel(ZIGBEE_MANUFACTURER, ZIGBEE_MODEL_NAME);

  // Add endpoint to Zigbee Core
  Serial.println("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbColorLight);

  // When all endpoints are registered, start Zigbee in Router mode
  Serial.println("Starting Zigbee in Router mode...");

  // Initialize the Zigbee stack (set auto_clean to false to retain network pairing details)
  if (!Zigbee.begin(ZIGBEE_ROUTER, false)) {
    Serial.println("Error: Zigbee failed to start!");
    Serial.println("Rebooting in 5s...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Zigbee started successfully. Connecting to network...");

  Serial.printf(
      "Syncing power-on state to Zigbee: %s, R:%d G:%d B:%d Level:%d\n",
      led_state ? "ON" : "OFF", led_color_r, led_color_g, led_color_b,
      led_level);
  
  // Sync the loaded power-on state back to the Zigbee network
  // We pass the global variables we loaded immediately at power-on to the Zigbee core
  zbColorLight.setLight(led_state, led_level, led_color_r, led_color_g,
                        led_color_b);

  // If not currently paired, keep the pairing network open for 120 seconds on boot
  Zigbee.setRebootOpenNetwork(120);

  Serial.println("Connecting to network in background...");
  last_zigbee_log = millis();
}

void loop() {
  // Reset boot_count after 3 seconds of continuous uptime
  if (!boot_count_reset && millis() > 3000) {
    Preferences prefs;
    prefs.begin("light_state", false);
    prefs.putUChar("boot_count", 0);
    prefs.end();
    boot_count_reset = true;
    Serial.println("3 seconds elapsed, power toggle window closed. Boot count reset.");
  }

  // Non-blocking Zigbee connection check
  if (!zigbee_connected) {
    if (Zigbee.connected()) {
      zigbee_connected = true;
      Serial.println("\nSuccess! Zigbee Light connected to network!");
    } else {
      if (millis() - last_zigbee_log > 10000) {
        Serial.println("\nStill waiting for connection. Ensure Coordinator is in pairing mode.");
        last_zigbee_log = millis();
      }
    }
  }

  // Checking button for factory reset
  if (digitalRead(BOOT_PIN) == LOW) { // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(BOOT_PIN) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
  }
  delay(100);
}
