# rotom

A two-player turn-based Pokémon battle game running on two ESP32 TTGO T-Display boards, communicating wirelessly via ESP-NOW.

<img src="squirtle_charmander.png" width="400"/>

## Overview

Each player holds one ESP32 and players select a Pokémon from a roster of four (Charmander, Squirtle, Bulbasaur, Pikachu), then battle turn-by-turn using a two-button interface. **Bottom button to cycle, top to select.** The game implements a full type effectiveness chart, stat stage modifiers (Attack/Defense ±1–3), move previews, and per-hit animations on the TFT display.

Damage is calculated deterministically on both devices from the same inputs via assigned indices for moves.

## Hardware

| Component | Notes |
|---|---|
| 2× LilyGO TTGO T-Display (ESP32) | One per player |

No additional hardware required. Uses the two built-in buttons on each board.

| Button | GPIO | Function |
|---|---|---|
| Bottom | 0 | Scroll / cycle options |
| Top | 35 | Confirm / use move |

## Setup

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- TFT_eSPI library configured for the TTGO T-Display

### Installation

1. Clone the repo
2. Open in PlatformIO
3. In `include/shared.h`, update the two MAC addresses to match your boards:
```cpp
#define DEVICE_1_MAC  {0x__, ...}
#define DEVICE_2_MAC  {0x__, ...}
```
To find a board's MAC, flash any sketch that prints `WiFi.macAddress()` to Serial.

4. In `src/main.cpp`, set `DEVICE_ID` before flashing each board:
```cpp
#define DEVICE_ID 1   // change to 2 for the second board
```

5. Flash Device 1, then Device 2.

### File Structure

```
include/
  shared.h      — Pokemon/Move structs, roster, type chart, damage formula
  sprites.h     — RGB565 sprite data for all 8 Pokemon sprites (front + back)
src/
  main.cpp      — Game logic, display rendering, ESP-NOW communication
```

## Gameplay

1. Both players scroll through the roster with the left button and confirm with the right button
2. Once both have selected, the battle starts automatically -- Device 1 always goes first (as of now)
3. During your turn: scroll moves with bottom button, use the highlighted move with top
4. Status moves (Growl, Withdraw, etc.) apply stat stage changes that affect future damage
5. Type effectiveness is displayed after each hit — super effective hits deal 2× damage
6. On game over, press either button to return to the selection screen

## Type Chart

|  | Normal | Fire | Water | Grass | Electric |
|---|---|---|---|---|---|
| **Normal** | 1× | 1× | 1× | 1× | 1× |
| **Fire** | 1× | 0.5× | 0.5× | **2×** | 1× |
| **Water** | 1× | **2×** | 0.5× | 0.5× | 1× |
| **Grass** | 1× | 0.5× | **2×** | 0.5× | 1× |
| **Electric** | 1× | 1× | **2×** | 0.5× | 0.5× |
