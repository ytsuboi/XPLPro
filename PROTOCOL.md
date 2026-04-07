# XPLPro Communication Protocol

This document describes the serial communication protocol between the
XPLPro X-Plane plugin and the XPLPro Arduino library.

## Physical Layer

| Parameter | Value |
|-----------|-------|
| Interface | USB Serial (virtual COM port) |
| Baud rate | 115200 bps |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None (software flow control via `dataFlowPause` / `dataFlowResume`) |

## Packet Format

All messages use a simple ASCII text format enclosed in bracket delimiters:

```
[<command_char>,<param1>,<param2>,...,<paramN>]
```

- **Header:** `[` (0x5B)
- **Trailer:** `]` (0x5D)
- **Separator:** `,` (comma) between command character and parameters
- **String values:** enclosed in double quotes (`"..."`)
- **Numeric values:** plain ASCII decimal (e.g., `42`, `-1`, `3.1416`)
- **Maximum packet size:** 200 bytes (configurable via `XPLMAX_PACKETSIZE_TRANSMIT` / `XPLMAX_PACKETSIZE_RECEIVE`)

Examples of valid packets:

```
[N]                              command only, no parameters
[n,"My Panel"]                   command with a string parameter
[1,3,1]                          command with two integer parameters
[2,1,25.5432]                    command with int and float parameters
[4,1,123.4567,5]                 command with int, float, and int parameters
```

### Known Hardware Workaround

Some Arduino boards have a bug when transmitting exactly 64 bytes over USB
serial. The library detects this case and appends an extra space character
after the packet to avoid data loss. This is handled automatically and is
invisible to application code.

---

## Connection Lifecycle

### 1. Device Discovery

The plugin scans serial ports on startup (COM1–COM256 on Windows,
`/dev/tty.usbmodem*` and `/dev/tty.usbserial*` on macOS). For each port,
the plugin opens a connection and sends a name request. If a valid response
arrives within 3 seconds, the device is registered.

```
Plugin                              Arduino
  │                                    │
  │──── open serial port ────────────►│
  │                                    │
  │──── [N] ──────────────────────────►│  "What is your name?"
  │                                    │
  │◄──── [v,"Apr  7 2026 10:30:00"] ──│  Build date/time
  │◄──── [n,"Radio Panel"] ───────────│  Device name
  │                                    │
  │  Device registered successfully    │
```

The Arduino responds with its build version first, then its device name.
Upon receiving the name, the plugin marks the device as connected.

### 2. Registration Phase

When X-Plane has finished loading (or when a new aircraft is loaded), the
plugin tells each device that it is ready to accept registrations:

```
Plugin                              Arduino
  │                                    │
  │──── [Q] ──────────────────────────►│  "Ready for registrations"
  │                                    │
  │  Arduino calls initFunc()          │
  │                                    │
  │◄──── [b,"sim/cockpit2/..."] ──────│  Register DataRef
  │──── [D,0] ────────────────────────►│  Handle = 0
  │                                    │
  │◄──── [b,"sim/flightmodel/..."] ───│  Register another DataRef
  │──── [D,1] ────────────────────────►│  Handle = 1
  │                                    │
  │◄──── [m,"sim/autopilot/..."] ─────│  Register Command
  │──── [C,0] ────────────────────────►│  Handle = 0
  │                                    │
  │◄──── [r,0,100,0] ────────────────-│  Subscribe to DataRef 0
  │◄──── [r,1,200,1.0] ──────────────-│  Subscribe to DataRef 1
  │                                    │
  │  Registration complete             │
```

Each registration is synchronous — the Arduino sends a request and blocks
(up to 90 seconds) until the plugin responds with a handle. The handle is
a small integer (0, 1, 2, ...) that identifies the DataRef or Command in
all subsequent communication.

If the DataRef or Command name is not found in X-Plane, the plugin
responds with handle `-1`.

### 3. Normal Operation

During flight, the plugin checks registered DataRefs every ~50ms (one
flight loop cycle). Changed values are pushed to the Arduino:

```
Plugin                              Arduino
  │                                    │
  │──── [1,0,1] ─────────────────────►│  DataRef 0 changed to 1 (int)
  │                                    │  → inboundHandler called
  │                                    │
  │──── [2,1,5280.25] ───────────────►│  DataRef 1 changed to 5280.25 (float)
  │                                    │  → inboundHandler called
  │                                    │
  │◄──── [k,0,1] ────────────────────-│  Trigger Command 0 once
  │                                    │
  │◄──── [1,0,0] ────────────────────-│  Write 0 to DataRef 0
  │                                    │
```

### 4. Shutdown

When X-Plane exits or unloads an aircraft, the plugin notifies all
connected devices:

```
Plugin                              Arduino
  │                                    │
  │──── [X] ──────────────────────────►│  "X-Plane is exiting"
  │                                    │  → stopFunc() called
  │                                    │  → connectionStatus = false
```

The Arduino's `stopFunc()` callback fires, allowing it to reset hardware
(turn off LEDs, zero servos, etc.). Note that this notification may not
arrive if X-Plane crashes.

When a new aircraft is loaded, the cycle returns to step 2 (Registration
Phase) — the plugin sends `[Q]` again, and the Arduino re-registers all
DataRefs and Commands.

---

## Packet Reference

### Direction Notation

| Symbol | Meaning |
|--------|---------|
| Plugin → Arduino | Sent by the X-Plane plugin to the Arduino |
| Arduino → Plugin | Sent by the Arduino to the X-Plane plugin |
| Bidirectional | Same packet format used in both directions |

---

### Handshake Packets

#### `N` — Request Device Name

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[N]` |
| Response | Arduino sends `[v,...]` followed by `[n,...]` |

Sent during device discovery. The plugin probes each serial port with this
packet and waits up to 3 seconds for a response.

#### `n` — Device Name Response

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[n,"<device_name>"]` |
| Example | `[n,"Overhead Panel"]` |

The device name is set by the first argument to `XPLPro::begin()`. It
appears in the plugin's status window inside X-Plane.

#### `v` — Device Version Response

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[v,"<build_date> <build_time>"]` |
| Example | `[v,"Apr  7 2026 10:30:00"]` |

Contains the compile date and time of the Arduino sketch (from the
`__DATE__` and `__TIME__` macros). Sent immediately before the name
response.

#### `Q` — Registration Ready

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[Q]` |

Signals that the plugin is ready to accept DataRef and Command
registrations. The Arduino sets an internal flag, and on the next
`xloop()` iteration, the `initFunc()` callback is invoked.

#### `X` — X-Plane Exiting

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[X]` |

Sent when X-Plane shuts down or unloads the current aircraft. The Arduino's
`stopFunc()` callback is invoked and `connectionStatus` is set to false.

---

### DataRef Registration

#### `b` — Register DataRef

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[b,"<dataref_name>"]` |
| Example | `[b,"sim/cockpit2/switches/beacon_on"]` |
| Response | Plugin sends `[D,<handle>]` |

The Arduino blocks for up to 90 seconds waiting for the response.
Abbreviations (short aliases defined in `abbreviations.txt` on the plugin
side) can be used in place of full DataRef names to reduce memory usage.

#### `D` — DataRef Handle Response

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[D,<handle>]` |
| Example (success) | `[D,3]` |
| Example (not found) | `[D,-1]` |

Returns the numeric handle assigned to the DataRef by the plugin. A value
of `-1` indicates the DataRef name was not found in X-Plane.

---

### Command Registration

#### `m` — Register Command

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[m,"<command_name>"]` |
| Example | `[m,"sim/autopilot/heading_up"]` |
| Response | Plugin sends `[C,<handle>]` |

Same timeout behavior as DataRef registration (90 seconds).

#### `C` — Command Handle Response

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[C,<handle>]` |
| Example (success) | `[C,5]` |
| Example (not found) | `[C,-1]` |

---

### DataRef Value Updates

These packets carry DataRef values in both directions. The plugin sends
them when a subscribed DataRef changes; the Arduino sends them when writing
a value to X-Plane.

#### `1` — Integer DataRef Update

| | |
|---|---|
| Direction | Bidirectional |
| Format | `[1,<handle>,<value>]` |
| Example | `[1,0,1]` — handle 0, value 1 |
| Types | `int`, `long` |

#### `2` — Float DataRef Update

| | |
|---|---|
| Direction | Bidirectional |
| Format | `[2,<handle>,<value>]` |
| Example | `[2,1,3.1416]` — handle 1, value 3.1416 |
| Precision | 4 decimal places by default (configurable via `XPL_FLOATPRECISION`) |

#### `3` — Integer Array DataRef Update

| | |
|---|---|
| Direction | Bidirectional |
| Format | `[3,<handle>,<value>,<element>]` |
| Example | `[3,2,1,0]` — handle 2, value 1, array element 0 |

#### `4` — Float Array DataRef Update

| | |
|---|---|
| Direction | Bidirectional |
| Format | `[4,<handle>,<value>,<element>]` |
| Example | `[4,2,0.85,1]` — handle 2, value 0.85, array element 1 |

#### `9` — String DataRef Update

| | |
|---|---|
| Direction | Plugin → Arduino |
| Format | `[9,<handle>,<length>` followed by `<length>` raw bytes |
| Example | `[9,5,6N12345` — handle 5, 6 bytes, string "N12345" |

String DataRefs are handled differently from other types. The packet
header contains the handle and string length, followed by the raw string
bytes read directly from the serial stream (not comma-delimited). The
maximum string length is `XPLMAX_PACKETSIZE_RECEIVE - 5` bytes.

---

### DataRef Subscriptions

These packets tell the plugin which DataRefs the Arduino wants to receive
updates for, and how frequently.

#### `r` — Subscribe to DataRef Updates

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[r,<handle>,<rate>,<precision>]` |
| Example | `[r,0,100,0]` |

| Parameter | Description |
|-----------|-------------|
| `handle` | DataRef handle from registration |
| `rate` | Minimum interval between updates (milliseconds). The plugin will not send updates more frequently than this, even if the value changes faster. |
| `precision` | Change threshold for float DataRefs. An update is sent only when `|new - old| >= precision`. Use `0` to receive every change. Ignored for integer DataRefs. |

#### `t` — Subscribe to Array Element Updates

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[t,<handle>,<rate>,<precision>,<element>]` |
| Example | `[t,1,200,0.5,3]` — handle 1, 200ms rate, 0.5 precision, element 3 |

Same as `r` but for a specific array element. Each element must be
subscribed individually.

#### `y` — Subscribe with Explicit Type

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[y,<handle>,<type>,<rate>,<precision>]` |
| Example | `[y,0,2,100,0.01]` — handle 0, force float type, 100ms, precision 0.01 |

Forces the plugin to treat the DataRef as a specific type. Useful for
DataRefs that report multiple supported types (e.g., some third-party
aircraft).

| Type value | Constant | Description |
|------------|----------|-------------|
| 1 | `xplmType_Int` | 4-byte integer |
| 2 | `xplmType_Float` | 4-byte float |
| 4 | `xplmType_Double` | 8-byte double |
| 8 | `xplmType_FloatArray` | Array of floats |
| 16 | `xplmType_IntArray` | Array of integers |
| 32 | `xplmType_Data` | Variable-length string/binary |

#### `w` — Subscribe to Array Element with Explicit Type

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[w,<handle>,<type>,<rate>,<precision>,<element>]` |
| Example | `[w,0,8,100,0.1,2]` — handle 0, force float array, 100ms, precision 0.1, element 2 |

---

### DataRef Scaling

#### `u` — Set Scaling Parameters

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[u,<handle>,<inLow>,<inHigh>,<outLow>,<outHigh>]` |
| Example | `[u,2,0,3500,180,0]` — map RPM 0–3500 to servo angle 180–0 |

Configures the plugin to apply linear interpolation to DataRef values
before sending them to the Arduino. The mapping is:

```
output = outLow + (value - inLow) * (outHigh - outLow) / (inHigh - inLow)
```

Currently active for **outbound** (X-Plane → Arduino) data only.

---

### DataRef Touch

#### `d` — Force DataRef Update

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[d,<handle>]` |
| Example | `[d,5]` |

Forces the plugin to resend the current value of the specified DataRef,
regardless of whether it has changed. The value arrives through the normal
update mechanism (`1`, `2`, `3`, `4`, or `9` packet). This is an
experimental feature.

---

### Command Execution

#### `k` — Trigger Command

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[k,<handle>,<count>]` |
| Example | `[k,3,1]` — trigger command 3 once |
| Example | `[k,3,5]` — trigger command 3 five times |

Equivalent to pressing and immediately releasing a button. The plugin
calls `XPLMCommandOnce()` for each trigger. When `count` is greater than
1, the plugin queues the additional triggers via an internal accumulator
and executes them across subsequent flight loop cycles.

#### `i` — Start Command (Begin Hold)

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[i,<handle>]` |
| Example | `[i,5]` |

Begins a continuous command (equivalent to pressing and holding a button).
The plugin calls `XPLMCommandBegin()`. **Must be paired with a
corresponding `j` (Command End) packet.**

#### `j` — End Command (Release Hold)

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[j,<handle>]` |
| Example | `[j,5]` |

Ends a continuous command (equivalent to releasing a held button). The
plugin calls `XPLMCommandEnd()`.

---

### Special Commands

These wrap deprecated X-Plane SDK functions. Prefer regular Commands
(section above) when a suitable command exists.

#### `$` — Special Command

| | |
|---|---|
| Direction | Arduino → Plugin |
| Subcommand 1 | `[$,1,<keyType>,<key>]` — Simulate key press |
| Subcommand 2 | `[$,2,<keyID>]` — Command key stroke |
| Subcommand 3 | `[$,3,<buttonID>]` — Command button press |
| Subcommand 4 | `[$,4,<buttonID>]` — Command button release |

Button press (subcommand 3) must always be paired with button release
(subcommand 4).

---

### Debug and Speech

#### `g` — Debug Message

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[g,"<message>"]` |
| Example | `[g,"Encoder overflow detected"]` |

The plugin writes the message to its log file.

#### `s` — Speech Message

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[s,"<message>"]` |
| Example | `[s,"Gear down and locked"]` |

The plugin passes the message to X-Plane's text-to-speech engine.

---

### Flow Control

#### `p` — Pause Data Flow

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[p,<bufferBytes>]` |
| Example | `[p,128]` |

Tells the plugin to stop sending DataRef updates. The `bufferBytes`
parameter reports the Arduino's current serial buffer occupancy.

#### `q` — Resume Data Flow

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[q,<bufferBytes>]` |
| Example | `[q,12]` |

Tells the plugin to resume sending DataRef updates.

#### `f` — Set Data Flow Speed

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[f,<bytesPerSecond>]` |
| Example | `[f,1000]` — limit to 1000 bytes/second |

Throttles the plugin's transmission rate. Complete packets are always sent
atomically — throttling occurs between packets, not mid-packet.

---

### Reset

#### `z` — Request Reset

| | |
|---|---|
| Direction | Arduino → Plugin |
| Format | `[z,0]` |

Requests the plugin to reset the device connection and re-trigger the full
registration cycle. The plugin will send `[Q]` again, causing the
Arduino's `initFunc()` to be called.

---

## Quick Reference Table

| Char | Name | Direction | Parameters | Description |
|------|------|-----------|------------|-------------|
| `N` | SENDNAME | → Arduino | *(none)* | Request device name |
| `n` | NAME | ← Arduino | `"name"` | Respond with device name |
| `v` | VERSION | ← Arduino | `"date time"` | Respond with build info |
| `Q` | SENDREQUEST | → Arduino | *(none)* | Ready for registrations |
| `X` | EXITING | → Arduino | *(none)* | X-Plane shutting down |
| `b` | REGISTERDATAREF | ← Arduino | `"dataref"` | Register a DataRef |
| `D` | DATAREF | → Arduino | `handle` | DataRef handle response |
| `m` | REGISTERCOMMAND | ← Arduino | `"command"` | Register a Command |
| `C` | COMMAND | → Arduino | `handle` | Command handle response |
| `1` | DATAREFUPDATEINT | ↔ Both | `handle, value` | Integer value update |
| `2` | DATAREFUPDATEFLOAT | ↔ Both | `handle, value` | Float value update |
| `3` | DATAREFUPDATEINTARRAY | ↔ Both | `handle, value, element` | Int array element update |
| `4` | DATAREFUPDATEFLOATARRAY | ↔ Both | `handle, value, element` | Float array element update |
| `9` | DATAREFUPDATESTRING | → Arduino | `handle, length, data` | String value update |
| `r` | UPDATES | ← Arduino | `handle, rate, precision` | Subscribe to DataRef |
| `t` | UPDATESARRAY | ← Arduino | `handle, rate, precision, element` | Subscribe to array element |
| `y` | UPDATES_TYPE | ← Arduino | `handle, type, rate, precision` | Subscribe with type |
| `w` | UPDATES_TYPE_ARRAY | ← Arduino | `handle, type, rate, precision, element` | Subscribe array with type |
| `u` | SCALING | ← Arduino | `handle, inLo, inHi, outLo, outHi` | Set value scaling |
| `d` | DATAREFTOUCH | ← Arduino | `handle` | Force value resend |
| `k` | COMMANDTRIGGER | ← Arduino | `handle, count` | Trigger command N times |
| `i` | COMMANDSTART | ← Arduino | `handle` | Begin held command |
| `j` | COMMANDEND | ← Arduino | `handle` | End held command |
| `$` | SPECIAL | ← Arduino | `subCmd, ...` | Deprecated key/button sim |
| `g` | PRINTDEBUG | ← Arduino | `"message"` | Log debug message |
| `s` | SPEAK | ← Arduino | `"message"` | Speak via TTS |
| `p` | DATAFLOWPAUSE | ← Arduino | `bufferBytes` | Pause plugin data flow |
| `q` | DATAFLOWRESUME | ← Arduino | `bufferBytes` | Resume plugin data flow |
| `f` | SETDATAFLOWSPEED | ← Arduino | `bytesPerSec` | Throttle data rate |
| `z` | RESET | ← Arduino | `0` | Request connection reset |

---

## Timing and Limits

| Parameter | Value | Notes |
|-----------|-------|-------|
| Baud rate | 115200 bps | Must match on both sides |
| Flight loop interval | ~50 ms | Plugin checks DataRefs and processes serial data |
| Discovery timeout | 3 seconds | Plugin waits for name response per port |
| Registration timeout | 90 seconds | Arduino waits for handle response per registration |
| Serial read timeout | 500 ms | Arduino `Stream::setTimeout()` for packet reception |
| Max packet size | 200 bytes | Configurable; must be < 256 |
| Max devices | 30 | Simultaneously connected Arduino boards |
| Max array elements | 10 | Per DataRef subscription |
| Float precision | 4 decimals | Configurable via `XPL_FLOATPRECISION` |

---

## Example Session

A complete session showing an Arduino controlling a beacon LED and a
heading bug encoder:

```
# Device discovery
Plugin → Arduino:  [N]
Arduino → Plugin:  [v,"Apr  7 2026 10:30:00"]
Arduino → Plugin:  [n,"Heading Panel"]

# Plugin ready for registrations
Plugin → Arduino:  [Q]

# Arduino registers DataRef and Command
Arduino → Plugin:  [b,"sim/cockpit2/switches/beacon_on"]
Plugin → Arduino:  [D,0]

Arduino → Plugin:  [m,"sim/autopilot/heading_up"]
Plugin → Arduino:  [C,0]

Arduino → Plugin:  [m,"sim/autopilot/heading_down"]
Plugin → Arduino:  [C,1]

# Arduino subscribes to beacon updates (every 100ms, any change)
Arduino → Plugin:  [r,0,100,0]

# Normal operation — plugin pushes beacon state change
Plugin → Arduino:  [1,0,1]           # beacon turned ON

# User turns encoder — Arduino triggers heading up 3 times
Arduino → Plugin:  [k,0,3]

# User turns encoder other way — heading down once
Arduino → Plugin:  [k,1,1]

# Beacon turned off in cockpit
Plugin → Arduino:  [1,0,0]           # beacon turned OFF

# X-Plane shutting down
Plugin → Arduino:  [X]
```
