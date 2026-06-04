# Simple Zigbee Ring LED

A lightweight Zigbee-controlled RGB LED ring light controller using the **Seeed Studio XIAO ESP32-C6**. This device is configured as a **Zigbee Router (repeater)**, which strengthens and extends the range of your Zigbee mesh network while providing real-time color and dimming controls.

It supports non-volatile memory (NVRAM) state recovery (turns back to its last state on power cycle) and includes a multi-cycle power-toggle failsafe for easy overrides.

---

## Bill of Materials (BOM)

To build this project, you will need the following hardware:

*   **Microcontroller:** [Seeed Studio XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
*   **LED Ring:** [WS2812B 16-LED Ring on AliExpress](https://www.aliexpress.us/item/3256802117755006.html) (16-ring version)
*   **Antenna Cable:** [FPC-IPX1 Cable (10cm) on AliExpress](https://www.aliexpress.us/item/3256802833801038.html)
*   **Wiring:** 26AWG wire

---

## Wiring Diagram

Connect the components according to the layout below:

```text
  +-----------------------------+             +-----------------------+
  |                             |             |     WS2812B LED RING  |
  | SEEED STUDIO XIAO ESP32-C6  |             |       (16 Pixels)     |
  |                             |             |                       |
  |       [5V]  (VBUS Output)  [#]------------> [5V / VCC]            |
  |       [GND] (Ground)       [#]------------> [GND]                 |
  |       [D2]  (GPIO 2)       [#]------------> [DIN] (Data Input)    |
  +-----------------------------+             +-----------------------+
```

> [!IMPORTANT]
> The **5V** pin on the XIAO ESP32-C6 provides power directly from the USB-C connector. This will draw up to ~1A at full brightness. Make sure your power supply provides sufficient current.

> [!IMPORTANT]
> **External Antenna Required:**
> You **must** connect the external antenna to the U.FL connector on the Seeed Studio XIAO ESP32-C6 for the Zigbee transceiver to work properly. Without the antenna connected, the device will have extremely poor signal range and will likely fail to pair or communicate with your Zigbee coordinator.
> 
> *Note on Internal Antenna:* By default, the firmware selects the external antenna via the board's RF switch pins. If you do not want to use an external antenna and prefer to use the built-in PCB antenna, you must change the following pin setting in `setup()`:
> ```cpp
> digitalWrite(WIFI_ANT_CONFIG_PIN, LOW); // Selects internal PCB antenna
> ```

---

## Compilation & IDE Configuration

To compile and upload the sketch, you must use the **Arduino IDE** or **Arduino CLI** with the `esp32` board manager package installed (version 3.x+ is required for the native Zigbee stack).

### Required Libraries
This project depends on the **Adafruit NeoPixel** library:
*   **Via Arduino IDE:** Go to **Sketch > Include Library > Manage Libraries...**, search for **Adafruit NeoPixel**, and click **Install**.
*   **Via Arduino CLI:**
    ```bash
    arduino-cli lib install "Adafruit NeoPixel"
    ```

### Required IDE Settings (Tools Menu)
*   **Board:** `XIAO_ESP32C6`
*   **Zigbee Mode:** `Zigbee ZCZR (coordinator/router)`
*   **Partition Scheme:** `Zigbee ZCZR 4MB with spiffs`
*   **USB CDC On Boot:** `Enabled`

### Compile & Flash via Arduino CLI
Run the following commands to compile and upload using `arduino-cli`:

```bash
# Compile the sketch
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc,PartitionScheme=zigbee_zczr,ZigbeeMode=zczr simple-zigbee-ring-led.ino

# Upload the sketch (replace <PORT> with your serial port, e.g. /dev/cu.usbmodem1101)
arduino-cli upload -p <PORT> --fqbn esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc,PartitionScheme=zigbee_zczr,ZigbeeMode=zczr simple-zigbee-ring-led.ino
```

---

## Firmware Configuration

The top of the main sketch file `simple-zigbee-ring-led.ino` contains a `USER CONFIGURATION` block with options to adapt the firmware for your hardware setup:

| Configuration Macro | Allowed Values | Description |
|---|---|---|
| `ACTIVE_LED_RING_TYPE` | `LED_RING_TYPE_RGB` (0) <br> `LED_RING_TYPE_RGBW` (1) | Selects the LED type: Standard RGB (WS2812B, using `NEO_GRB`) or RGB + Warm White (SK6812, using `NEO_GRBW` with dynamic white channel extraction). |
| `NUMPIXELS` | Integer (e.g. `16`) | The number of addressable LEDs on your ring light. |
| `LED_PIN` | Pin Label (e.g. `D2`) | The data pin connected to the DI (Data Input) of the LED ring. |
| `BOOT_PIN` | Pin Number (e.g. `9`) | The GPIO pin of the physical Boot button on the XIAO ESP32-C6 (used for factory reset). |
| `ZIGBEE_RGB_LIGHT_ENDPOINT` | Integer (e.g. `10`) | Logical endpoint number for the Zigbee Color Dimmable Light. |
| `RUN_SELF_TESTS` | Uncommented / Commented | Uncomment `#define RUN_SELF_TESTS` to run boot-time logic scaling, NVRAM, and RGBW math checks. |

---

## Pairing Instructions

### Initial Pairing
1. Set your Zigbee hub/coordinator (e.g., Zigbee2MQTT, Hubitat, Home Assistant ZHA) into **Permit Join / Pairing Mode**.
2. Power on the device by plugging in the USB-C cable.
3. The device will automatically enter pairing mode for **120 seconds** on boot if it is not currently paired.
4. The hub should discover the device as **Simple Zigbee Ring LED** manufactured by **Josh Andrews**.

### Re-pairing / Factory Reset
If you need to pair the device to a new coordinator or reset the connection:
1. Put your Zigbee hub into **Pairing Mode**.
2. While the device is powered on, press and hold the physical **BOOT button** (GPIO 9) on the Seeed Studio board for at least **3 seconds**.
3. Release the button. The board will wipe its stored pairing database, reboot, and enter the 120-second pairing window automatically.

---

## Operation & Features

### 1. Zigbee Router (Repeater) Capability
Because this device is mains-powered (via USB-C), it functions as a **Zigbee Router**. It will automatically route Zigbee packets for nearby devices, strengthening your home automation mesh network.

### 2. State Retention & Restore
Upon normal boot, the microcontroller loads its last known power state (ON/OFF), color, and brightness from non-volatile memory (NVRAM) and restores it instantly. It also synchronizes this state back to the Zigbee network coordinator.

### 3. Failsafe Override Mode
If you toggle the power switch **three times** rapidly (with less than 3 seconds of uptime on each boot), the device will trigger a failsafe bypass:
*   Bypasses NVRAM state load.
*   Forces the LEDs to **100% white ON**.
*   Resets the boot counter.
This acts as a hardware bypass if the device becomes unresponsive or loses its Zigbee connection and you need light immediately.

### 4. Factory Reset
To reset the device to factory default settings and clear its Zigbee pairing data:
1.  Press and hold the physical **BOOT button** (GPIO 9) on the XIAO ESP32-C6.
2.  Keep it pressed for at least **3 seconds**.
3.  The board serial console will print a reset message, clear the pairing data, and reboot the device in pairing mode.
