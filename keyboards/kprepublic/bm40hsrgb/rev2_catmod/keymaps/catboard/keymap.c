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
  _ADJUST
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
  BSP_DEL,
  MREC1,
  MPLY1,
  MREC2,
  MPLY2,
  MRSTP
};

bool extend_key_active = false;
bool extend_key_latched = false;

// timer struct: ramp is 10x fixed-point (0.1 steps) for smooth slow pulses
#define RAMP_SCALE 10
#define RAMP_TO_VAL(r) ((uint8_t)((r) / RAMP_SCALE))

typedef struct {
  uint16_t timer_data;
  uint8_t ramp_start;
  uint8_t ramp_end;
  uint16_t ramp;
  bool ramp_direction;
} Timer_Ramp;

static const Timer_Ramp Timer_Ramp_default = {0, 100, RGB_MATRIX_MAXIMUM_BRIGHTNESS, 100 * RAMP_SCALE, true};
static Timer_Ramp timer_danger = Timer_Ramp_default;
static Timer_Ramp timer_caps   = Timer_Ramp_default;
static Timer_Ramp timer_macro = Timer_Ramp_default;
/* 0 = idle, 1 = slot 1, 2 = slot 2 */
static uint8_t macro_recording;
/* Resolved keycodes, not matrix positions. Press and release are separate events. */
#define MACRO_EVT 64
typedef struct __attribute__((packed)) {
  uint16_t code;
  uint8_t pressed;
} macro_evt_t;
static macro_evt_t macro_ev[2][MACRO_EVT];
static uint8_t macro_len[2];
static const Timer_Ramp timer_boot_animation_default = {0, 0, RGB_MATRIX_MAXIMUM_BRIGHTNESS, 0, true};
static Timer_Ramp timer_boot_animation = timer_boot_animation_default;
#define BOOT_BG_SPARKLE_CHANCE_MASK 0x07
#define BOOT_BG_SPARKLE_VAL_MIN 12
#define BOOT_BG_SPARKLE_VAL_RANGE 20

uint8_t boot_animation = 1;
#define BOOT_ANIMATION_STOP 3

/* Boot animation: "CAT" on the 4x12 grid (row 4 does not exist; T stem ends on row 3). */
static const uint16_t PROGMEM cat_boot_row_mask[MATRIX_ROWS] = {
  0x0EF7, 0x0491, 0x04F1, 0x0497
};

static bool is_cat_boot_key(uint8_t row, uint8_t col) {
  return row < MATRIX_ROWS && (pgm_read_word(&cat_boot_row_mask[row]) & (1u << col));
}

/* Key latching
 * Hold W, raise an overlay, release W or tap an ability: W stays down.
 * Swallow the original key-up (QMK would release HID). Never register_code()
 * a key that is already down - that retriggers (del+add) and feels like a tap.
 * Delayed unregister only when returning to the default layer with the switch up.
 * ------------------------------------------------------------------------- */

#define MAX_HELD_KEYS 12

typedef struct {
  uint8_t row;
  uint8_t col;
  uint16_t keycode;
  uint8_t source_layer;
  bool latched;
  bool physically_down;
} held_key_t;

static held_key_t held_keys[MAX_HELD_KEYS];

#define IS_CAPS_ON() (host_keyboard_led_state().caps_lock)
#define KEYCODE_AT_LAYER(layer, col, row) keymap_key_to_keycode((layer), (keypos_t){(col), (row)})
#define KEYCODE_AT_LAYER_BELOW(col, row) keymap_key_to_keycode(layer_switch_get_layer((keypos_t){(col), (row)}), (keypos_t){(col), (row)})
#define MATRIX_KEY_DOWN(r, c) ((matrix_get_row(r) & ((matrix_row_t)1 << (c))) != 0)

static bool is_layer_changer(uint16_t keycode) {
  if (keycode >= QK_MODS) {
    uint16_t base = keycode & 0xFF00;
    if (base == QK_MOMENTARY || base == QK_LAYER_TAP || base == QK_LAYER_MOD ||
        base == QK_TOGGLE_LAYER || base == QK_ONE_SHOT_LAYER || base == QK_TO ||
        base == QK_LAYER_TAP_TOGGLE) return true;
  }
  return (keycode >= LOWER && keycode <= RAISE_B);
}

/* Vial keeps the keymap in EEPROM, so these keys may still be the old QMK
 * codes or the keymap's own copies. Treat them as the same keys.
 * 1/2 record, 3/4 play, 5 stop. */
static uint8_t macro_key_kind(uint16_t keycode) {
  switch (keycode) {
    case DM_REC1: case MREC1: return 1;
    case DM_REC2: case MREC2: return 2;
    case DM_PLY1: case MPLY1: return 3;
    case DM_PLY2: case MPLY2: return 4;
    case DM_RSTP: case MRSTP: return 5;
  }
  return 0;
}

static bool is_self_managed(uint16_t keycode) {
  if (keycode >= QK_MOD_TAP && keycode <= QK_MOD_TAP_MAX) return true;
  if (macro_key_kind(keycode)) return true;
  switch (keycode) {
    case KC_TAB:
    case KC_CAPS:
    case BSP_DEL:
    case EXTEND:
      return true;
  }
  return is_layer_changer(keycode);
}

static held_key_t* find_held_key(uint8_t row, uint8_t col) {
  for (uint8_t i = 0; i < MAX_HELD_KEYS; i++) {
    if (held_keys[i].keycode && held_keys[i].row == row && held_keys[i].col == col) {
      return &held_keys[i];
    }
  }
  return NULL;
}

static void track_key_press(uint8_t row, uint8_t col, uint16_t keycode, uint8_t source_layer) {
  if (keycode == KC_NO || keycode == KC_TRNS || is_self_managed(keycode)) return;
  if (find_held_key(row, col)) return;

  for (uint8_t i = 0; i < MAX_HELD_KEYS; i++) {
    if (held_keys[i].keycode == 0) {
      held_keys[i] = (held_key_t){row, col, keycode, source_layer, false, true};
      return;
    }
  }
}

static void restore_latched_hid(void) {
  bool dirty = false;
  for (uint8_t i = 0; i < MAX_HELD_KEYS; i++) {
    uint16_t kc = held_keys[i].keycode;
    if (!kc || !IS_BASIC_KEYCODE(kc)) continue;
    if (!held_keys[i].latched && !held_keys[i].physically_down) continue;
    if (!is_key_pressed((uint8_t)kc)) {
      add_key((uint8_t)kc);
      dirty = true;
    }
  }
  if (dirty) send_keyboard_report();
}

/* Overlay still active and this key was pressed on a different layer: keep HID. */
static bool process_latching(uint16_t keycode, keyrecord_t *record) {
  const uint8_t row = record->event.key.row;
  const uint8_t col = record->event.key.col;
  const uint8_t layer = get_highest_layer(layer_state);

  if (record->event.pressed) {
    held_key_t *h = find_held_key(row, col);
    if (h) h->physically_down = true;
    else track_key_press(row, col, keycode, layer);
    return true;
  }

  held_key_t *h = find_held_key(row, col);
  if (!h) return true;

  if (h->latched && keycode != h->keycode) {
    h->physically_down = false;
    return true;
  }

  if (layer != 0 && h->source_layer != layer) {
    h->latched = true;
    h->physically_down = false;
    return false;
  }

  h->keycode = 0;
  return true;
}

static void unlatch_keys_for_state(layer_state_t new_state) {
  const uint8_t new_layer = get_highest_layer(new_state);
  for (uint8_t i = 0; i < MAX_HELD_KEYS; i++) {
    held_key_t *h = &held_keys[i];
    if (!h->keycode) continue;
    if (h->source_layer != new_layer && new_state != 0) continue;
    if (!h->latched) continue;

    if (!MATRIX_KEY_DOWN(h->row, h->col)) {
      unregister_code16(h->keycode);
      h->keycode = 0;
    } else {
      h->latched = false;
    }
  }
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
   * |LShift|  Z   |  X   |  C   |  V   |  B   |  N   |  M   |  ,<  |  .>  |  Up  | /Sft |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | LCtrl| Super| Alt  |LowerB| Lower| Space       | Raise|RaiseB| Ext  | Down | Right|
   * `-----------------------------------------------------------------------------------'
   */
  [_QUIRKY] = LAYOUT_ortho_4x12_1x2uC(
    KC_ESC , KC_Q   , KC_W   , KC_E   , KC_R   , KC_T   , KC_Y   , KC_U   , KC_I   , KC_O   , KC_P   , KC_GRV ,
    KC_TAB , KC_A   , KC_S   , KC_D   , KC_F   , KC_G   , KC_H   , KC_J   , KC_K   , KC_L   , KC_SCLN, KC_QUOT,
    KC_LSFT, KC_Z   , KC_X   , KC_C   , KC_V   , KC_B   , KC_N   , KC_M   , KC_COMM, KC_DOT , KC_UP  , RSFT_T(KC_SLSH),
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

  /* Raise 2 - mouse. Right-hand symbols match Raise.
   * ,-----------------------------------------------------------------------------------.
   * | Acl2 | Btn4 | WhlU | Btn5 |  $   |  %   |  ^   |  &   |  *   |  (   |  )   | Ins  |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | Acl1 | WhlL | WhlD | WhlR |      |      |      |  _   |  +   |  [   |  ]   |  |   |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | Acl0 | Btn1 | MsUp | Btn2 |      |      |      |      |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | Btn3 | MsLf | MsDn | MsRt | .... | Bsp/Del     | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
   [_RAISE_B] = LAYOUT_ortho_4x12_1x2uC(
    MS_ACL2, MS_BTN4, MS_WHLU, MS_BTN5, KC_DLR , KC_PERC, KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_INS ,
    MS_ACL1, MS_WHLL, MS_WHLD, MS_WHLR, XXXXXXX, XXXXXXX, XXXXXXX, KC_UNDS, KC_PLUS, KC_LBRC, KC_RBRC, KC_PIPE,
    MS_ACL0, MS_BTN1, MS_UP  , MS_BTN2, XXXXXXX, XXXXXXX, XXXXXXX, _______, _______, KC_MRWD, KC_VOLU, KC_MFFD,
    MS_BTN3, MS_LEFT, MS_DOWN, MS_RGHT, _______, BSP_DEL         , _______, _______, KC_MPRV, KC_VOLD, KC_MNXT
  ),

  /* Adjust (Lower + Raise)
   * ,-----------------------------------------------------------------------------------.
   * | BOOT |DBTOGG|      |      |      |      | RGB_A| MOD+ | SPD+ | HUE+ | SAT+ | VAL+ |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |CLRRST|      |      |      |      |      | RGB_B| MOD- | SPD- | HUE- | SAT- | VAL- |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * | CLR  |      |      |      | Rec1 | Ply1 | Rec2 | Ply2 |      |      |      |      |
   * |------+------+------+------+------+------+------+------+------+------+------+------|
   * |      |      |      |      | .... | Stop        | .... |      |      |      |      |
   * `-----------------------------------------------------------------------------------'
   */
  [_ADJUST] = LAYOUT_ortho_4x12_1x2uC(
    QK_BOOT, DB_TOGG, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, RM_TOGG, RM_NEXT, RM_SPDU, RM_HUEU, RM_SATU, RM_VALU,
    CLR_RST, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, UG_TOGG, RM_PREV, RM_SPDD, RM_HUED, RM_SATD, RM_VALD,
    EE_CLR , XXXXXXX, XXXXXXX, XXXXXXX, DM_REC1, DM_PLY1, DM_REC2, DM_PLY2, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, DM_RSTP         , _______, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX
  )
};


/* Extend substitutes
 * Ext is a modifier. While it is held or latched, a key with this function
 * sends `sub` instead, whichever layer produced the key. With Shift held,
 * `shifted` is sent when it is not KC_NO; otherwise `sub` is sent and Shift
 * stays held, so Ctrl+Left selects the word.
 * Word delete is Ext+Space (Ctrl+Backspace, previous word) and
 * Ext+Shift+Space (Ctrl+Delete, next word). Space is the other thumb.
 * Keys absent from this list are unchanged.
 * ------------------------------------------------------------------------- */

typedef struct {
  uint16_t key;
  uint16_t sub;
  uint16_t shifted;
} extend_sub_t;

static const extend_sub_t extend_subs[] PROGMEM = {
  {KC_LEFT, C(KC_LEFT), KC_NO  },
  {KC_RGHT, C(KC_RGHT), KC_NO  },
  {KC_UP,   KC_PGUP,    KC_HOME},
  {KC_DOWN, KC_PGDN,    KC_END },
  {KC_SPC,  C(KC_BSPC), C(KC_DEL)},
};

static uint16_t extend_fired[MATRIX_ROWS][MATRIX_COLS];
static bool extend_claimed[MATRIX_ROWS][MATRIX_COLS];
static uint8_t extend_shift_stripped[MATRIX_ROWS][MATRIX_COLS];
static uint8_t shift_keys_down;

static void track_shift_key(uint16_t keycode, keyrecord_t *record) {
  uint8_t bit = 0;
  if (keycode == KC_LSFT) bit = MOD_BIT(KC_LSFT);
  else if (keycode == KC_RSFT || keycode == RSFT_T(KC_SLSH)) bit = MOD_BIT(KC_RSFT);
  if (!bit) return;
  if (record->event.pressed) shift_keys_down |= bit;
  else shift_keys_down &= ~bit;
}

static uint16_t extend_lookup(uint16_t keycode) {
  uint16_t key = keycode;
  if ((keycode >= QK_MOD_TAP && keycode <= QK_MOD_TAP_MAX) ||
      (keycode >= QK_LAYER_TAP && keycode <= QK_LAYER_TAP_MAX)) {
    key &= 0xFF;
  }

  for (uint8_t i = 0; i < sizeof(extend_subs) / sizeof(extend_subs[0]); i++) {
    if (pgm_read_word(&extend_subs[i].key) != key) continue;
    if (get_mods() & MOD_MASK_SHIFT) {
      uint16_t shifted = pgm_read_word(&extend_subs[i].shifted);
      if (shifted != KC_NO) return shifted;
    }
    return pgm_read_word(&extend_subs[i].sub);
  }
  return KC_NO;
}


/* Timers
 * ------------------------------------------------------------------------- */

static void handle_timer(Timer_Ramp *t, uint8_t duration, uint16_t ramp_step) {
  if (timer_elapsed(t->timer_data) < duration) return;
  t->timer_data = timer_read();

  const uint16_t start_scaled = (uint16_t)t->ramp_start * RAMP_SCALE;
  const uint16_t end_scaled   = (uint16_t)t->ramp_end * RAMP_SCALE;

  if (t->ramp_direction) {
    t->ramp += ramp_step;
    if (t->ramp >= end_scaled) {
      t->ramp = end_scaled;
      t->ramp_direction = false;
    }
  } else if (t->ramp <= start_scaled + ramp_step) {
    t->ramp = start_scaled;
    t->ramp_direction = true;
  } else {
    t->ramp -= ramp_step;
  }
}

static void handle_timers(void) {
  handle_timer(&timer_danger, 1, 10);
  handle_timer(&timer_caps, 1, 10);
  if (macro_recording) handle_timer(&timer_macro, 1, 10);
}


/* Lighting logic
 * ------------------------------------------------------------------------- */

#define SPECIALTY_HUE_OFFSET 128
#define LATCHED_HUE_OFFSET 64
#define TRANSPARENT_VAL 1

void light_keycode(uint8_t led_index, uint8_t col, uint8_t row, uint8_t layer) {
  const HSV hsv_default = rgb_matrix_get_hsv();
  const uint8_t base_hue = hsv_default.h;
  const uint8_t base_sat = hsv_default.s;
  const uint8_t base_val = hsv_default.v;
  const uint8_t specialty_hue = (base_hue + SPECIALTY_HUE_OFFSET) & 0xFF;
  const HSV hsv_danger = {0, 255, RAMP_TO_VAL(timer_danger.ramp)};
  const HSV hsv_mouse_btn = {12, 250, 120};
  uint16_t keycode = KEYCODE_AT_LAYER(layer, col, row);
  bool extend_lit = false;
  if (extend_key_active && keycode != EXTEND) {
    uint16_t effective = (keycode == KC_TRNS) ? KEYCODE_AT_LAYER_BELOW(col, row) : keycode;
    uint16_t sub = extend_lookup(effective);
    if (sub != KC_NO) {
      keycode = sub;
      extend_lit = true;
    }
  }

  HSV hsv = {0, 0, 0};

  /* Rec/Stop live on Adjust. Play/other Rec are dead while recording. */
  if (macro_recording) {
    uint16_t adj = KEYCODE_AT_LAYER(_ADJUST, col, row);
    uint8_t adj_kind = macro_key_kind(adj);
    if ((macro_recording == 1 && adj_kind == 1) ||
        (macro_recording == 2 && adj_kind == 2)) {
      hsv = (HSV){0, 255, RGB_MATRIX_MAXIMUM_BRIGHTNESS};
    } else if (adj_kind == 5) {
      hsv = (HSV){0, 255, RAMP_TO_VAL(timer_macro.ramp)};
    } else if (layer != 0 && adj_kind >= 1 && adj_kind <= 4) {
      rgb_matrix_set_color(led_index, 0, 0, 0);
      return;
    }
    if (hsv.s) {
      RGB rgb = hsv_to_rgb(hsv);
      rgb_matrix_set_color(led_index, rgb.r, rgb.g, rgb.b);
      return;
    }
  }

  bool is_transparent = false;
  if (keycode == KC_TRNS) {
    keycode = KEYCODE_AT_LAYER_BELOW(col, row);
    is_transparent = true;
  }

  held_key_t* held = find_held_key(row, col);
  if (held && (held->latched || held->source_layer != layer)) {
    hsv = (HSV){(base_hue + LATCHED_HUE_OFFSET) & 0xFF, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    is_transparent = false;
  }
  /* F1-F12 and F13-F24 are separate ranges. Arrows sit between F12 and F13. */
  else if ((keycode >= KC_F1 && keycode <= KC_F12) || (keycode >= KC_F13 && keycode <= KC_F24) || (keycode >= KC_1 && keycode <= KC_0)) {
    uint8_t index;
    if (keycode >= KC_F1 && keycode <= KC_F12) index = keycode - KC_F1;
    else if (keycode >= KC_F13 && keycode <= KC_F24) index = (keycode - KC_F13) + 12;
    else {
      index = (keycode == KC_0) ? 9 : (keycode - KC_1);
    }

    const uint8_t GROUP_ODD = (index / 3) & 1;
    const uint8_t HUE_A = 10;
    const uint8_t HUE_B = 22;
    const uint8_t SAT_DELTA = 28;
    const uint8_t VAL_BOOST_L = 8;
    const uint8_t VAL_BOOST_H = 56;
    const uint8_t VAL_CAP_L = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 64;
    const uint8_t VAL_CAP_H = RGB_MATRIX_MAXIMUM_BRIGHTNESS - 24;

    uint8_t sat_low = (base_sat > SAT_DELTA) ? (base_sat - SAT_DELTA) : 0;
    uint8_t sat_high = (base_sat < 255 - SAT_DELTA) ? (base_sat + SAT_DELTA) : 255;
    uint16_t boosted_low = (uint16_t)base_val + VAL_BOOST_L;
    uint16_t boosted_high = (uint16_t)base_val + VAL_BOOST_H;
    uint8_t val_low = boosted_low > VAL_CAP_L ? VAL_CAP_L : (uint8_t)boosted_low;
    uint8_t val_high = boosted_high > VAL_CAP_H ? VAL_CAP_H : (uint8_t)boosted_high;

    hsv = (HSV){(base_hue + (GROUP_ODD ? HUE_B : HUE_A)) & 0xFF, GROUP_ODD ? sat_high : sat_low, GROUP_ODD ? val_high : val_low};
  }
  else if (keycode >= MS_BTN1 && keycode <= MS_BTN5) {
    hsv = hsv_mouse_btn;
  }
  else if (keycode == QK_BOOT || keycode == EE_CLR || keycode == CLR_RST || keycode == DB_TOGG) {
    hsv = hsv_danger;
  }
  else if (macro_key_kind(keycode) == 1 || macro_key_kind(keycode) == 2) {
    hsv = (HSV){0, 255, RGB_MATRIX_MAXIMUM_BRIGHTNESS};
  }
  else if (macro_key_kind(keycode) == 3 || macro_key_kind(keycode) == 4) {
    uint8_t play_sat = base_sat < 160 ? 160 : base_sat;
    hsv = (HSV){(base_hue + 85) & 0xFF, play_sat, RGB_MATRIX_MAXIMUM_BRIGHTNESS};
  }
  else if (keycode == KC_TAB || keycode == KC_CAPS || keycode == CW_TOGG) {
    if (IS_CAPS_ON()) hsv = (HSV){specialty_hue, base_sat, base_val};
    else if (is_caps_word_on()) hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    else if (layer == 0 && !is_transparent) return;
    else hsv = (keycode == KC_TAB) ? hsv_default : (HSV){specialty_hue, base_sat, base_val / 2};
  }
  else if (keycode == EXTEND) {
    if (extend_key_latched) hsv = (HSV){specialty_hue, base_sat, base_val};
    else if (extend_key_active) hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
    else if (is_transparent) hsv = hsv_default;
    else return;
  }
  else if (is_layer_changer(keycode)) {
    if (layer == 0) return;
    hsv = (HSV){specialty_hue, base_sat, RAMP_TO_VAL(timer_caps.ramp)};
  }
  else if (keycode == KC_NO || macro_key_kind(keycode) == 5) {
    rgb_matrix_set_color(led_index, 0, 0, 0);
    return;
  }
  else {
    if (layer != 0 || extend_lit) {
      hsv = hsv_default;
      hsv.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    }
    else return;
  }

  if (is_transparent && !is_layer_changer(keycode)) hsv.v = TRANSPARENT_VAL;
  if (hsv.v > RGB_MATRIX_MAXIMUM_BRIGHTNESS) hsv.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
  RGB rgb = hsv_to_rgb(hsv);
  rgb_matrix_set_color(led_index, rgb.r, rgb.g, rgb.b);
}

void boot_light_effect(void) {
  if (boot_animation > BOOT_ANIMATION_STOP) {
    boot_animation = 0;
    timer_boot_animation = timer_boot_animation_default;
    rgb_matrix_reload_from_eeprom();
  } else {
    bool prev_dir = timer_boot_animation.ramp_direction;
    handle_timer(&timer_boot_animation, 1, 10);
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

static bool momentary_layer(uint8_t layer, keyrecord_t *record, bool tri) {
  if (record->event.pressed) layer_on(layer);
  else layer_off(layer);
  if (tri) update_tri_layer(_LOWER, _RAISE, _ADJUST);
  return false;
}

static bool process_keycode(uint16_t keycode, keyrecord_t *record) {
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
      return momentary_layer(_LOWER_B, record, false);
    case LOWER:
      return momentary_layer(_LOWER, record, true);
    case RAISE:
      return momentary_layer(_RAISE, record, true);
    case RAISE_B:
      return momentary_layer(_RAISE_B, record, false);
    case CLR_RST:
      eeconfig_init();
      reset_keyboard();
      break;
  }

  return true;
}

static void emit_extend_sub(uint16_t keycode, keyrecord_t *record) {
  if (!process_keycode(keycode, record)) return;
  process_action(record, action_for_keycode(keycode));
}

static void macro_note(uint16_t keycode, bool pressed);
static void macro_stop(void);

/* Returns true when this event was an extend substitute (caller must not also send the layer key). */
static bool extend_handle(uint16_t keycode, keyrecord_t *record) {
  const uint8_t row = record->event.key.row;
  const uint8_t col = record->event.key.col;

  if (!record->event.pressed) {
    if (!extend_claimed[row][col]) return false;
    uint16_t fired = extend_fired[row][col];
    uint8_t stripped = extend_shift_stripped[row][col];
    extend_claimed[row][col] = false;
    extend_fired[row][col] = KC_NO;
    extend_shift_stripped[row][col] = 0;
    if (fired != KC_NO) {
      macro_note(fired, false);
      emit_extend_sub(fired, record);
    }
    if (stripped && shift_keys_down) {
      add_mods(shift_keys_down);
      send_keyboard_report();
    }
    return true;
  }

  if (keycode == EXTEND || !extend_key_active) return false;

  uint16_t sub = extend_lookup(keycode);
  if (sub == KC_NO) return false;

  extend_claimed[row][col] = true;
  extend_fired[row][col] = sub;
  /* Ctrl+Shift+Delete is a browser shortcut, so word-delete drops Shift while it is held. */
  if (keycode == KC_SPC) {
    uint8_t shift = get_mods() & MOD_MASK_SHIFT;
    extend_shift_stripped[row][col] = shift;
    if (shift) del_mods(shift);
  }
  if (sub != KC_NO) {
    macro_note(sub, true);
    emit_extend_sub(sub, record);
  }
  return true;
}

/* QMK stores matrix positions and replays them against the current layer.
 * Store the keycode that was actually live instead, including keys reached
 * through Lower/Raise. Layer chords themselves are not part of the macro. */
static void macro_note(uint16_t keycode, bool pressed) {
  if (!macro_recording) return;
  if (keycode == KC_NO || keycode == KC_TRNS || keycode == EXTEND || macro_key_kind(keycode)) return;
  if (is_layer_changer(keycode)) return;

  uint8_t slot = macro_recording - 1;
  if (macro_len[slot] >= MACRO_EVT) {
    macro_stop();
    return;
  }
  macro_ev[slot][macro_len[slot]++] = (macro_evt_t){keycode, pressed};
}

static void macro_replay(uint16_t keycode, bool pressed) {
  keyrecord_t rec = {0};
  rec.event.pressed = pressed;
  if (!process_keycode(keycode, &rec)) return;
  process_action(&rec, action_for_keycode(keycode));
}

static void macro_start(uint8_t slot) {
  macro_recording = slot;
  macro_len[slot - 1] = 0;
  timer_macro = Timer_Ramp_default;
}

static void macro_stop(void) {
  macro_recording = 0;
}

static void macro_play(uint8_t slot) {
  for (uint8_t i = 0; i < macro_len[slot]; i++) {
    macro_replay(macro_ev[slot][i].code, macro_ev[slot][i].pressed != 0);
  }
}

/* Record starts on release so the Rec key itself is not stored. */
static bool process_macro_key(uint16_t keycode, keyrecord_t *record) {
  switch (macro_key_kind(keycode)) {
    case 1:
    case 2:
      if (!macro_recording && !record->event.pressed) macro_start(macro_key_kind(keycode));
      return true;
    case 3:
    case 4:
      if (!macro_recording && !record->event.pressed) macro_play(macro_key_kind(keycode) - 3);
      return true;
    case 5:
      if (macro_recording && record->event.pressed) macro_stop();
      return true;
  }
  return false;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
  (void)record;
  return keycode == RSFT_T(KC_SLSH);
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
  track_shift_key(keycode, record);
  if (macro_recording) {
    uint8_t kind = macro_key_kind(keycode);
    if (kind >= 1 && kind <= 4) return false;
  }
  /* Extend owns these keys. Latching them would swallow the release that
   * unregisters the substitute. */
  const uint8_t row = record->event.key.row;
  const uint8_t col = record->event.key.col;
  if (!record->event.pressed) {
    if (extend_claimed[row][col]) return true;
  } else if (extend_key_active && keycode != EXTEND && extend_lookup(keycode) != KC_NO) {
    return true;
  }
  return process_latching(keycode, record);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
  if (extend_handle(keycode, record)) return false;
  if (process_macro_key(keycode, record)) return false;
  macro_note(keycode, record->event.pressed);
  return process_keycode(keycode, record);
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
  (void)keycode;
  if (record->event.pressed) restore_latched_hid();
}

layer_state_t layer_state_set_user(layer_state_t new_state) {
  unlatch_keys_for_state(new_state);
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
