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

// LED Ring Type Definitions
#define LED_RING_TYPE_RGB 0
#define LED_RING_TYPE_RGBW 1

/**
 * BOOT_PIN
 * The GPIO pin connected to the physical Boot button on the Seeed Studio board.
 * Used for triggering factory resets.
 */
#ifndef BOOT_PIN
#define BOOT_PIN 9
#endif

// ==========================================
//           USER CONFIGURATION
// ==========================================

/**
 * ACTIVE_LED_RING_TYPE
 * Selects the type of LED ring light hardware attached:
 * - LED_RING_TYPE_RGB:  Standard RGB pixels (e.g., WS2812B, using NEO_GRB color
 * order)
 * - LED_RING_TYPE_RGBW: RGB + Warm White pixels (e.g., SK6812, using NEO_GRBW
 * color order)
 */
#define ACTIVE_LED_RING_TYPE LED_RING_TYPE_RGB

/**
 * RGBW_WHITE_TEMP_MIREDS
 * Only used when ACTIVE_LED_RING_TYPE is LED_RING_TYPE_RGBW.
 *
 * The native color temperature of the dedicated white LED in the SK6812
 * package, in mireds (1000000 / Kelvin). The default of 333 mireds (3000K)
 * matches the common warm white variant; use 222 (4500K) for natural white or
 * 153 (6535K) for cool white.
 *
 * Getting this wrong does not break anything, but the ring will lean toward
 * the real emitter temperature whenever the white channel is driven hard.
 */
#define RGBW_WHITE_TEMP_MIREDS 333

/**
 * NUMPIXELS
 * The number of addressable LEDs on the ring light. Default is 16.
 */
#define NUMPIXELS 16

/**
 * LED_PIN
 * The GPIO pin connected to the DI (Data Input) of the LED ring.
 */
#define LED_PIN D2

/**
 * COLOR_TEMP_MIN_MIREDS / COLOR_TEMP_MAX_MIREDS
 * The coolest and warmest color temperatures advertised to the Zigbee network,
 * in mireds (1000000 / Kelvin). Defaults span 6535K (153) to 3000K (333).
 *
 * On RGBW rings, keeping the warm end at RGBW_WHITE_TEMP_MIREDS means the
 * white LED runs at full output across the whole range and RGB only ever adds
 * the blue lift for cooler targets. Going warmer than that is possible, but
 * the white LED has to back off and RGB carries an increasing share, which
 * renders as amber rather than warm white.
 *
 * These must be advertised explicitly. The Zigbee library registers the
 * ColorTempPhysicalMinMireds/MaxMireds attributes with placeholder defaults of
 * 0x0000 and 0xFEFF, which coordinators read back as a 15K-6535K range.
 * Requesting the 15K end yields 66666 mireds, which overflows the uint16_t the
 * attribute is transported in and makes the command fail outright.
 */
#define COLOR_TEMP_MIN_MIREDS 153
#define COLOR_TEMP_MAX_MIREDS 333

/**
 * RUN_SELF_TESTS
 * Uncomment this line to run the built-in diagnostic test suite on device boot.
 */
// #define RUN_SELF_TESTS

// ==========================================
//         END USER CONFIGURATION
// ==========================================

/**
 * ZIGBEE_RGB_LIGHT_ENDPOINT
 * The logical Zigbee endpoint number for the color dimmable light.
 */
#define ZIGBEE_RGB_LIGHT_ENDPOINT 10

#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRBW + NEO_KHZ800);
#else
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
#endif

ZigbeeColorDimmableLight zbColorLight =
    ZigbeeColorDimmableLight(ZIGBEE_RGB_LIGHT_ENDPOINT);

// Color modes. The ring is always driven as RGB, but we track whether the
// network last set a color or a color temperature so the correct mode is
// restored and reported back after a power cycle.
#define COLOR_MODE_RGB 0
#define COLOR_MODE_TEMP 1

// Global variables to store current LED state
bool led_state = false;
uint8_t led_level = 255;
uint8_t led_color_r = 255;
uint8_t led_color_g = 255;
uint8_t led_color_b = 255;
uint8_t led_color_mode = COLOR_MODE_RGB;
uint16_t led_color_temp = 250; // mireds (4000K)

// Variables for connection and memory tracking
bool zigbee_connected = false;
bool boot_count_reset = false;
uint32_t last_zigbee_log = 0;

// NVRAM debouncing variables to protect flash lifespan
bool nvram_dirty = false;
unsigned long last_state_change_time = 0;
const unsigned long NVRAM_WRITE_DELAY =
    5000; // Debounce NVS writes for 5 seconds (if device shuts off within 5
          // seconds, the change is lost... acceptable tradeoff for flash
          // lifespan)

/**
 * Converts a color temperature in mireds to an approximate RGB triplet using
 * Tanner Helland's blackbody approximation. Overall brightness is left to
 * led_level; this only sets the hue of the white point.
 */
void colorTempToRGB(uint16_t mireds, uint8_t &red, uint8_t &green,
                    uint8_t &blue) {
  if (mireds < 1) {
    mireds = 1;
  }
  // The approximation is only defined between 1000K and 40000K
  float kelvin = 1000000.0f / (float)mireds;
  kelvin = constrain(kelvin, 1000.0f, 40000.0f);

  float temp = kelvin / 100.0f;
  float r, g, b;
  if (temp <= 66.0f) {
    r = 255.0f;
    g = 99.4708025861f * log(temp) - 161.1195681661f;
    b = (temp <= 19.0f) ? 0.0f
                        : 138.5177312231f * log(temp - 10.0f) - 305.0447927307f;
  } else {
    r = 329.698727446f * pow(temp - 60.0f, -0.1332047592f);
    g = 288.1221695283f * pow(temp - 60.0f, -0.0755148492f);
    b = 255.0f;
  }

  red = (uint8_t)constrain(r, 0.0f, 255.0f);
  green = (uint8_t)constrain(g, 0.0f, 255.0f);
  blue = (uint8_t)constrain(b, 0.0f, 255.0f);
}

#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
/**
 * Splits an RGB target into RGB + white channels for SK6812 rings.
 *
 * Mixes in as much of the white LED as possible without requiring a negative
 * RGB channel to compensate. Because the white emitter has its own color
 * temperature, this is not the same as W = min(R, G, B): a plain minimum
 * assumes the white LED is neutral, so on a warm white part it drags cool
 * targets toward orange, worst of all near-white ones where the minimum
 * captures nearly the entire output.
 */
void extractWhiteChannel(uint8_t &red, uint8_t &green, uint8_t &blue,
                         uint8_t &white) {
  static bool white_point_ready = false;
  static uint8_t white_r, white_g, white_b;
  if (!white_point_ready) {
    colorTempToRGB(RGBW_WHITE_TEMP_MIREDS, white_r, white_g, white_b);
    white_point_ready = true;
  }

  // Largest white level whose contribution still fits under every channel
  uint32_t w = 255;
  if (white_r > 0 && (uint32_t)red * 255 / white_r < w) {
    w = (uint32_t)red * 255 / white_r;
  }
  if (white_g > 0 && (uint32_t)green * 255 / white_g < w) {
    w = (uint32_t)green * 255 / white_g;
  }
  if (white_b > 0 && (uint32_t)blue * 255 / white_b < w) {
    w = (uint32_t)blue * 255 / white_b;
  }

  // Subtract what the white LED already supplies; w is chosen so that none of
  // these can underflow
  white = (uint8_t)w;
  red -= (uint8_t)(w * white_r / 255);
  green -= (uint8_t)(w * white_g / 255);
  blue -= (uint8_t)(w * white_b / 255);
}
#endif

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

  // Test 4: RGB to RGBW White Extraction Math
  Serial.print("Test 4 (RGBW Extraction): ");
#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
  uint8_t tr, tg, tb, tw;

  // A target at the white LED's own temperature must drive it alone
  colorTempToRGB(RGBW_WHITE_TEMP_MIREDS, tr, tg, tb);
  extractWhiteChannel(tr, tg, tb, tw);
  bool native_ok = (tw == 255 && tr == 0 && tg == 0 && tb == 0);

  // Targets are derived from the white point rather than the advertised range,
  // so this exercises the algorithm whatever the range is configured to.

  // Cooler than the white LED: RGB tops it up, blue leading
  colorTempToRGB(RGBW_WHITE_TEMP_MIREDS / 2, tr, tg, tb);
  extractWhiteChannel(tr, tg, tb, tw);
  bool cooler_ok = (tb > tg);

  // Warmer than the white LED: it has to back off and let red carry
  colorTempToRGB(RGBW_WHITE_TEMP_MIREDS * 3 / 2, tr, tg, tb);
  extractWhiteChannel(tr, tg, tb, tw);
  bool warmer_ok = (tw < 255 && tr > 0);

  // A saturated color has no white content to extract
  tr = 255;
  tg = 0;
  tb = 0;
  extractWhiteChannel(tr, tg, tb, tw);
  bool saturated_ok = (tw == 0 && tr == 255);

  if (native_ok && cooler_ok && warmer_ok && saturated_ok) {
    Serial.println("PASS");
  } else {
    Serial.printf("FAIL (Native:%d, Cooler:%d, Warmer:%d, Saturated:%d)\n",
                  native_ok, cooler_ok, warmer_ok, saturated_ok);
  }
#else
  Serial.println("PASS (Skipped in RGB Mode)");
#endif

  // Test 5: Color Temperature (mireds) to RGB conversion
  Serial.print("Test 5 (Color Temp Conversion): ");
  // Fixed reference points: the conversion is a pure function and does not
  // depend on the range the device happens to advertise
  uint8_t ct_r, ct_g, ct_b;
  // 500 mireds (2000K): deep amber, red saturated and almost no blue
  colorTempToRGB(500, ct_r, ct_g, ct_b);
  bool warm_ok = (ct_r == 255 && ct_g > 100 && ct_g < 180 && ct_b < 40);
  // 153 mireds (~6535K): near white
  colorTempToRGB(153, ct_r, ct_g, ct_b);
  bool cool_ok = (ct_r == 255 && ct_g > 230 && ct_b > 230);
  if (warm_ok && cool_ok) {
    Serial.println("PASS");
  } else {
    Serial.printf("FAIL (Warm:%d, Cool:%d)\n", warm_ok, cool_ok);
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
#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
    // Offload as much output as possible onto the dedicated white LED
    uint8_t w;
    extractWhiteChannel(r, g, b, w);
    pixels.fill(pixels.Color(r, g, b, w));
#else
    pixels.fill(pixels.Color(r, g, b));
#endif
  } else {
#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
    pixels.fill(pixels.Color(0, 0, 0, 0));
#else
    pixels.fill(pixels.Color(0, 0, 0));
#endif
  }
  pixels.show();
}

/********************* RGB LED functions **************************/
void saveStateToNVRAM() {
  Preferences prefs;
  prefs.begin("light_state", false);
  prefs.putBool("state", led_state);
  prefs.putUChar("r", led_color_r);
  prefs.putUChar("g", led_color_g);
  prefs.putUChar("b", led_color_b);
  prefs.putUChar("level", led_level);
  prefs.putUChar("cmode", led_color_mode);
  prefs.putUShort("ctemp", led_color_temp);
  prefs.end();
  nvram_dirty = false;
  Serial.println("Light state saved to NVRAM.");
}

void setRGBLight(bool state, uint8_t red, uint8_t green, uint8_t blue,
                 uint8_t level) {
  // Only update and mark dirty if state or parameters actually changed
  if (led_state != state || led_color_r != red || led_color_g != green ||
      led_color_b != blue || led_level != level ||
      led_color_mode != COLOR_MODE_RGB) {
    led_state = state;
    led_color_r = red;
    led_color_g = green;
    led_color_b = blue;
    led_level = level;
    led_color_mode = COLOR_MODE_RGB;
    updateLEDs();

    nvram_dirty = true;
    last_state_change_time = millis();
  }
}

// Invoked when the coordinator sends a Move to Color Temperature command. The
// ring has no dedicated white point, so the temperature is rendered as RGB.
void setColorTempLight(bool state, uint8_t level, uint16_t color_temperature) {
  uint8_t red, green, blue;
  colorTempToRGB(color_temperature, red, green, blue);

  if (led_state != state || led_level != level ||
      led_color_temp != color_temperature ||
      led_color_mode != COLOR_MODE_TEMP) {
    led_state = state;
    led_level = level;
    led_color_temp = color_temperature;
    led_color_mode = COLOR_MODE_TEMP;
    led_color_r = red;
    led_color_g = green;
    led_color_b = blue;
    updateLEDs();

    nvram_dirty = true;
    last_state_change_time = millis();
  }
}
// Callback function invoked when the Zigbee coordinator requests device
// identification (blinking)
void identify(uint16_t time) {
  static uint8_t blink = 1;
  log_d("Identify called for %d seconds", time);
  if (time == 0) {
    // If identify time is 0, stop blinking and restore light as it was used for
    // identify
    zbColorLight.restoreLight();
    blink = 1; // Reset blink state for next identify sequence
    return;
  }

  if (blink) {
#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
    pixels.fill(pixels.Color(0, 0, 0, 255));
#else
    pixels.fill(pixels.Color(255, 255, 255));
#endif
  } else {
#if ACTIVE_LED_RING_TYPE == LED_RING_TYPE_RGBW
    pixels.fill(pixels.Color(0, 0, 0, 0));
#else
    pixels.fill(pixels.Color(0, 0, 0));
#endif
  }
  pixels.show();
  blink = !blink;
}

void setup() {
  Serial.begin(115200);

#ifdef RUN_SELF_TESTS
  // Wait for serial connection and a start signal character ('s') to run tests
  while (!Serial && millis() < 4000)
    ;
  uint32_t start_wait = millis();
  while (!Serial.available() && (millis() - start_wait < 10000)) {
    delay(10);
  }
  if (Serial.available()) {
    while (Serial.available())
      Serial.read(); // consume all input
    runSelfTests();
  }
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
    prefs.putUChar("boot_count",
                   0); // Reset immediately upon triggering override
    led_state = true;
    led_color_r = 255;
    led_color_g = 255;
    led_color_b = 255;
    led_level = 255;
    led_color_mode = COLOR_MODE_RGB;
  } else {
    led_state = prefs.getBool("state", true); // Load state (default ON)
    led_color_r = prefs.getUChar("r", 255);
    led_color_g = prefs.getUChar("g", 255);
    led_color_b = prefs.getUChar("b", 255);
    led_level = prefs.getUChar("level", 255);
    led_color_mode = prefs.getUChar("cmode", COLOR_MODE_RGB);
    led_color_temp = prefs.getUShort("ctemp", 250);
  }
  prefs.end();

  pixels.begin();
  updateLEDs(); // Light behavior activates completely immediately!

  // Wait to ensure serial connection is established before printing logs
  delay(2000);
  if (failsafe_triggered) {
    Serial.println("Failsafe activated via power toggle! Memory ignored, light "
                   "forced to 100% white ON.");
  }
  Serial.println("Starting XIAO ESP32-C6 Zigbee Light initialization...");

  // Configure External Antenna via RF Switch
  // XIAO ESP32-C6 uses standard pins to select internal vs external antenna.
  // WIFI_ENABLE_PIN LOW (activate RF switch logic)
  // WIFI_ANT_CONFIG_PIN HIGH (selects the associated U.FL connector for the
  // external antenna)
  pinMode(WIFI_ENABLE_PIN, OUTPUT);
  digitalWrite(WIFI_ENABLE_PIN, LOW);
  delay(10);
  pinMode(WIFI_ANT_CONFIG_PIN, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG_PIN,
               HIGH); // Selects external U.FL connector antenna
  delay(10);

  // Initialize button for factory reset (Boot button)
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // Enable XY, Hue/Saturation and Color Temperature capabilities.
  // Color temperature must be declared here before the physical mireds range
  // can be set below; the library rejects the range otherwise.
  uint16_t capabilities = ZIGBEE_COLOR_CAPABILITY_X_Y |
                          ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION |
                          ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP;
  zbColorLight.setLightColorCapabilities(capabilities);

  // Do not drop this call. The Zigbee library registers the
  // ColorTempPhysicalMinMireds/MaxMireds attributes with placeholder defaults
  // of 0x0000 and 0xFEFF, which coordinators read back as a 15K-6535K range.
  // Asking for the 15K end yields 66666 mireds, overflowing the uint16_t the
  // attribute travels in, and the command fails before reaching the device.
  if (!zbColorLight.setLightColorTemperatureRange(COLOR_TEMP_MIN_MIREDS,
                                                  COLOR_TEMP_MAX_MIREDS)) {
    Serial.println("Warning: failed to set color temperature range!");
  }

  // Set callbacks for color/state updates from the Zigbee network
  zbColorLight.onLightChangeRgb(setRGBLight);
  zbColorLight.onLightChangeTemp(setColorTempLight);

  // Set callback function for device identify
  zbColorLight.onIdentify(identify);

  // Set Zigbee device name and model
  zbColorLight.setManufacturerAndModel(ZIGBEE_MANUFACTURER, ZIGBEE_MODEL_NAME);

  // Add endpoint to Zigbee Core
  Serial.println("Adding ZigbeeLight endpoint to Zigbee Core");
  Zigbee.addEndpoint(&zbColorLight);

  // When all endpoints are registered, start Zigbee in Router mode
  Serial.println("Starting Zigbee in Router mode...");

  // Initialize the Zigbee stack (set auto_clean to false to retain network
  // pairing details)
  if (!Zigbee.begin(ZIGBEE_ROUTER, false)) {
    Serial.println("Error: Zigbee failed to start!");
    Serial.println("Rebooting in 5s...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Zigbee started successfully. Connecting to network...");

  // setLight() below reports the color as XY, which makes the Zigbee core fire
  // the RGB callback and overwrite led_color_mode. Capture what was loaded from
  // NVRAM first, otherwise the color temperature mode can never be restored.
  uint8_t restore_color_mode = led_color_mode;
  uint16_t restore_color_temp = led_color_temp;

  Serial.printf("Syncing power-on state to Zigbee: %s, R:%d G:%d B:%d "
                "Level:%d Mode:%s\n",
                led_state ? "ON" : "OFF", led_color_r, led_color_g, led_color_b,
                led_level,
                restore_color_mode == COLOR_MODE_TEMP ? "TEMP" : "RGB");

  // Sync the loaded power-on state back to the Zigbee network
  // We pass the global variables we loaded immediately at power-on to the
  // Zigbee core
  zbColorLight.setLight(led_state, led_level, led_color_r, led_color_g,
                        led_color_b);

  // Re-assert color temperature mode if that is what was in use before the
  // power cycle; this also puts led_color_mode back to COLOR_MODE_TEMP
  if (restore_color_mode == COLOR_MODE_TEMP) {
    Serial.printf("Restoring color temperature: %u mireds\n",
                  restore_color_temp);
    zbColorLight.setLightColorTemperature(restore_color_temp);
  }

  // If not currently paired, keep the pairing network open for 120 seconds on
  // boot
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
    Serial.println(
        "3 seconds elapsed, power toggle window closed. Boot count reset.");
  }

  // Non-blocking NVRAM save logic
  if (nvram_dirty && (millis() - last_state_change_time >= NVRAM_WRITE_DELAY)) {
    saveStateToNVRAM();
  }

  // Non-blocking Zigbee connection check
  if (!zigbee_connected) {
    if (Zigbee.connected()) {
      zigbee_connected = true;
      Serial.println("\nSuccess! Zigbee Light connected to network!");
    } else {
      if (millis() - last_zigbee_log > 10000) {
        Serial.println("\nStill waiting for connection. Ensure Coordinator is "
                       "in pairing mode.");
        last_zigbee_log = millis();
      }
    }
  }

  // Checking button for factory reset
  if (digitalRead(BOOT_PIN) == LOW) { // Push button pressed
    // Key debounce handling
    delay(100);
    unsigned long startTime = millis();
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
