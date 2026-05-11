#pragma once
#include <Arduino.h>

// ── Pins ────────────────────────────────────────────────────
#define BUTTON_CYCLE  0    // left  button — scroll / cycle
#define BUTTON_SEND   35   // right button — confirm / use move

// ── MACs ────────────────────────────────────────────────────
#define DEVICE_1_MAC  {0xA0, 0xDD, 0x6C, 0x73, 0xAC, 0xF4}
#define DEVICE_2_MAC  {0xA0, 0xDD, 0x6C, 0x74, 0xC0, 0x8C}

// ── Packet types ─────────────────────────────────────────────
#define PKT_SELECT  0
#define PKT_MOVE    1
#define PKT_RESET   2

typedef struct {
  uint8_t type;
  uint8_t data;
  uint8_t seq;
} __attribute__((packed)) BattlePacket;

// ── Types ────────────────────────────────────────────────────
#define TYPE_NORMAL   0
#define TYPE_FIRE     1
#define TYPE_WATER    2
#define TYPE_GRASS    3
#define TYPE_ELECTRIC 4
#define NUM_TYPES     5

// Type effectiveness table [attacking type][defending pokemon type]
// Values are x10: 10 = 1x, 20 = 2x, 5 = 0.5x
static const uint8_t TYPE_CHART[NUM_TYPES][NUM_TYPES] = {
  //          NRM  FIR  WAT  GRS  ELC  — defender
  /* NRM */  { 10,  10,  10,  10,  10 },
  /* FIR */  { 10,   5,   5,  20,  10 },
  /* WAT */  { 10,  20,   5,   5,  10 },
  /* GRS */  { 10,   5,  20,   5,  10 },
  /* ELC */  { 10,  10,  20,   5,   5 },
};

// Returns x10 effectiveness (10=normal, 20=super, 5=not very)
inline uint8_t typeEffectiveness(uint8_t move_type, uint8_t poke_type) {
  return TYPE_CHART[move_type][poke_type];
}

// ── Stat stage system (-3 to +3) ────────────────────────────
static const uint16_t STAGE_MULT[7] = {50, 67, 75, 100, 133, 150, 200};
inline uint16_t stageMult(int8_t stage) {
  return STAGE_MULT[constrain((int)stage + 3, 0, 6)];
}

// ── Move effects ─────────────────────────────────────────────
#define EFF_NONE          0
#define EFF_LOWER_OPP_ATK 1
#define EFF_LOWER_OPP_DEF 2
#define EFF_RAISE_MY_ATK  3
#define EFF_RAISE_MY_DEF  4

struct Move {
  const char* name;
  uint8_t     power;
  uint8_t     move_type;  // TYPE_* constant
  uint8_t     effect;     // EFF_* constant
  const char* desc;
};

struct Pokemon {
  const char* name;
  uint8_t     max_hp;
  uint8_t     poke_type;  // TYPE_* constant (used as defender)
  Move        moves[4];
};

// ── Roster ───────────────────────────────────────────────────
static const Pokemon ROSTER[4] = {
  { "Charmander", 76, TYPE_FIRE, {
    { "Scratch",     40, TYPE_NORMAL, EFF_NONE,          "A quick scratch attack." },
    { "Ember",       40, TYPE_FIRE,   EFF_NONE,          "Shoots small flames." },
    { "Growl",        0, TYPE_NORMAL, EFF_LOWER_OPP_ATK, "Lowers opponent's Attack." },
    { "Smokescreen",  0, TYPE_NORMAL, EFF_LOWER_OPP_DEF, "Lowers opponent's Defense." },
  }},
  { "Squirtle", 80, TYPE_WATER, {
    { "Tackle",      35, TYPE_NORMAL, EFF_NONE,          "A solid body slam." },
    { "Water Gun",   40, TYPE_WATER,  EFF_NONE,          "Fires a jet of water." },
    { "Tail Whip",    0, TYPE_NORMAL, EFF_LOWER_OPP_DEF, "Lowers opponent's Defense." },
    { "Withdraw",     0, TYPE_NORMAL, EFF_RAISE_MY_DEF,  "Tucks in to raise Defense." },
  }},
  { "Bulbasaur", 90, TYPE_GRASS, {
    { "Tackle",      35, TYPE_NORMAL, EFF_NONE,          "A solid body slam." },
    { "Vine Whip",   35, TYPE_GRASS,  EFF_NONE,          "Strikes with vines." },
    { "Growl",        0, TYPE_NORMAL, EFF_LOWER_OPP_ATK, "Lowers opponent's Attack." },
    { "Defense Curl", 0, TYPE_NORMAL, EFF_RAISE_MY_DEF,  "Curls up to raise Defense." },
  }},
  { "Pikachu", 65, TYPE_ELECTRIC, {
    { "Quick Attack",  40, TYPE_NORMAL,   EFF_NONE,          "A very fast strike." },
    { "Thundershock",  55, TYPE_ELECTRIC, EFF_NONE,          "Fires an electric bolt." },
    { "Thunder Wave",   0, TYPE_ELECTRIC, EFF_LOWER_OPP_ATK, "Weakens opponent's Attack." },
    { "Tail Whip",      0, TYPE_NORMAL,   EFF_LOWER_OPP_DEF, "Lowers opponent's Defense." },
  }},
};
#define NUM_POKEMON 4

// ── Damage formula ───────────────────────────────────────────
// Returns damage dealt. eff_out receives the x10 type multiplier
// so the caller can display "Super effective!" etc.
inline int calcDamage(uint8_t power, uint8_t move_type, uint8_t defender_type,
                      int8_t atk_stage, int8_t def_stage, uint8_t* eff_out) {
  if (power == 0) { if (eff_out) *eff_out = 10; return 0; }
  uint8_t eff = typeEffectiveness(move_type, defender_type);
  if (eff_out) *eff_out = eff;
  // damage = power * stat_stages * type_effectiveness
  uint32_t d = (uint32_t)power * stageMult(atk_stage) / stageMult(def_stage);
  d = d * eff / 10;
  return (int)d;
}