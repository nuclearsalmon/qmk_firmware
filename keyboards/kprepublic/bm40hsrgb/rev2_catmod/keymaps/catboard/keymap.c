#include QMK_KEYBOARD_H


/* Constants
 * ------------------------------------------------------------------------- */

// layers
enum layers {
  _QUIRKY,
  _LOWER,
  _LOWER_B,
  _RAISE,
  _RAISE_B,
  _EXTEND,
  _ADJUST,
  _LAYERS_END
};

// keycodes
enum custom_keycodes {
  QUIRKY = SAFE_RANGE,
  LOWER,
  LOWER_B,
  RAISE,
  RAISE_B,
  EXTEND,
  CLR_RST,
  BSP_DEL
};

bool extend_key_active = false;
bool extend_key_latched = false;

// timer struct: ramp is 10x fixed-point (0.1 steps) for smooth slow pulses
#define RAMP_SCALE 10
#define RAMP_TO_VAL(r) ((uint8_t)((r) / RAMP_SCALE))

struct Timer_Ramp_t {
  uint16_t timer_data;
  uint8_t ramp_start;
  uint8_t ramp_end;
  uint16_t ramp;  // internal 0..(255*RAMP_SCALE); use RAMP_TO_VAL(ramp) for HSV
  bool ramp_direction;
} const Timer_Ramp_default = {0, 100, RGB_MATRIX_MAXIMUM_BRIGHTNESS, 100 * RAMP_SCALE, true};
typedef struct Timer_Ramp_t Timer_Ramp;

// timers
Timer_Ramp timer_danger = Timer_Ramp_default;
Timer_Ramp timer_caps   = Timer_Ramp_default;
Timer_Ramp timer_layer_v  = Timer_Ramp_default;
Timer_Ramp timer_layer_s  = {0, 140, 210, 140 * RAMP_SCALE, true};
// KC_TRNS muted pulse: low brightness, smooth (more steps) and similar speed to danger
#define MUTED_PULSE_MIN 40
#define MUTED_PULSE_MAX 78
Timer_Ramp timer_muted_pulse = {0, MUTED_PULSE_MIN, MUTED_PULSE_MAX, MUTED_PULSE_MIN * RAMP_SCALE, true};
const Timer_Ramp timer_boot_animation_default = {0, 0, RGB_MATRIX_MAXIMUM_BRIGHTNESS, 0, true};
Timer_Ramp timer_boot_animation = timer_boot_animation_default;
#define BOOT_BG_SPARKLE_CHANCE_MASK 0x07
#define BOOT_BG_SPARKLE_VAL_MIN 12
#define BOOT_BG_SPARKLE_VAL_RANGE 20

uint8_t boot_animation = 1;
const uint8_t boot_animation_stop = 3;

/* Boot animation: "C A T" on grid. C and T are 3 keys wide, A is 4 keys wide, centered for 2u spacebar. */
static const uint8_t PROGMEM cat_boot_keys[][2] = {
  /* C (cols 1–3) */ {0,0},{0,1},{0,2},{1,0},{2,0},{3,0},{3,1},{3,2},
  /* A (cols 4–7) */ {0,4},{0,5},{0,6},{0,7},{1,4},{1,7},{2,4},{2,5},{2,6},{2,7},{3,4},{3,7},
  /* T (cols 8–10) */ {0,9},{0,10},{0,11},{1,10},{2,10},{3,10},{4,10}
};
#define CAT_BOOT_KEY_COUNT (sizeof(cat_boot_keys) / sizeof(cat_boot_keys[0]))

static bool is_cat_boot_key(uint8_t row, uint8_t col) {
  for (uint8_t i = 0; i < CAT_BOOT_KEY_COUNT; i++) {
    if (pgm_read_byte(&cat_boot_keys[i][0]) == row && pgm_read_byte(&cat_boot_keys[i][1]) == col) return true;
  }
  return false;
}

/* Key latching system
 * ------------------------------------------------------------------------- */

#define MAX_LATCHED_KEYS 12

typedef struct {
  uint8_t row;
  uint8_t col;
  uint16_t keycode;
  uint8_t source_layer;
  bool is_latched;
} latched_key_t;

static latched_key_t latched_keys[MAX_LATCHED_KEYS];
static uint8_t num_latched_keys = 0;

/* Resolve keys
 * ------------------------------------------------------------------------- */

#define IS_CAPS_ON() (host_keyboard_led_state().caps_lock)
#define KEYCODE_AT_LAYER(layer, col, row) keymap_key_to_keycode((layer), (keypos_t){(col), (row)})
#define KEYCODE_AT_LAYER_BELOW(col, row) keymap_key_to_keycode(layer_switch_get_layer((keypos_t){(col), (row)}), (keypos_t){(col), (row)})
 
static bool is_layer_changer(uint16_t keycode) {
  if (keycode >= QK_MODS) {
    uint16_t base = keycode & 0xFF00;
    if (base == QK_MOMENTARY || base == QK_LAYER_TAP || base == QK_LAYER_MOD ||
        base == QK_TOGGLE_LAYER || base == QK_ONE_SHOT_LAYER || base == QK_TO ||
        base == QK_LAYER_TAP_TOGGLE) return true;
  }
  return (keycode >= LOWER && keycode <= EXTEND);
}

/* Latching
 * ------------------------------------------------------------------------- */ 


static latched_key_t* find_latched_key(uint8_t row, uint8_t col) {
  for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
    if (latched_keys[i].keycode && latched_keys[i].row == row && latched_keys[i].col == col) {
      return &latched_keys[i];
    }
  }
  return NULL;
}

static void track_key_press(uint8_t row, uint8_t col, uint16_t keycode, uint8_t source_layer) {
  if (keycode == KC_NO || keycode == KC_TRNS || is_layer_changer(keycode)) return;
  
  latched_key_t* existing = find_latched_key(row, col);
  if (existing && existing->source_layer == source_layer) return;
  
  for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
    if (latched_keys[i].keycode == 0) {
      latched_keys[i] = (latched_key_t){row, col, keycode, source_layer, false};
      num_latched_keys++;
      return;
    }
  }
}

static void unlatch_key(uint8_t row, uint8_t col) {
  latched_key_t* key = find_latched_key(row, col);
  if (key && !key->is_latched) {
    key->keycode = 0;
    num_latched_keys--;
  }
}

static void latch_keys_from_layer(uint8_t layer) {
  for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
    if (latched_keys[i].keycode && latched_keys[i].source_layer == layer && !latched_keys[i].is_latched) {
      register_code16(latched_keys[i].keycode);
      latched_keys[i].is_latched = true;
    }
  }
}

static void unlatch_keys_from_layer(uint8_t layer) {
  for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
    if (!latched_keys[i].keycode || latched_keys[i].source_layer != layer || !latched_keys[i].is_latched) continue;
    
    bool still_pressed = (matrix_get_row(latched_keys[i].row) & (1 << latched_keys[i].col)) != 0;
    unregister_code16(latched_keys[i].keycode);
    
    if (still_pressed) {
      latched_keys[i].is_latched = false;
    } else {
      latched_keys[i].keycode = 0;
      num_latched_keys--;
    }
  }
}

static void clear_all_latched_keys(void) {
  for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
    if (latched_keys[i].keycode && latched_keys[i].is_latched) {
      unregister_code16(latched_keys[i].keycode);
    }
    latched_keys[i].keycode = 0;
  }
  num_latched_keys = 0;
}

static bool is_layer_active(layer_state_t state, uint8_t layer) {
  return layer == 0 ? (state == 0) : (state & ((layer_state_t)1 << layer)) != 0;
}


/* Keymaps
 * ------------------------------------------------------------------------- */

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  /* Quirky (Modified qwerty)
   * ,-----------------------------------------------------------------------------------.
   * | Esc  |  Q   |  W   |  E   |  R   |  T   |  Y   |  U   |  I   |  O   |  P   |  `~  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | Tab  |  A   |  S   |  D   |  F   |  G   |  H   |  J   |  K   |  L   |  ;:  |  "'  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |LShift|  Z   |  X   |  C   |  V   |  B   |  N   |  M   |  ,<  |  .>  |  /?  |RShift|
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | LCtrl| Super| Alt  |LowerB| Lower| Space       | Raise|RaiseB| Ext  | Down | Right|
   * `-----------------------------------------------------------------------------------'
   */
  [_QUIRKY] = LAYOUT_ortho_4x12_1x2uC(
    KC_ESC , KC_Q   , KC_W   , KC_E   , KC_R   , KC_T   , KC_Y   , KC_U   , KC_I   , KC_O   , KC_P   , KC_GRV ,
    KC_TAB , KC_A   , KC_S   , KC_D   , KC_F   , KC_G   , KC_H   , KC_J   , KC_K   , KC_L   , KC_SCLN, KC_QUOT,
    KC_LSFT, KC_Z   , KC_X   , KC_C   , KC_V   , KC_B   , KC_N   , KC_M   , KC_COMM, KC_DOT , KC_SLSH, KC_RSFT,
    KC_LCTL, KC_LGUI, KC_LALT, LOWER_B, LOWER  , KC_SPC          , RAISE  , RAISE_B, EXTEND , KC_DOWN, KC_RGHT
  ),

  /* Lower
   * ,-----------------------------------------------------------------------------------.
   * | `~   |  1   |  2   |  3   |      | Mrwd | Mffd |      |      |      |      | Ins  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CAPS |  4   |  5   |  6   | >/|| | Mprev| Mnext|  -   |  =   |  {   |  }   |  \   |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |  7   |  8   |  9   |  0   | Vol+ | Vol- |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... | Enter       | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
  [_LOWER] = LAYOUT_ortho_4x12_1x2uC(
    KC_GRV , KC_1   , KC_2   , KC_3   , XXXXXXX, KC_MRWD, KC_MFFD, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_INS ,
    KC_CAPS, KC_4   , KC_5   , KC_6   , KC_MPLY, KC_MPRV, KC_MNXT, KC_MINS, KC_EQL , KC_LCBR, KC_RCBR, KC_BSLS,
    _______, KC_7   , KC_8   , KC_9   , KC_0   , KC_VOLU, KC_VOLD, XXXXXXX, XXXXXXX, KC_WBAK, XXXXXXX, KC_WFWD,
    _______, _______, _______, XXXXXXX, _______, KC_ENT          , _______, XXXXXXX, XXXXXXX, KC_UP  , KC_LEFT
  ),

  /* Lower 2
   * ,-----------------------------------------------------------------------------------.
   * | `~   |  F1  |  F2  |  F3  | F10  | F11  | F12  | F19  | F20  | F21  |      | Ins  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CAPS |  F4  |  F5  |  F6  | F13  | F14  | F15  |  -   |  =   |  {   |  }   |  \   |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |  F7  |  F8  |  F9  | F16  | F17  | F18  | F22  | F23  | F24  |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... |             | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
   [_LOWER_B] = LAYOUT_ortho_4x12_1x2uC(
    XXXXXXX, KC_F1  , KC_F2  , KC_F3  , KC_F10 , KC_F11 , KC_F12 , KC_F19 , KC_F20 , KC_F21 , XXXXXXX, XXXXXXX,
    XXXXXXX, KC_F4  , KC_F5  , KC_F6  , KC_F13 , KC_F14 , KC_F15 , KC_MINS, KC_EQL , KC_LCBR, KC_RCBR, KC_BSLS,
    _______, KC_F7  , KC_F8  , KC_F9  , KC_F16 , KC_F17 , KC_F18 , KC_F22 , KC_F23 , KC_F24 , XXXXXXX, _______,
    _______, _______, _______, _______, XXXXXXX, XXXXXXX         , XXXXXXX, XXXXXXX, XXXXXXX, KC_PGDN, KC_END
  ),
  
  /* Raise
   * ,-----------------------------------------------------------------------------------.
   * |  ~   |  !   |  @   |  #   |  $   |  %   |  ^   |  &   |  *   |  (   |  )   | Ins  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CAPS |      |      |      |      |      |      |  _   |  +   |  [   |  ]   |  |   |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... | Bsp/Del     | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
  [_RAISE] = LAYOUT_ortho_4x12_1x2uC(
    KC_TILD, KC_EXLM, KC_AT  , KC_HASH, KC_DLR , KC_PERC, KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_INS ,
    KC_CAPS, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_UNDS, KC_PLUS, KC_LBRC, KC_RBRC, KC_PIPE,
    _______, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, _______, KC_MRWD, KC_VOLU, KC_MFFD,
    _______, _______, _______, XXXXXXX, _______, BSP_DEL         , _______, _______, KC_MPRV, KC_VOLD, KC_MNXT
  ),

  /* Raise 2
   * ,-----------------------------------------------------------------------------------.
   * |  ~   |  !   |  @   |  #   |  $   |  %   |  ^   |  &   |  *   |  (   |  )   | Ins  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CAPS |      |      |      |      |      |      |  _   |  +   |  [   |  ]   |  |   |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... | Bsp/Del     | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
   [_RAISE_B] = LAYOUT_ortho_4x12_1x2uC(
    KC_TILD, KC_EXLM, KC_AT  , KC_HASH, KC_DLR , KC_PERC, KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_INS ,
    KC_CAPS, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_UNDS, KC_PLUS, KC_LBRC, KC_RBRC, KC_PIPE,
    _______, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, _______, KC_MRWD, KC_VOLU, KC_MFFD,
    _______, _______, _______, XXXXXXX, _______, BSP_DEL         , _______, _______, KC_MPRV, KC_VOLD, KC_MNXT
  ),

  /* Extend (Ext key)
   * ,-----------------------------------------------------------------------------------.
   * |      |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      |      |             |      |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
   [_EXTEND] = LAYOUT_ortho_4x12_1x2uC(
    MS_ACL2, MS_BTN4, MS_WHLU, MS_BTN5, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    MS_ACL1, MS_WHLL, MS_WHLD, MS_WHLR, XXXXXXX, DM_REC1, DM_REC2, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    MS_ACL0, MS_BTN1, MS_UP  , MS_BTN2, XXXXXXX, DM_PLY1, DM_PLY2, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    MS_BTN3, MS_LEFT, MS_DOWN, MS_RGHT, XXXXXXX, DM_RSTP         , XXXXXXX, XXXXXXX, _______, KC_UP  , KC_LEFT
  ),  
  
  /* Adjust (Lower + Raise)
   * ,-----------------------------------------------------------------------------------.
   * | BOOT |DBTOGG|      |      |      |      | RGB_A| MOD+ | SPD+ | HUE+ | SAT+ | VAL+ |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |CLRRST|      |      |      |      |      | RGB_B| MOD- | SPD- | HUE- | SAT- | VAL- |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CLR  |      |      |      |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... |             | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
  [_ADJUST] = LAYOUT_ortho_4x12_1x2uC(
    QK_BOOT, DB_TOGG, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, RM_TOGG, RM_NEXT, RM_SPDU, RM_HUEU, RM_SATU, RM_VALU,
    CLR_RST, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, UG_TOGG, RM_PREV, RM_SPDD, RM_HUED, RM_SATD, RM_VALD,
    EE_CLR , XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, XXXXXXX         , _______, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX
  )
};


/* Timers
 * ------------------------------------------------------------------------- */

// ramp_step in scaled units: 1 = 0.1 output step (smooth), 10 = 1.0 step (same as old)
void handle_timer(uint16_t *timer_data, uint8_t timer_duration, uint16_t *ramp,
                  uint8_t ramp_start, uint8_t ramp_end, bool *ramp_direction, uint16_t ramp_step) {
  if (timer_elapsed(*timer_data) < timer_duration) return;
  *timer_data = timer_read();

  const uint16_t start_scaled = (uint16_t)ramp_start * RAMP_SCALE;
  const uint16_t end_scaled   = (uint16_t)ramp_end * RAMP_SCALE;

  if (*ramp_direction) {
    *ramp += ramp_step;
    if (*ramp >= end_scaled) {
      *ramp = end_scaled;
      *ramp_direction = false;
    }
  } else {
    if (*ramp <= start_scaled + ramp_step) {
      *ramp = start_scaled;
      *ramp_direction = true;
    } else {
      *ramp -= ramp_step;
    }
  }
}

// Step 10 (1.0) = one brightness level per tick → smooth (no 10-frame plateaus). Step 1 for layer only (kept as preferred).
void handle_timers(void) {
  handle_timer(&timer_danger.timer_data, 1, &timer_danger.ramp,
               timer_danger.ramp_start, timer_danger.ramp_end, &timer_danger.ramp_direction, 10);
  handle_timer(&timer_caps.timer_data, 1, &timer_caps.ramp,
               timer_caps.ramp_start, timer_caps.ramp_end, &timer_caps.ramp_direction, 10);
  handle_timer(&timer_layer_v.timer_data, 1, &timer_layer_v.ramp,
               timer_layer_v.ramp_start, timer_layer_v.ramp_end, &timer_layer_v.ramp_direction, 1);
  handle_timer(&timer_layer_s.timer_data, 1, &timer_layer_s.ramp,
               timer_layer_s.ramp_start, timer_layer_s.ramp_end, &timer_layer_s.ramp_direction, 1);
  handle_timer(&timer_muted_pulse.timer_data, 1, &timer_muted_pulse.ramp,
               timer_muted_pulse.ramp_start, timer_muted_pulse.ramp_end, &timer_muted_pulse.ramp_direction, 5);
}


/* Lighting logic
 * ------------------------------------------------------------------------- */

#define SPECIALTY_HUE_OFFSET 128
#define LATCHED_HUE_OFFSET 64

void light_keycode(uint8_t led_index, uint8_t col, uint8_t row, uint8_t layer) {
  const HSV hsv_default = rgb_matrix_get_hsv();
  const uint8_t base_hue = hsv_default.h;
  const uint8_t base_sat = hsv_default.s;
  const uint8_t base_val = hsv_default.v;
  const uint8_t specialty_hue = (base_hue + SPECIALTY_HUE_OFFSET) & 0xFF;
  const HSV hsv_danger = {0, 255, RAMP_TO_VAL(timer_danger.ramp)};
  const HSV hsv_mouse_btn = {12, 250, 120};
  const uint16_t keycode = KEYCODE_AT_LAYER(layer, col, row);

  HSV hsv = {0, 0, 0};

  latched_key_t* latched = find_latched_key(row, col);
  if (latched && latched->is_latched) {
    hsv = (HSV){(base_hue + LATCHED_HUE_OFFSET) & 0xFF, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
  }
  else if ((keycode >= KC_F1 && keycode <= KC_F24) || (keycode >= KC_1 && keycode <= KC_0)) {
    uint8_t index;
    if (keycode >= KC_F1 && keycode <= KC_F24) {
      index = (keycode <= KC_F12) ? (keycode - KC_F1) : ((keycode - KC_F13) + 12);
    } else {
      index = (keycode == KC_0) ? 9 : (keycode - KC_1);
    }

    const uint8_t GROUP = index / 3;
    const int8_t HUE_OFFSET = 11;
    const uint8_t SAT_DELTA = 9;
    const uint8_t VAL_BOOST_L = 10;
    const uint8_t VAL_BOOST_H = 64;
    const uint8_t VAL_CAP_L = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 64;
    const uint8_t VAL_CAP_H = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 30;

    uint8_t sat_low = (base_sat >= SAT_DELTA) ? (base_sat - SAT_DELTA) : base_val;
    uint8_t sat_high = (base_sat <= 255 - SAT_DELTA) ? (base_sat + SAT_DELTA) : 255;
    uint16_t boosted_low = (uint16_t)base_val + VAL_BOOST_L;
    uint16_t boosted_high = (uint16_t)base_val + VAL_BOOST_H;
    uint8_t val_low = boosted_low > VAL_CAP_L ? VAL_CAP_L : (uint8_t)boosted_low;
    uint8_t val_high = boosted_high > VAL_CAP_H ? VAL_CAP_H : (uint8_t)boosted_high;
    
    hsv = (HSV){(base_hue + HUE_OFFSET) % 256, (GROUP % 2) ? sat_high : sat_low, (GROUP % 2) ? val_high : val_low};
  }
  else if (keycode >= MS_BTN1 && keycode <= MS_BTN5) {
    hsv = hsv_mouse_btn;
  }
  else if (keycode == QK_BOOT || keycode == EE_CLR || keycode == CLR_RST || keycode == DB_TOGG) {
    hsv = hsv_danger;
  }
  else if (keycode == KC_TAB || keycode == KC_CAPS || keycode == CW_TOGG) {
    if (IS_CAPS_ON()) hsv = (HSV){specialty_hue, base_sat, base_val};
    else if (is_caps_word_on()) hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    else hsv = (keycode == KC_TAB) ? hsv_default : (HSV){specialty_hue, base_sat, base_val / 2};
  }
  else if (keycode == EXTEND) {
    if (extend_key_latched) hsv = (HSV){specialty_hue, base_sat, base_val};
    else if (extend_key_active) hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    else hsv = hsv_default;
  }
  else if (keycode == KC_TRNS) {
    uint16_t keycode_below = KEYCODE_AT_LAYER_BELOW(col, row);
    if (is_layer_changer(keycode_below)) {
      hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    } else {
      hsv = (HSV){rgb_matrix_get_hue(), rgb_matrix_get_sat(), RAMP_TO_VAL(timer_muted_pulse.ramp)};
    }
  }
  else if (keycode == KC_NO) {
    rgb_matrix_set_color(led_index, 0, 0, 0);
    return;
  }
  else {
    if (layer != 0) {
      hsv = hsv_default;
      hsv.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    }
    else return;
  }

  if (hsv.v > RGB_MATRIX_MAXIMUM_BRIGHTNESS) hsv.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
  RGB rgb = hsv_to_rgb(hsv);
  rgb_matrix_set_color(led_index, rgb.r, rgb.g, rgb.b);
}

void boot_light_effect(void) {
  if (boot_animation > boot_animation_stop) {
    boot_animation = 0;
    timer_boot_animation = timer_boot_animation_default;
    rgb_matrix_reload_from_eeprom();
  } else {
    bool prev_dir = timer_boot_animation.ramp_direction;
    handle_timer(&timer_boot_animation.timer_data, 1, &timer_boot_animation.ramp,
                 timer_boot_animation.ramp_start, timer_boot_animation.ramp_end,
                 &timer_boot_animation.ramp_direction, 10);
    if (prev_dir != timer_boot_animation.ramp_direction) {
      boot_animation++;
      if (boot_animation == 3) {
        timer_boot_animation = timer_boot_animation_default;
        timer_boot_animation.timer_data = timer_read();
        timer_boot_animation.ramp_end = rgb_matrix_get_val();
        timer_boot_animation.ramp = 0;
        timer_boot_animation.ramp_direction = true;
      }
    }

    const uint8_t h = rgb_matrix_get_hue();
    const uint8_t s = rgb_matrix_get_sat();
    const uint8_t v = RAMP_TO_VAL(timer_boot_animation.ramp);
    const HSV hsv_cat = {h, s, v};
    const RGB rgb_cat = hsv_to_rgb(hsv_cat);
    const RGB rgb_all = rgb_cat;
    const uint16_t sparkle_tick = timer_read() / 75;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
      for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        uint8_t led_index = g_led_config.matrix_co[row][col];
        if (led_index == NO_LED) continue;
        if (boot_animation == 3) {
          rgb_matrix_set_color(led_index, rgb_all.r, rgb_all.g, rgb_all.b);
        } else if (is_cat_boot_key(row, col)) {
          rgb_matrix_set_color(led_index, rgb_cat.r, rgb_cat.g, rgb_cat.b);
        } else {
          uint8_t noise = (uint8_t)(sparkle_tick + (row * 29) + (col * 53) + (led_index * 7));
          noise ^= (noise << 3);
          noise ^= (noise >> 5);
          if ((noise & BOOT_BG_SPARKLE_CHANCE_MASK) == 0) {
            uint8_t sparkle_v = BOOT_BG_SPARKLE_VAL_MIN + (noise % BOOT_BG_SPARKLE_VAL_RANGE);
            RGB rgb_bg = hsv_to_rgb((HSV){h, s, sparkle_v});
            rgb_matrix_set_color(led_index, rgb_bg.r, rgb_bg.g, rgb_bg.b);
          } else {
            rgb_matrix_set_color(led_index, 0, 0, 0);
          }
        }
      }
    }
  }
}


/* Scanning
 * ------------------------------------------------------------------------- */

void extended_key(
  uint16_t keycode, 
  keyrecord_t *record, 
  uint16_t keycode_ext,
  uint16_t keycode_ext_shift)
{
  if (record->event.pressed)
  {
    if (extend_key_active) {
      const uint16_t mods = get_mods();
      if (mods & MOD_MASK_SHIFT) {
        register_code(keycode_ext_shift);
      } else {
        register_code(keycode_ext);
      }
    } else {
      register_code(keycode);
    }
  } else {
    unregister_code(keycode);
    unregister_code(keycode_ext);
  }
}

bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
  uint8_t row = record->event.key.row;
  uint8_t col = record->event.key.col;
  uint8_t current_layer = get_highest_layer(layer_state);
  
  latched_key_t* tracked = find_latched_key(row, col);
  if (tracked && tracked->is_latched && !record->event.pressed) return false;
  
  if (!current_layer && !record->event.pressed) {
    for (uint8_t i = 0; i < MAX_LATCHED_KEYS; i++) {
      if (latched_keys[i].keycode && latched_keys[i].is_latched) {
        if (!(matrix_get_row(latched_keys[i].row) & (1 << latched_keys[i].col))) {
          unregister_code16(latched_keys[i].keycode);
          latched_keys[i].keycode = 0;
          num_latched_keys--;
        }
      }
    }
  }
  
  if (record->event.pressed) {
    track_key_press(row, col, keycode, current_layer);
  } else {
    unlatch_key(row, col);
  }

  uint16_t tmp_keycode = keycode;
  if ((keycode >= QK_MOD_TAP && keycode <= QK_MOD_TAP_MAX) || 
      (keycode >= QK_LAYER_TAP && keycode <= QK_LAYER_TAP_MAX)) {
    tmp_keycode &= 0xFF;
  }

  switch (tmp_keycode) {
    case KC_TAB:
      if (!record->event.pressed) {
        if (IS_CAPS_ON()) tap_code(KC_CAPS);
        else if (is_caps_word_on()) caps_word_off();
        else return true;
      }
      return !(IS_CAPS_ON() || is_caps_word_on());
    case KC_CAPS:
      if (record->event.pressed) {
        const uint8_t saved_mods = get_mods() & MOD_MASK_SHIFT;
        if (saved_mods) {
          del_mods(saved_mods);
          if (is_caps_word_on()) caps_word_off();
          register_code(KC_CAPS);
          add_mods(saved_mods);
        } else {
          IS_CAPS_ON() ? tap_code(KC_CAPS) : caps_word_toggle();
        }
      } else {
        unregister_code(KC_CAPS);
      }
      return false;
    case BSP_DEL:
      if (record->event.pressed) {
        const uint8_t saved_mods = get_mods() & MOD_MASK_SHIFT;
        if (saved_mods == MOD_MASK_SHIFT) {
          register_code(KC_DEL);
        } else if (saved_mods) {
          del_mods(saved_mods);
          register_code(KC_DEL);
          add_mods(saved_mods);
        } else {
          register_code(KC_BSPC);
        }
      } else {
        unregister_code(KC_DEL);
        unregister_code(KC_BSPC);
      }
      return false;
    case KC_LEFT:
      if (record->event.pressed)
      {
        if (get_mods() & MOD_MASK_SHIFT) register_code(KC_HOME);
        else register_code(KC_LEFT);
      } else {
        unregister_code(KC_HOME);
        unregister_code(KC_LEFT);
      }
      return false;
    case KC_RGHT:
      extended_key(keycode, record, KC_LEFT, KC_END);
      return false;
    case KC_DOWN:
      extended_key(keycode, record, KC_UP, KC_PGDN);
      return false;
    case KC_UP:
      if (record->event.pressed)
      {
        if (get_mods() & MOD_MASK_SHIFT) register_code(KC_PGUP);
        else register_code(KC_UP);
      } else {
        unregister_code(KC_PGUP);
        unregister_code(KC_UP);
      }
      return false;
    case EXTEND:
      if (record->event.pressed)
      {
        if (extend_key_latched) {
          extend_key_active = false;
          extend_key_latched = false;
        } else {
          extend_key_active = true;
          if (get_mods() & MOD_MASK_SHIFT) extend_key_latched = true;
        }
      } else {
        if (!extend_key_latched) extend_key_active = false;
      }
      return false;
    case LOWER_B:
      if (record->event.pressed) layer_on(_LOWER_B);
      else layer_off(_LOWER_B);
      return false;
    case LOWER:
      if (record->event.pressed) layer_on(_LOWER);
      else layer_off(_LOWER);
      update_tri_layer(_LOWER, _RAISE, _ADJUST);
      return false;
    case RAISE:
      if (record->event.pressed) layer_on(_RAISE);
      else layer_off(_RAISE);
      update_tri_layer(_LOWER, _RAISE, _ADJUST);
      return false;
    case RAISE_B:
      if (record->event.pressed) layer_on(_RAISE_B);
      else layer_off(_RAISE_B);
      return false;
    case CLR_RST:
      eeconfig_init();
      reset_keyboard();
      break;
  }

  return true;
}

layer_state_t layer_state_set_user(layer_state_t new_state) {
  static layer_state_t previous_state = 0;
  
  if (previous_state != new_state) {
    uint8_t prev_layer = previous_state ? get_highest_layer(previous_state) : 0;
    uint8_t new_layer = new_state ? get_highest_layer(new_state) : 0;
    
    if (prev_layer != new_layer) {
      latch_keys_from_layer(prev_layer);
      unlatch_keys_from_layer(new_layer);
    }
    
    for (uint8_t layer = 0; layer < _LAYERS_END; layer++) {
      if (!is_layer_active(previous_state, layer) && is_layer_active(new_state, layer)) {
        unlatch_keys_from_layer(layer);
      }
    }
    
    /* Entering default layer: new_state == 0 means no overlay; actual default layer index is get_highest_layer(default_layer_state). Clear all latched keys unconditionally. */
    if (!new_state && previous_state) {
      clear_all_latched_keys();
    }
  }
  
  previous_state = new_state;
  return new_state;
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
  if (boot_animation > 0) {
    boot_light_effect();
    return true;
  }

  const uint8_t layer = get_highest_layer(layer_state);
  for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
    for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
      uint8_t led_index = g_led_config.matrix_co[row][col];
      if (led_index >= led_min && led_index <= led_max && led_index != NO_LED) {
        light_keycode(led_index, col, row, layer);
      }
    }
  }

  handle_timers();
  return true;
}

void keyboard_post_init_user(void) {
  rgb_matrix_enable_noeeprom();
  rgb_matrix_set_speed_noeeprom(127);
  rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR); // RGB_MATRIX_BREATHING

  boot_animation = 1;
  timer_boot_animation = timer_boot_animation_default;
}
