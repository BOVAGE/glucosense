# 3.3.5 Mechanical Enclosure & Packaging Specification

This document defines the physical layout, packaging architecture, and mechanical assembly specifications for the **GlucoSense Tabletop Non-Invasive Blood Glucose Monitor**. 

During early prototyping, components are typically arranged on a flat breadboard plane. However, packaging a device for clinical trials or patient use requires transitioning to a **3D mechanical design**. This specification ensures that panel-mounted controls (OLED display, tactile buttons), side-mounted sensors (MAX30100), and internal electronics (ESP32 NodeMCU on a Vero board) fit securely within a standard ABS project box with proper clearance and wire routing.

---

## 1. Mechanical Packaging Architecture

The tabletop console employs a **split-enclosure design** with two distinct mounting zones to maximize ergonomics, protect fragile components, and ensure USB and sensor accessibility.

```
                  EXPLODED VIEW (SIDE PROFILE)
                  
                  +-----------------------------------+  <-- TOP COVER / LID
                  |    OLED   BTN1             BTN2   |
                  |    [====]  ( )              ( )   |
                  +----|--------|----------------|----+
                       | Wire   | Wire           | Wire
                       |        |                |
                    ==================================   <-- 15 mm Spacers (M3 Standoffs)
                       |        |                |
                  +----v--------v----------------v----+
                  |    [==============================]  |  <-- VERO BOARD (Upside down)
                  |    [      ESP32 NodeMCU Board     ]  |
                  +-----------------------------------+
                  
                                                         <-- SIDE WALL Breakouts:
                                                             - USB Port (Aligned with ESP32)
                                                             - Thumb Tunnel (MAX30100 behind)
                  +-----------------------------------+
                  |                                   |  <-- BOTTOM COVER
                  +-----------------------------------+
```

### 1.1 Top Cover (Lid) Components (Panel-Mounted)
- **SSD1306 OLED Display (0.96")**: Mounted directly inside a rectangular cutout in the lid. It is secured using hot glue or M2 screws/washers on mounting tabs.
- **Start (Green) & Reset (Red) Pushbuttons**: Standard 12mm threaded momentary pushbuttons are screwed into circular cutouts on the lid.
- **Status LEDs**: Standard 3mm or 5mm LEDs (Wi-Fi and recording status) are mounted using plastic bezels.

### 1.2 Inner Lid (Suspended Vero Board)
- To eliminate the clutter of bottom-mounted boards and long, messy wire bundles, the **Vero board is mounted upside down directly beneath the lid**.
- Four **15 mm nylon or brass M3 standoffs** are screwed into the top lid, suspending the Vero board parallel to the lid.
- This upside-down layout ensures that:
  - Connectors and pins face down into the open space of the box.
  - The ESP32's micro-USB/USB-C port aligns perfectly with a side cutout in the wall.
  - Wiring paths to the OLED, buttons, and status LEDs are extremely short (approx. 5–8 cm).

### 1.3 Side Wall Components (Panel-Mounted)
- **MAX30100/MAX30102 PPG Sensor**: Mounted vertically behind a custom **thumb tunnel** cutout on the right-side wall.
- **USB Power Cutout**: An oblong slot in the left-side wall exposes the ESP32 programming/power port.

---

## 2. Component Clearances & Box Dimensions

### 2.1 Enclosure Selection
To house the ESP32 NodeMCU, Vero board, and panel-mounted buttons comfortably, we recommend a standard **ABS Electrical Project Box** with the following minimum dimensions:

| Dimension | Minimum Value | Recommended | Rationale |
| :--- | :--- | :--- | :--- |
| **Length (X)** | 110 mm | **115 mm** | Fits the 85mm Vero board length plus side sensor clearances. |
| **Width (Y)** | 85 mm | **90 mm** | Fits the width of the OLED and dual button layout. |
| **Height (Z)** | 50 mm | **55 mm** | Clears the 15mm standoffs, Vero board components, and bottom USB plug radius. |

### 2.2 Clearance Budget (Z-Axis)
The vertical space is packed sequentially to prevent component collisions:

```
Lid Surface  ========================================= (0 mm)
               [ OLED Screen Profile: 4.5 mm depth ]
               [ Button Threaded Bodies: 12.0 mm depth ]
Standoffs    ----------------------------------------- (15 mm spacer level)
               [ Vero Board Thickness: 1.6 mm ]
               [ Female Header & Soldered Pins: 10 mm ]
               [ ESP32 NodeMCU Board Profile: 6.0 mm ]
Free Space   
Bottom Cover ========================================= (55 mm)
```
*Using **15 mm standoffs** provides 3 mm of safety margin above the longest button bodies, ensuring the Vero board does not crush or contact the metal button pins.*

---

## 3. Wiring Harness & Jumper Routing

Instead of soldering components directly to the Vero board (which prevents disassembly), use **flexible stranded wires with female DuPont/JST connector harnesses** that plug into male headers on the Vero board.

### 3.1 Wiring Map Table

| Component | Physical Pin | ESP32 GPIO | Wire Color | Connector Type | Length |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Green Button (Start)** | Pin 1 | GPIO 12 | Green | 1-Pin DuPont | 8 cm |
| | Pin 2 (GND) | GND | Black | 1-Pin DuPont | 8 cm |
| **Red Button (Reset)** | Pin 1 | GPIO 14 | Red | 1-Pin DuPont | 8 cm |
| | Pin 2 (GND) | GND | Black | 1-Pin DuPont | 8 cm |
| **OLED Display** | VCC (3.3V) | 3.3V | Red | 4-Pin DuPont/JST | 6 cm |
| | GND | GND | Black | | 6 cm |
| | SDA | GPIO 21 | Yellow | | 6 cm |
| | SCL | GPIO 22 | Blue | | 6 cm |
| **MAX30100 Sensor** | VCC (3.3V) | 3.3V | Red | 4-Pin JST-PH (2.0mm)| 12 cm |
| | GND | GND | Black | | 12 cm |
| | SDA | GPIO 21 | Yellow | | 12 cm |
| | SCL | GPIO 22 | Blue | | 12 cm |

---

## 4. Step-by-Step Enclosure Assembly Guide

```
+-------------------------------------------------------------------+
|                          ASSEMBLY FLOW                            |
|                                                                   |
| 1. Solder Vero Board  --> 2. Machining Cutouts --> 3. Lid Mounting |
|           |                                            |          |
|           +---------------------+----------------------+          |
|                                 |                                 |
|                                 v                                 |
|                       4. Wire Connections                         |
|                                 |                                 |
|                                 v                                 |
|                       5. Final Box Enclosure                      |
+-------------------------------------------------------------------+
```

### Step 1: Vero Board Preparation & Soldering
1. Solder two rows of 15-pin female headers onto the Vero board to socket the **ESP32 NodeMCU**.
2. Solder I2C pull-up resistors ($4.7\text{ k}\Omega$) between the 3.3V rail and SCL (GPIO 22) / SDA (GPIO 21) lines.
3. Solder a 4-pin male header row for the OLED connection and a 4-pin male header row for the MAX30100 connection.
4. Solder a 2-pin male header for the Green Button and a 2-pin male header for the Red Button.

### Step 2: Enclosure Machining & Cutouts
1. **OLED Slot**: Mark a $24\text{ mm} \times 14\text{ mm}$ rectangle on the top cover. Drill pilot holes and use a file or rotary tool (Dremel) to cut out the window.
2. **Button Holes**: Drill two $12\text{ mm}$ circular holes on either side of the OLED slot.
3. **USB Slot**: Measure the alignment of the ESP32 socket and cut a $12\text{ mm} \times 6\text{ mm}$ oblong slot in the left-side wall.
4. **Thumb Tunnel**: Drill a $15\text{ mm}$ diameter hole in the right-side wall. Fit a short plastic cylindrical sleeve (the "Thumb Tunnel") inside the hole to prevent ambient light leakage into the MAX30100 photodiode.

### Step 3: Component Installation
1. Secure the OLED display to the inside of the lid using hot glue or small M2 mounting screws.
2. Insert the Green and Red pushbuttons into their holes from the outside and screw their locking nuts from the inside.
3. Mount four **15 mm standoffs** to the top lid using flat-head M3 countersunk screws from the top cover.
4. Position the MAX30100 sensor board vertically directly behind the thumb tunnel sleeve. Secure it using mounting tape or a 3D-printed bracket screwed to the side wall.

### Step 4: Wire Connection & Mating
1. Socket the ESP32 NodeMCU into the Vero board headers (ensure correct antenna orientation).
2. Screw the Vero board onto the four 15 mm standoffs using M3 pan-head screws.
3. Connect the OLED ribbon cable to the dedicated 4-pin OLED header.
4. Connect the Green and Red button wires to their respective 2-pin headers.
5. Route the 12 cm MAX30100 sensor cable along the inside wall and plug it into the 4-pin sensor header.

### Step 5: Enclosure Closure & Verification
1. Carefully close the lid, ensuring no wires are pinched or kinked between the standoffs and side walls.
2. Connect a micro-USB/USB-C power cable through the left side cutout.
3. Verify that the ESP32 boots up, the OLED displays `GLUCOSENSE READY`, and pressing the Green button initiates a PPG scan.
