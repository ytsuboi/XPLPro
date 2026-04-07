# XPLPro Arduino Library API Reference

XPLPro is an Arduino library that connects Arduino hardware to X-Plane flight simulator via a serial (USB) link. The Arduino communicates with an X-Plane plugin (`XPLPro_Plugin`) that acts as a bridge to X-Plane's SDK. This allows you to build physical cockpit panels with real switches, LEDs, encoders, servos, and displays that interact with the simulator in real time.

## Architecture Overview

The communication follows a **callback-driven, event-based** model:

1. Arduino connects to the PC via USB serial at 115200 baud
2. The X-Plane plugin detects the Arduino and requests its device name
3. The plugin signals that it is ready for DataRef/Command registrations
4. Arduino registers the DataRefs and Commands it needs
5. The plugin pushes DataRef value updates to Arduino whenever values change
6. Arduino can write DataRef values and trigger Commands back to X-Plane

All incoming data from X-Plane is delivered through a single callback function. There is no blocking "read" call — the library is entirely non-blocking and event-driven.

---

## 1. Initialization and Main Loop

### `XPLPro(Stream *device)`

Constructor. Creates a library instance bound to a serial port. Almost always used with `&Serial`.

```cpp
#include <XPLPro.h>
XPLPro XP(&Serial);
```

### `void begin(deviceName, initFunc, stopFunc, inboundFunc)`

Initialize the library and register three callback functions. This must be called in `setup()` after `Serial.begin()`.

| Parameter | Type | Description |
|---|---|---|
| `deviceName` | `const char*` | A human-readable name for your device. This appears in the plugin's status window inside X-Plane, so choose something descriptive (e.g., "Radio Panel", "Overhead Switches"). |
| `initFunc` | `void(*)()` | Registration callback — called when X-Plane and the plugin are ready to accept DataRef and Command registrations. This is where you call `registerDataRef()`, `registerCommand()`, and `requestUpdates()`. This callback may be called **multiple times** during a session (e.g., when the user loads a different aircraft or restarts X-Plane). |
| `stopFunc` | `void(*)()` | Shutdown callback — called when X-Plane shuts down, unloads an aircraft, or the connection is lost. Use this to reset hardware state (turn off LEDs, zero out servos, etc.). |
| `inboundFunc` | `void(*)(inStruct*)` | Data callback — called every time a subscribed DataRef value changes and the plugin sends an update. This is the only way to receive data from X-Plane. |

```cpp
void setup() {
    pinMode(PIN_LED, OUTPUT);
    Serial.begin(XPL_BAUDRATE);    // XPL_BAUDRATE is 115200, defined by the library
    XP.begin("My Cockpit Panel", &xplRegister, &xplShutdown, &xplInboundHandler);
    digitalWrite(PIN_LED, LOW);    // start with LED off
}
```

**Important:** The baud rate must be `XPL_BAUDRATE` (115200). Do not change this — it must match the plugin.

### `int xloop()`

The main processing function. **Must be called on every iteration of `loop()`.** This function:
- Reads incoming serial data from the plugin
- Parses received packets
- Fires the appropriate callbacks (`initFunc`, `stopFunc`, or `inboundFunc`)

Returns the current connection status: `1` if connected, `0` if not.

```cpp
void loop() {
    XP.xloop();

    // your own periodic logic can go here, but keep it fast
    // avoid delay() — it blocks serial processing
}
```

**Tip:** Never use `delay()` in your main loop. It blocks serial reception and can cause missed packets. Use `millis()`-based timing instead:

```cpp
unsigned long lastCheck = 0;

void loop() {
    XP.xloop();

    if (millis() - lastCheck > 100) {    // every 100ms
        lastCheck = millis();
        // read sensors, update displays, etc.
    }
}
```

### `int connectionStatus()`

Returns `true` (non-zero) if the Arduino is currently connected to the X-Plane plugin, `false` (0) otherwise. This is the same value returned by `xloop()`.

```cpp
void loop() {
    XP.xloop();

    if (XP.connectionStatus()) {
        // connected — normal operation
    } else {
        // not connected — maybe blink an LED to indicate waiting
    }
}
```

---

## 2. DataRef Registration and Subscription

X-Plane exposes thousands of internal variables called **DataRefs** (e.g., airspeed, altitude, switch positions, light states). To use a DataRef, you must first register it to obtain a handle, then subscribe to receive updates.

### `int registerDataRef(name)` -> `dref_handle`

Register an X-Plane DataRef by its full name and obtain a numeric handle for all subsequent operations. The handle is a small integer assigned by the plugin.

**Constraints:**
- Must be called **only inside the `initFunc` callback** (the registration callback passed to `begin()`). Calling it elsewhere returns `XPL_HANDLE_INVALID` (-1).
- The DataRef name must match an existing X-Plane DataRef exactly. If not found, returns `-1`.
- On AVR boards (Arduino Mega, Uno, etc.), wrap strings in the `F()` macro to store them in flash memory and save RAM.

```cpp
dref_handle hBeacon;
dref_handle hNavLights;
dref_handle hAltitude;

void xplRegister() {
    // Read-only datarefs (we will subscribe to updates)
    hBeacon    = XP.registerDataRef(F("sim/cockpit2/switches/beacon_on"));
    hAltitude  = XP.registerDataRef(F("sim/cockpit2/gauges/indicators/altitude_ft_pilot"));

    // Writable dataref (we will write values from Arduino hardware)
    hNavLights = XP.registerDataRef(F("sim/cockpit2/switches/navigation_lights_on"));

    // Subscribe to receive updates (see requestUpdates below)
    XP.requestUpdates(hBeacon, 100, 0);
    XP.requestUpdates(hAltitude, 200, 1.0);
}
```

**Abbreviations:** The plugin supports short aliases defined in `abbreviations.txt` to reduce memory usage. For example, instead of `"sim/cockpit2/switches/beacon_on"` you can use `"LTbcn"` if the abbreviation is configured on the plugin side:

```cpp
hBeacon = XP.registerDataRef(F("LTbcn"));       // abbreviation
hPause  = XP.registerCommand(F("CMDpause"));     // abbreviation for command
```

### `void requestUpdates(handle, rate, precision)`

Subscribe to automatic updates for a DataRef. After calling this, the plugin will send the current value of the DataRef to Arduino via the inbound callback whenever the value changes.

| Parameter | Type | Description |
|---|---|---|
| `handle` | `dref_handle` | Handle obtained from `registerDataRef()` |
| `rate` | `int` | Minimum interval between updates in milliseconds. For example, `100` means the plugin will send at most one update every 100ms, even if the value changes more frequently. This limits serial traffic. |
| `precision` | `float` | Change threshold for float DataRefs. The plugin only sends an update when the value changes by at least this amount. Use `0` to receive every change. For integer DataRefs, use `0`. |

```cpp
void xplRegister() {
    hBeacon   = XP.registerDataRef(F("sim/cockpit2/switches/beacon_on"));
    hAltitude = XP.registerDataRef(F("sim/cockpit2/gauges/indicators/altitude_ft_pilot"));
    hBrake    = XP.registerDataRef(F("sim/cockpit2/controls/parking_brake_ratio"));

    // Integer dataref: update as fast as possible, no precision filter
    XP.requestUpdates(hBeacon, 100, 0);

    // Float dataref: update every 200ms, only if changed by >= 1.0 ft
    XP.requestUpdates(hAltitude, 200, 1.0);

    // Float dataref: update every 100ms, report any change
    XP.requestUpdates(hBrake, 100, 0);
}
```

**Note:** If you register a DataRef but never call `requestUpdates()`, you can still write to it — you just won't receive any value updates from X-Plane. This is useful for write-only DataRefs (e.g., setting a switch position from a physical toggle).

### `void requestUpdates(handle, rate, precision, arrayElement)`

Subscribe to a specific element of an **array-type** DataRef. Many X-Plane DataRefs are arrays — for example, engine parameters are arrays indexed by engine number.

```cpp
dref_handle hEngineRPM;

void xplRegister() {
    hEngineRPM = XP.registerDataRef(F("sim/cockpit2/engine/indicators/prop_speed_rpm"));

    // Subscribe to engine 0 (left engine) and engine 1 (right engine) separately
    XP.requestUpdates(hEngineRPM, 100, 10.0, 0);    // element 0 = left engine
    XP.requestUpdates(hEngineRPM, 100, 10.0, 1);    // element 1 = right engine
}

void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hEngineRPM) {
        if (inData->element == 0) {
            // left engine RPM changed
            leftRPM = inData->inFloat;
        }
        if (inData->element == 1) {
            // right engine RPM changed
            rightRPM = inData->inFloat;
        }
    }
}
```

### `void requestUpdatesType(handle, type, rate, precision)`

Subscribe with an explicitly specified data type. Some third-party aircraft DataRefs (notably Zibo 737) report multiple data types for the same DataRef. Use this to force which type you want to receive.

Available types (from X-Plane SDK):

| Constant | Value | Description |
|---|---|---|
| `xplmType_Int` | 1 | Single 4-byte integer |
| `xplmType_Float` | 2 | Single 4-byte float |
| `xplmType_Double` | 4 | Single 8-byte double |
| `xplmType_FloatArray` | 8 | Array of floats |
| `xplmType_IntArray` | 16 | Array of integers |
| `xplmType_Data` | 32 | Variable-length binary/string data |

```cpp
// Force receiving as integer even if the dataref also supports float
XP.requestUpdatesType(hSwitch, xplmType_Int, 100, 0);
```

### `void requestUpdatesType(handle, type, rate, precision, arrayElement)`

Same as above, but for a specific element of an array-type DataRef.

---

## 3. DataRef Write

### `void datarefWrite(handle, value)`

Write a value to a DataRef in X-Plane. The function is overloaded to accept `int`, `long`, and `float` types. The compiler selects the correct overload based on the argument type.

```cpp
// Write an integer value (e.g., toggle a switch on/off)
XP.datarefWrite(hNavLights, 1);           // turn nav lights on
XP.datarefWrite(hNavLights, 0);           // turn nav lights off

// Write a float value (e.g., set throttle position)
XP.datarefWrite(hThrottle, 0.75f);        // 75% throttle

// Write a long value
XP.datarefWrite(hTransponder, 1200L);     // squawk 1200
```

A practical example — reading a physical toggle switch and writing the state to X-Plane:

```cpp
#define PIN_NAV_SWITCH 22

int lastNavState = -1;

void loop() {
    XP.xloop();

    if (millis() - lastCheck > 100) {
        lastCheck = millis();
        int navState = digitalRead(PIN_NAV_SWITCH);
        if (navState != lastNavState) {          // only write when changed
            XP.datarefWrite(hNavLights, navState);
            lastNavState = navState;
        }
    }
}
```

### `void datarefWrite(handle, value, arrayElement)`

Write a value to a specific element of an array-type DataRef. Overloaded for `int`, `long`, and `float`.

```cpp
dref_handle hThrottle;

void xplRegister() {
    hThrottle = XP.registerDataRef(F("sim/cockpit2/engine/actuators/throttle_ratio"));
}

// Set left engine (element 0) to 80%, right engine (element 1) to 60%
XP.datarefWrite(hThrottle, 0.80f, 0);
XP.datarefWrite(hThrottle, 0.60f, 1);
```

### `void datarefTouch(handle)`

Force the plugin to immediately resend the current value of the specified DataRef. The value arrives through the normal inbound callback — this does not return a value directly.

This is marked as an **experimental** feature. In most cases, `requestUpdates()` provides all the updates you need. Use `datarefTouch()` only if you need a one-time refresh outside the normal update cycle.

```cpp
// Force a refresh of the altitude value
XP.datarefTouch(hAltitude);
// The updated value will arrive later via xplInboundHandler()
```

---

## 4. Reading DataRef Values

There is no explicit "read" or "get" function. The library uses a **push (subscription) model** by design, because serial communication has limited bandwidth and polling would be inefficient.

The pattern for reading values is:

1. Register the DataRef with `registerDataRef()`
2. Subscribe to changes with `requestUpdates()`
3. Store incoming values in global variables inside the `inboundHandler` callback
4. Use those global variables anywhere else in your sketch

```cpp
dref_handle hBeacon;
dref_handle hAltitude;
dref_handle hBrake;

long beaconState = 0;
float altitude = 0.0;
float brakeRatio = 0.0;

void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hBeacon) {
        beaconState = inData->inLong;
        digitalWrite(PIN_BEACON_LED, beaconState);
    }
    if (inData->handle == hAltitude) {
        altitude = inData->inFloat;
        updateAltitudeDisplay(altitude);
    }
    if (inData->handle == hBrake) {
        brakeRatio = inData->inFloat;
        // LED on when brake is more than 20% engaged
        digitalWrite(PIN_BRAKE_LED, brakeRatio > 0.2 ? HIGH : LOW);
    }
}
```

### Why no read function?

A previous version of the library had a one-shot read request (`XPLREQUEST_DATAREFVALUE`), but it was removed because `requestUpdates()` is more efficient. With subscriptions, the plugin only sends data when values actually change, reducing serial traffic. A polling approach would either waste bandwidth (frequent polls) or miss changes (infrequent polls).

---

## 5. Command Operations

X-Plane **Commands** represent actions (as opposed to DataRefs which represent state). Commands include things like "toggle pause", "retract gear", "increase heading bug", etc.

### `int registerCommand(name)` -> `cmd_handle`

Register an X-Plane command by its full name and obtain a handle. Like `registerDataRef()`, this must be called **only inside the `initFunc` callback**.

```cpp
cmd_handle hGearToggle;
cmd_handle hHdgUp;
cmd_handle hHdgDn;
cmd_handle hPause;

void xplRegister() {
    hGearToggle = XP.registerCommand(F("sim/flight_controls/landing_gear_toggle"));
    hHdgUp      = XP.registerCommand(F("sim/autopilot/heading_up"));
    hHdgDn      = XP.registerCommand(F("sim/autopilot/heading_down"));
    hPause      = XP.registerCommand(F("sim/operation/pause_toggle"));
}
```

Returns `-1` if the command was not found.

### `int commandTrigger(handle)` / `int commandTrigger(handle, count)`

Trigger a command once (or `count` times). This is equivalent to a momentary button press and immediate release. Use this for toggle-type or increment/decrement actions.

```cpp
// Toggle landing gear (single trigger)
XP.commandTrigger(hGearToggle);

// Increase heading by 5 degrees (trigger 5 times)
XP.commandTrigger(hHdgUp, 5);
```

A practical example with a rotary encoder for heading adjustment:

```cpp
#include <Encoder.h>
Encoder hdgEncoder(2, 3);

long lastEncoderPos = 0;

void loop() {
    XP.xloop();

    long pos = hdgEncoder.read() / 4;    // 4 pulses per detent
    if (pos > lastEncoderPos) {
        XP.commandTrigger(hHdgUp, pos - lastEncoderPos);
        lastEncoderPos = pos;
    } else if (pos < lastEncoderPos) {
        XP.commandTrigger(hHdgDn, lastEncoderPos - pos);
        lastEncoderPos = pos;
    }
}
```

### `int commandStart(handle)` / `int commandEnd(handle)`

Start and end a command separately. This simulates holding down a button. **Every `commandStart()` must be paired with a `commandEnd()`** — failing to do so leaves the command in a "held" state indefinitely.

Use this for actions that behave differently when held (e.g., brakes, push-to-talk, view controls).

```cpp
#define PIN_BRAKES 24

int lastBrakeBtn = HIGH;

void loop() {
    XP.xloop();

    int brakeBtn = digitalRead(PIN_BRAKES);
    if (brakeBtn != lastBrakeBtn) {
        if (brakeBtn == LOW) {            // button pressed (active low)
            XP.commandStart(hBrakes);
        } else {                          // button released
            XP.commandEnd(hBrakes);
        }
        lastBrakeBtn = brakeBtn;
    }
}
```

---

## 6. Special Commands

These functions simulate keyboard and joystick inputs at the X-Plane level. They are wrappers around X-Plane SDK functions that X-Plane itself considers deprecated, so use them sparingly — prefer Commands (section 5) whenever a suitable command exists.

### `void commandKeyStroke(inKey)`

Simulate an X-Plane keystroke command. The `inKey` parameter is an `XPLMCommandKeyID` value defined by the X-Plane SDK.

### `void commandButtonPress(inButton)` / `void commandButtonRelease(inButton)`

Simulate a joystick button press and release. The `inButton` parameter is an `XPLMCommandButtonID` value. **Must always be called in pairs** — every press needs a matching release.

### `void simulateKeyPress(inKeyType, inKey)`

Simulate a raw key press event with a key type and key code.

---

## 7. Debug and Speech

### `int sendDebugMessage(msg)`

Send a text message to the X-Plane plugin log. The message appears in the plugin's log file and/or status window. Useful for debugging your Arduino code without needing a separate serial monitor (since the serial port is occupied by XPLPro communication).

```cpp
XP.sendDebugMessage("Switch A toggled ON");
XP.sendDebugMessage("Encoder position: overflow detected");
```

### `int sendSpeakMessage(msg)`

Have X-Plane's built-in text-to-speech engine speak a message aloud through the PC speakers. This can be useful for audio feedback from your hardware panel.

```cpp
XP.sendSpeakMessage("Gear down and locked");
XP.sendSpeakMessage("Parking brake set");
```

Practical example — announce gear state changes:

```cpp
void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hGearDeploy) {
        if (inData->inFloat >= 1.0) {
            XP.sendSpeakMessage("Gear down");
        } else if (inData->inFloat <= 0.0) {
            XP.sendSpeakMessage("Gear up");
        }
    }
}
```

---

## 8. Flow Control

These functions control the rate at which the plugin sends data to Arduino. Useful when your sketch needs time to process large amounts of incoming data (e.g., updating a complex display).

### `void dataFlowPause()` / `void dataFlowResume()`

Temporarily pause and resume all DataRef update transmissions from the plugin. While paused, the plugin queues changes internally and sends them when resumed.

```cpp
XP.dataFlowPause();
// ... do some time-consuming processing ...
XP.dataFlowResume();
```

### `void setDataFlowSpeed(bytesPerSecond)`

Throttle the plugin's data transmission rate. The plugin ensures that it does not exceed the specified number of bytes per second. A complete packet is always sent atomically — throttling occurs between packets, not mid-packet.

```cpp
// Limit to 1000 bytes/second (useful for slower boards or complex sketches)
XP.setDataFlowSpeed(1000);
```

### `void sendResetRequest()`

Request the plugin to reset the connection and re-trigger the full registration cycle. After calling this, the plugin will call your `initFunc` callback again, allowing all DataRefs and Commands to be re-registered.

```cpp
// Force a full re-registration (e.g., after detecting a problem)
XP.sendResetRequest();
```

### `int getBufferStatus()`

Returns the number of bytes currently waiting in the serial receive buffer. Useful for monitoring buffer pressure and deciding whether to pause data flow.

```cpp
if (XP.getBufferStatus() > 150) {
    XP.dataFlowPause();
    // process backlog
    XP.dataFlowResume();
}
```

---

## 9. Scaling

### `void setScaling(handle, inLow, inHigh, outLow, outHigh)`

Configure the plugin to apply linear scaling (mapping) to a DataRef's values before sending them to Arduino. This offloads the `map()` calculation from the Arduino to the PC, which is especially useful for driving servos or analog gauges.

The plugin maps the DataRef value from the range `[inLow, inHigh]` to `[outLow, outHigh]` using linear interpolation.

| Parameter | Description |
|---|---|
| `handle` | Handle of the DataRef |
| `inLow` | Lower bound of the DataRef's expected range |
| `inHigh` | Upper bound of the DataRef's expected range |
| `outLow` | Lower bound of the desired output range |
| `outHigh` | Upper bound of the desired output range |

**Note:** Scaling currently applies only to **outbound** (X-Plane to Arduino) data.

Example — driving a servo gauge for engine RPM:

```cpp
#include <Servo.h>
Servo rpmGauge;

dref_handle hRPM;

void xplRegister() {
    hRPM = XP.registerDataRef(F("sim/cockpit2/engine/indicators/prop_speed_rpm"));
    XP.requestUpdates(hRPM, 100, 10.0, 0);   // engine 0, update every 100ms

    // Map RPM 0-3500 to servo angle 180-0 (reversed because gauge needle moves clockwise)
    XP.setScaling(hRPM, 0, 3500, 180, 0);
}

void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hRPM) {
        // inData->inLong is already scaled to 0-180 by the plugin
        rpmGauge.write(inData->inLong);
    }
}
```

Without scaling, you would need to do this on the Arduino side:
```cpp
int angle = map(inData->inFloat, 0, 3500, 180, 0);
rpmGauge.write(angle);
```

Using `setScaling()` saves CPU cycles on the Arduino and reduces the data size sent over serial (integers are smaller than floats in the wire format).

---

## 10. Inbound Data Structure (`inStruct`)

The `inboundHandler` callback receives a pointer to an `inStruct` containing information about the incoming DataRef update:

| Field | Type | Description |
|---|---|---|
| `handle` | `int` | Handle identifying which DataRef sent the update. Compare this against your stored handles to determine which DataRef changed. |
| `type` | `int` | Data type of the update. One of: `xplmType_Int` (1), `xplmType_Float` (2), `xplmType_IntArray` (16), `xplmType_FloatArray` (8), `xplmType_Data` (32). |
| `inLong` | `long` | The integer value. Populated for `xplmType_Int` and `xplmType_IntArray` types. |
| `inFloat` | `float` | The float value. Populated for `xplmType_Float` and `xplmType_FloatArray` types. |
| `element` | `int` | The array element index. Populated for array types (`xplmType_IntArray`, `xplmType_FloatArray`). For non-array DataRefs, this is 0. |
| `strLength` | `int` | Length of the string/binary data. Populated for `xplmType_Data` type. |
| `inStr` | `char*` | Pointer to the string/binary data buffer. Populated for `xplmType_Data` type. Only valid during the callback — copy the data if you need it later. |

### Handling different data types

```cpp
void xplInboundHandler(inStruct *inData) {
    // Integer dataref (e.g., switch on/off)
    if (inData->handle == hBeacon) {
        digitalWrite(PIN_BEACON_LED, inData->inLong);
    }

    // Float dataref (e.g., brake ratio 0.0 - 1.0)
    if (inData->handle == hBrake) {
        if (inData->inFloat > 0.2) {
            digitalWrite(PIN_BRAKE_LED, HIGH);
        } else {
            digitalWrite(PIN_BRAKE_LED, LOW);
        }
    }

    // Float dataref with multi-state thresholds (e.g., flap deploy ratio)
    if (inData->handle == hFlaps) {
        if (inData->inFloat <= 0.0) {
            setLED(LED_GREEN);     // flaps up
        } else if (inData->inFloat >= 1.0) {
            setLED(LED_RED);       // flaps fully down
        } else {
            setLED(LED_AMBER);     // flaps in transit
        }
    }

    // Array dataref (e.g., per-engine parameters)
    if (inData->handle == hEngineRPM) {
        rpmValues[inData->element] = inData->inFloat;
        updateGauge(inData->element, inData->inFloat);
    }

    // String dataref (e.g., aircraft tail number)
    if (inData->handle == hTailNumber) {
        // inData->inStr contains the string, inData->strLength is its length
        lcd.setCursor(0, 0);
        lcd.print(inData->inStr);
    }
}
```

### Handling multiple DataRefs efficiently

When you have many DataRefs, a chain of `if` statements works fine. Each callback invocation delivers exactly one DataRef update, so only one `if` branch will match.

```cpp
void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hBeacon)     { digitalWrite(PIN_BCN, inData->inLong);  return; }
    if (inData->handle == hNavLights)  { digitalWrite(PIN_NAV, inData->inLong);  return; }
    if (inData->handle == hTaxiLight)  { digitalWrite(PIN_TAXI, inData->inLong); return; }
    if (inData->handle == hLandLight)  { digitalWrite(PIN_LAND, inData->inLong); return; }
    if (inData->handle == hStrobes)    { digitalWrite(PIN_STRB, inData->inLong); return; }
}
```

---

## 11. Compile-Time Configuration

These constants can be overridden by defining them **before** including `XPLPro.h`, either with `#define` in your sketch or via compiler flags:

| Define | Default | Description |
|---|---|---|
| `XPL_FLOATPRECISION` | 4 | Number of decimal places transmitted for float DataRefs. More decimals means more serial traffic. Reduce to 2 if you don't need high precision. |
| `XPL_RESPONSE_TIMEOUT` | 90000 | Timeout in milliseconds when waiting for a registration response. The default is 90 seconds because X-Plane can take a long time to finish loading after signaling readiness. |
| `XPL_USE_PROGMEM` | 1 (AVR), 0 (others) | Enable the `F()` macro for storing DataRef name strings in flash memory. Enabled by default on AVR boards (Mega, Uno). Disable if you get compilation errors with `strncmp_PF`. |
| `XPLMAX_PACKETSIZE_TRANSMIT` | 200 | Send buffer size in bytes. Must be large enough for the longest DataRef name + 10 bytes of overhead. Should be less than 256. |
| `XPLMAX_PACKETSIZE_RECEIVE` | 200 | Receive buffer size in bytes. Same sizing considerations as the transmit buffer. Also needs to be large enough for string DataRef values you receive. |

```cpp
// Override before including the library
#define XPL_FLOATPRECISION 2
#define XPLMAX_PACKETSIZE_TRANSMIT 128
#define XPLMAX_PACKETSIZE_RECEIVE 128
#include <XPLPro.h>
```

---

## Complete Example: LED + Switch + Encoder

This example combines the most common use cases — reading a DataRef to control an LED, writing a DataRef from a physical switch, and triggering commands from a rotary encoder.

```cpp
#include <XPLPro.h>
#include <Encoder.h>

XPLPro XP(&Serial);
Encoder hdgEncoder(2, 3);

#define PIN_BEACON_LED   10
#define PIN_NAV_SWITCH   22

dref_handle hBeacon;
dref_handle hNavLights;
cmd_handle  hHdgUp;
cmd_handle  hHdgDn;

int lastNavState = -1;
long lastEncoderPos = 0;
unsigned long lastCheck = 0;

void setup() {
    pinMode(PIN_BEACON_LED, OUTPUT);
    pinMode(PIN_NAV_SWITCH, INPUT_PULLUP);
    Serial.begin(XPL_BAUDRATE);
    XP.begin("LED+Switch+Encoder Demo", &xplRegister, &xplShutdown, &xplInboundHandler);
}

void loop() {
    XP.xloop();

    // --- Rotary encoder: heading bug adjustment ---
    long pos = hdgEncoder.read() / 4;
    if (pos > lastEncoderPos) {
        XP.commandTrigger(hHdgUp, pos - lastEncoderPos);
        lastEncoderPos = pos;
    } else if (pos < lastEncoderPos) {
        XP.commandTrigger(hHdgDn, lastEncoderPos - pos);
        lastEncoderPos = pos;
    }

    // --- Toggle switch: nav lights (debounced with millis) ---
    if (millis() - lastCheck > 100) {
        lastCheck = millis();
        int navState = digitalRead(PIN_NAV_SWITCH);
        if (navState != lastNavState) {
            XP.datarefWrite(hNavLights, navState == LOW ? 1 : 0);
            lastNavState = navState;
        }
    }
}

void xplRegister() {
    hBeacon    = XP.registerDataRef(F("sim/cockpit2/switches/beacon_on"));
    hNavLights = XP.registerDataRef(F("sim/cockpit2/switches/navigation_lights_on"));
    hHdgUp     = XP.registerCommand(F("sim/autopilot/heading_up"));
    hHdgDn     = XP.registerCommand(F("sim/autopilot/heading_down"));

    XP.requestUpdates(hBeacon, 100, 0);
}

void xplShutdown() {
    digitalWrite(PIN_BEACON_LED, LOW);
}

void xplInboundHandler(inStruct *inData) {
    if (inData->handle == hBeacon) {
        digitalWrite(PIN_BEACON_LED, inData->inLong);
    }
}
```

---

## Lifecycle Summary

```
Power on / Reset
    │
    ▼
setup()
    ├── Serial.begin(XPL_BAUDRATE)
    └── XP.begin("name", &init, &stop, &handler)
    │
    ▼
loop()  ──►  XP.xloop()  ◄── repeats forever
                │
                ├── Plugin detected ──► initFunc() called
                │       ├── registerDataRef()
                │       ├── registerCommand()
                │       ├── requestUpdates()
                │       └── setScaling()
                │
                ├── DataRef changed ──► inboundFunc(inData) called
                │       └── read inData->inLong / inFloat / inStr
                │
                └── X-Plane exit ──► stopFunc() called
                        └── reset hardware state
```

The `initFunc` may be called multiple times during a session (aircraft change, X-Plane restart), so your registration code must be safe to run repeatedly.
