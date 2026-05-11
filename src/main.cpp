#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "shared.h"
#include "sprites.h"

TFT_eSPI tft = TFT_eSPI();

// sprite lookup by pokemon index
const uint16_t* FRONT_SPRITES[4] = {
  charmander_front, squirtle_front, bulbasaur_front, pikachu_front
};
const uint16_t* BACK_SPRITES[4] = {
  charmander_back, squirtle_back, bulbasaur_back, pikachu_back
};

// ===========================================================
//  CHANGE THIS before flashing each device.
//  1 = device 1 (goes first), 2 = device 2 (goes second)
// ===========================================================
#define DEVICE_ID 1

#if DEVICE_ID == 1
  uint8_t my_id      = 1;
  uint8_t peer_mac[] = DEVICE_2_MAC;
#else
  uint8_t my_id      = 2;
  uint8_t peer_mac[] = DEVICE_1_MAC;
#endif

// ===========================================================
//  Game state
// ===========================================================
enum Phase { SELECTING, BATTLE, GAME_OVER };
Phase phase = SELECTING;

enum TurnState { MY_TURN, OPPONENT_TURN };
TurnState turn_state;

// selection state
uint8_t sel_cursor    = 0;    // currently highlighted pokemon
uint8_t my_poke_idx   = 0;   // my confirmed choice
uint8_t opp_poke_idx  = 0;   // opponent's confirmed choice
bool    my_confirmed  = false;
bool    opp_confirmed = false;

// battle state
int     my_hp       = 0;
int     opponent_hp = 0;
int8_t  my_atk = 0, my_def = 0;    // my stat stages
int8_t  opp_atk = 0, opp_def = 0;  // opponent stat stages
uint8_t selected_move = 0;
char    last_action[48] = "";
bool    i_won = false;

// ESP-NOW receive flag (set in callback, read in loop — Lecture 14 pattern)
volatile bool         move_received = false;
volatile BattlePacket incoming;
uint8_t               last_seq_seen = 255;
uint8_t               seq_num       = 0;

// ===========================================================
//  Display helpers
// ===========================================================

void drawHPBar(int x, int y, int w, int hp, int max_hp) {
  float pct = (max_hp > 0) ? (float)hp / max_hp : 0;
  int filled = constrain((int)(pct * w), 0, w);
  uint16_t color = (pct > 0.5f) ? TFT_GREEN : (pct > 0.25f) ? TFT_YELLOW : TFT_RED;
  tft.fillRect(x, y, w, 6, TFT_DARKGREY);
  tft.fillRect(x, y, filled, 6, color);
}

void drawSprite(int x, int y, const uint16_t* sprite) {
  tft.pushImage(x, y, SPRITE_W, SPRITE_H, sprite);
}

// ===========================================================
//  Animations
// ===========================================================

void flashHit() {
  tft.fillScreen(TFT_RED);   delay(90);
  tft.fillScreen(TFT_BLACK); delay(40);
}

void flashStat() {
  tft.fillScreen(TFT_YELLOW); delay(90);
  tft.fillScreen(TFT_BLACK);  delay(40);
}

void flashWin() {
  for (int i = 0; i < 4; i++) {
    tft.fillScreen(TFT_GREEN); delay(120);
    tft.fillScreen(TFT_BLACK); delay(80);
  }
}

// ===========================================================
//  Stat stage helper
//  isMyMove=true: I am attacking | false: opponent is attacking
// ===========================================================
const char* applyEffect(uint8_t eff, bool isMyMove) {
  switch (eff) {
    case EFF_LOWER_OPP_ATK:
      if (isMyMove) opp_atk = constrain(opp_atk - 1, -3, 3);
      else          my_atk  = constrain(my_atk  - 1, -3, 3);
      return isMyMove ? "Opp Attack fell!" : "Your Attack fell!";
    case EFF_LOWER_OPP_DEF:
      if (isMyMove) opp_def = constrain(opp_def - 1, -3, 3);
      else          my_def  = constrain(my_def  - 1, -3, 3);
      return isMyMove ? "Opp Defense fell!" : "Your Defense fell!";
    case EFF_RAISE_MY_ATK:
      if (isMyMove) my_atk  = constrain(my_atk  + 1, -3, 3);
      else          opp_atk = constrain(opp_atk + 1, -3, 3);
      return isMyMove ? "Attack rose!" : "Opp Attack rose!";
    case EFF_RAISE_MY_DEF:
      if (isMyMove) my_def  = constrain(my_def  + 1, -3, 3);
      else          opp_def = constrain(opp_def + 1, -3, 3);
      return isMyMove ? "Defense rose!" : "Opp Defense rose!";
    default: return "";
  }
}

// ===========================================================
//  drawScreen — dispatches to the right screen for each phase
// ===========================================================

void drawSelectionScreen() {
  tft.fillScreen(TFT_BLACK);
  const Pokemon& p = ROSTER[sel_cursor];

  // header
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(38, 3);
  tft.print("-- CHOOSE YOUR POKÉMON --");

  // nav arrows + pokemon name (size 2)
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 22);    tft.print("<");
  tft.setCursor(226, 22);  tft.print(">");
  tft.setCursor(22, 22);   tft.print(p.name);

  // HP
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(22, 48);
  tft.printf("HP: %d", p.max_hp);

  // moves preview
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(22, 62);
  for (int i = 0; i < 4; i++) {
    tft.print(p.moves[i].name);
    if (i < 3) tft.print("  /  ");
  }

  // position dots
  for (int i = 0; i < NUM_POKEMON; i++) {
    uint16_t c = (i == (int)sel_cursor) ? TFT_WHITE : TFT_DARKGREY;
    tft.fillCircle(100 + i * 14, 82, 4, c);
  }

  // bottom status
  if (!my_confirmed) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 98);
    tft.print("CYCLE = scroll    SEND = choose");
  } else {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(32, 98);
    tft.print("Waiting for opponent...");
  }
}

void drawBattleScreen() {
  tft.fillScreen(TFT_BLACK);
  const Pokemon& me  = ROSTER[my_poke_idx];
  const Pokemon& opp = ROSTER[opp_poke_idx];

  // sprites: my back sprite left, opponent front sprite right
  drawSprite(0,  0, BACK_SPRITES[my_poke_idx]);
  drawSprite(184, 0, FRONT_SPRITES[opp_poke_idx]);

  // middle column HP info (X=44, leaves room for sprites on both sides)
  const int mx = 60;

  // my info
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(mx, 3);
  tft.print(me.name);
  tft.setTextColor(TFT_WHITE);
  tft.printf(" %d/%d", my_hp, me.max_hp);
  drawHPBar(mx, 13, 120, my_hp, me.max_hp);

  // my stat stages (if any)
  if (my_atk != 0 || my_def != 0) {
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(mx, 21);
    if (my_atk != 0) tft.printf("A%+d ", my_atk);
    if (my_def != 0) tft.printf("D%+d",  my_def);
  }

  // opponent info
  tft.setTextColor(TFT_RED);
  tft.setCursor(mx, 28);
  tft.print(opp.name);
  tft.setTextColor(TFT_WHITE);
  tft.printf(" %d/%d", opponent_hp, opp.max_hp);
  drawHPBar(mx, 38, 120, opponent_hp, opp.max_hp);

  // opponent stat stages (if any)
  if (opp_atk != 0 || opp_def != 0) {
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(mx, 46);
    if (opp_atk != 0) tft.printf("A%+d ", opp_atk);
    if (opp_def != 0) tft.printf("D%+d",  opp_def);
  }

  // last action message
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(2, 48);
  tft.print(last_action);

  // divider
  tft.drawLine(0, 58, 240, 58, TFT_DARKGREY);

  // move list (my turn) or waiting text
  if (turn_state == MY_TURN) {
    for (int i = 0; i < 4; i++) {
      int y = 62 + i * 16;
      const Move& mv = me.moves[i];
      bool sel = (i == (int)selected_move);

      if (sel) tft.fillRect(0, y - 2, 240, 12, TFT_NAVY);

      tft.setTextSize(1);
      tft.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY);
      tft.setCursor(4, y);
      tft.print(sel ? "> " : "  ");
      tft.print(mv.name);

      // power or "--" on the right
      tft.setCursor(196, y);
      if (mv.power > 0) {
        tft.setTextColor(sel ? TFT_YELLOW : TFT_DARKGREY);
        tft.printf("%3d", mv.power);
      } else {
        tft.setTextColor(sel ? TFT_CYAN : TFT_DARKGREY);
        tft.print(" --");
      }
    }
    // move description at very bottom
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(4, 128);
    tft.print(me.moves[selected_move].desc);

  } else {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(52, 90);
    tft.print("Waiting for opponent...");
  }
}

void drawGameOverScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  if (i_won) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(28, 20);
    tft.print("YOU WIN!");
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(28, 20);
    tft.print("YOU LOSE");
  }
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 72);
  tft.printf("%s  vs  %s", ROSTER[my_poke_idx].name, ROSTER[opp_poke_idx].name);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(18, 100);
  tft.print("Press any button to play again");
}

void drawScreen() {
  if      (phase == SELECTING)  drawSelectionScreen();
  else if (phase == BATTLE)     drawBattleScreen();
  else                           drawGameOverScreen();
}

// ===========================================================
//  Helpers
// ===========================================================

bool buttonPressed(int pin) {
  static unsigned long last_cycle = 0;
  static unsigned long last_send  = 0;
  unsigned long& last = (pin == BUTTON_CYCLE) ? last_cycle : last_send;
  if (digitalRead(pin) == LOW && millis() - last > 250) {
    last = millis();
    return true;
  }
  return false;
}

void sendPkt(uint8_t type, uint8_t data) {
  BattlePacket pkt = { type, data, seq_num++ };
  esp_now_send(peer_mac, (uint8_t*)&pkt, sizeof(pkt));
}

// ===========================================================
//  Game logic
// ===========================================================

void startBattle() {
  phase       = BATTLE;
  turn_state  = (DEVICE_ID == 1) ? MY_TURN : OPPONENT_TURN;
  my_hp       = ROSTER[my_poke_idx].max_hp;
  opponent_hp = ROSTER[opp_poke_idx].max_hp;
  my_atk = my_def = opp_atk = opp_def = 0;
  selected_move = 0;
  i_won = false;
  snprintf(last_action, sizeof(last_action),
           turn_state == MY_TURN ? "You go first!" : "Opponent goes first...");
  drawScreen();
}

void resetGame() {
  phase         = SELECTING;
  sel_cursor    = 0;
  my_confirmed  = false;
  opp_confirmed = false;
  last_action[0] = '\0';
  drawScreen();
}

void typeEffMsg(uint8_t eff, char* buf, size_t len) {
  if      (eff >= 20) snprintf(buf, len, " Super effective!");
  else if (eff <=  5) snprintf(buf, len, " Not very effective...");
  else                buf[0] = '\0';
}

void sendMove() {
  const Move& mv = ROSTER[my_poke_idx].moves[selected_move];

  if (mv.power > 0) {
    // damage move
    uint8_t eff = 10;
    int dmg = calcDamage(mv.power, mv.move_type, ROSTER[opp_poke_idx].poke_type, my_atk, opp_def, &eff);
    char effmsg[24]; typeEffMsg(eff, effmsg, sizeof(effmsg));
    opponent_hp = max(0, opponent_hp - dmg);
    snprintf(last_action, sizeof(last_action), "You used %s! -%d HP%s", mv.name, dmg, effmsg);
    sendPkt(PKT_MOVE, selected_move);
    flashHit();
    if (opponent_hp <= 0) {
      i_won = true;
      phase = GAME_OVER;
      flashWin();
    } else {
      turn_state = OPPONENT_TURN;
    }
  } else {
    // status move
    const char* eff = applyEffect(mv.effect, true);
    snprintf(last_action, sizeof(last_action), "Used %s! %s", mv.name, eff);
    sendPkt(PKT_MOVE, selected_move);
    flashStat();
    turn_state = OPPONENT_TURN;
  }

  drawScreen();
  Serial.printf("[SEND] Used %s\n", mv.name);
}

void applyReceivedMove() {
  const Move& mv = ROSTER[opp_poke_idx].moves[incoming.data];

  if (mv.power > 0) {
    uint8_t eff = 10;
    int dmg = calcDamage(mv.power, mv.move_type, ROSTER[my_poke_idx].poke_type, opp_atk, my_def, &eff);
    char effmsg[24]; typeEffMsg(eff, effmsg, sizeof(effmsg));
    my_hp   = max(0, my_hp - dmg);
    snprintf(last_action, sizeof(last_action), "Foe used %s! -%d HP%s", mv.name, dmg, effmsg);
    flashHit();
    if (my_hp <= 0) {
      i_won = false;
      phase = GAME_OVER;
    } else {
      turn_state = MY_TURN;
    }
  } else {
    const char* eff = applyEffect(mv.effect, false);
    snprintf(last_action, sizeof(last_action), "Foe %s! %s", mv.name, eff);
    flashStat();
    turn_state = MY_TURN;
  }

  drawScreen();
  Serial.printf("[RECV] Foe used %s\n", mv.name);
}

// ===========================================================
//  ESP-NOW callbacks
// ===========================================================

void OnDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[SEND] Delivery failed!");
  }
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(BattlePacket)) return;
  BattlePacket pkt;
  memcpy(&pkt, data, sizeof(BattlePacket));
  // deduplicate move packets only
  if (pkt.type == PKT_MOVE && pkt.seq == last_seq_seen) return;
  if (pkt.type == PKT_MOVE) last_seq_seen = pkt.seq;
  memcpy((void*)&incoming, &pkt, sizeof(BattlePacket));
  move_received = true;
}

// ===========================================================
//  setup
// ===========================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  // display init
  tft.init();
  tft.setRotation(1);   // landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(4, 4);
  tft.printf("Device %d", my_id);
  tft.setCursor(4, 24);
  tft.print("Starting...");

  pinMode(BUTTON_CYCLE, INPUT_PULLUP);
  pinMode(BUTTON_SEND,  INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.printf("\n=== Pokemon Battle — Device %d ===\n", my_id);

  if (esp_now_init() != ESP_OK) {
    tft.setCursor(4, 44);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("ESP-NOW FAIL");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, peer_mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  esp_now_add_peer(&peer_info);

  delay(300);
  drawScreen();
}

// ===========================================================
//  main loop
// ===========================================================

void loop() {
  // ignore input for first 2 seconds (GPIO 0 boot quirk)
  if (millis() < 2000) return;

  // handle incoming packet (Lecture 14: flag set in callback, processed here)
  if (move_received) {
    move_received = false;
    BattlePacket pkt;
    memcpy(&pkt, (void*)&incoming, sizeof(BattlePacket));

    if (pkt.type == PKT_SELECT) {
      opp_poke_idx  = pkt.data;
      opp_confirmed = true;
      Serial.printf("[SEL] Opponent chose Pokemon %d\n", opp_poke_idx);
      if (my_confirmed) startBattle();
      else drawScreen();

    } else if (pkt.type == PKT_MOVE) {
      if (phase == BATTLE && turn_state == OPPONENT_TURN) {
        applyReceivedMove();
      }

    } else if (pkt.type == PKT_RESET) {
      resetGame();
    }
  }

  // selecting phase input
  if (phase == SELECTING) {
    if (buttonPressed(BUTTON_CYCLE)) {
      if (!my_confirmed) sel_cursor = (sel_cursor + 1) % NUM_POKEMON;
      drawScreen();
    }
    if (buttonPressed(BUTTON_SEND)) {
      if (!my_confirmed) {
        my_poke_idx  = sel_cursor;
        my_confirmed = true;
        sendPkt(PKT_SELECT, my_poke_idx);
        Serial.printf("[SEL] I chose Pokemon %d\n", my_poke_idx);
        if (opp_confirmed) startBattle();
        else drawScreen();
      }
    }
  }

  // battle phase input
  if (phase == BATTLE && turn_state == MY_TURN) {
    if (buttonPressed(BUTTON_CYCLE)) {
      selected_move = (selected_move + 1) % 4;
      // show selected move's description in the status line while browsing
      snprintf(last_action, sizeof(last_action), "%s",
               ROSTER[my_poke_idx].moves[selected_move].desc);
      drawScreen();
    }
    if (buttonPressed(BUTTON_SEND)) {
      sendMove();
    }
  }

  // game over phase input
  if (phase == GAME_OVER) {
    if (buttonPressed(BUTTON_CYCLE) || buttonPressed(BUTTON_SEND)) {
      sendPkt(PKT_RESET, 0);
      delay(50);
      resetGame();
    }
  }
}