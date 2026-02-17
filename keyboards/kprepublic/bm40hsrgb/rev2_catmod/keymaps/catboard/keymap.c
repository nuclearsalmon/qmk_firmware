#include QMK_KEYBOARD_H
#include <color.h>
//#include "print.h"


/* Constants
 * ------------------------------------------------------------------------- */

// layers
enum layers {
  _QUIRKY,
  _LOWER,
  _LOWER2,
  _RAISE,
  _RAISE2,
  _ADJUST,
  _LAYERS_END
};

// keycodes
enum custom_keycodes {
  QUIRKY = SAFE_RANGE,
  LOWER,
  LOWER2,
  RAISE,
  RAISE2,
  EXTEND,
  CLR_RST,
};

// timer struct
struct Timer_Ramp_t {
  uint16_t timer_data;
  const uint8_t ramp_start;
  const uint8_t ramp_end;
  uint8_t ramp;
  bool ramp_direction;
} const Timer_Ramp_default = {0, 100, RGBLIGHT_LIMIT_VAL, 100, true};
typedef struct Timer_Ramp_t Timer_Ramp;

// timers
Timer_Ramp timer_danger = Timer_Ramp_default;
Timer_Ramp timer_caps   = Timer_Ramp_default;
Timer_Ramp timer_layer_v  = Timer_Ramp_default;
Timer_Ramp timer_layer_s  = {0, 140, 210, 140, true};
const Timer_Ramp timer_boot_animation_default = {0, 0, 127, 0, true};
Timer_Ramp timer_boot_animation = timer_boot_animation_default;

uint8_t boot_animation = 1;
const uint8_t boot_animation_stop = 2;


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
   * | LCtrl| Super| Alt  | Enter| Lower| Space       | Raise|RaiseB| Ext  | Down | Right|
   * `-----------------------------------------------------------------------------------'
   */
  [_QUIRKY] = LAYOUT_ortho_4x12_1x2uC(
    KC_ESC , KC_Q   , KC_W   , KC_E    , KC_R   , KC_T   , KC_Y   , KC_U   , KC_I   , KC_O   , KC_P   , KC_GRV ,
    KC_TAB , KC_A   , KC_S   , KC_D    , KC_F   , KC_G   , KC_H   , KC_J   , KC_K   , KC_L   , KC_SCLN, KC_QUOT,
    KC_LSFT, KC_Z   , KC_X   , KC_C    , KC_V   , KC_B   , KC_N   , KC_M   , KC_COMM, KC_DOT , KC_SLSH, KC_RSFT,
    KC_LCTL, KC_LGUI, KC_LALT, LOWER_B , LOWER  , KC_SPC          , RAISE  , RAISE_B, EXTEND , KC_DOWN, KC_RGHT
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
    _______, KC_7   , KC_8   , KC_9   , KC_0   , KC_VOLU, KC_VOLD, XXXXXXX, XXXXXXX, KC_WBAK, KC_PGUP, KC_WFWD,
    _______, _______, _______, XXXXXXX, _______, KC_ENT          , _______, XXXXXXX, KC_HOME, KC_PGDN, KC_END
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
   [_LOWER2] = LAYOUT_ortho_4x12_1x2uC(
    XXXXXXX, KC_F1  , KC_F2  , KC_F3  , KC_F10 , KC_F11 , KC_F12 , KC_F19 , KC_F20 , KC_F21 , XXXXXXX, XXXXXXX,
    XXXXXXX, KC_F4  , KC_F5  , KC_F6  , KC_F13 , KC_F14 , KC_F15 , KC_MINS, KC_EQL , KC_LCBR, KC_RCBR, KC_BSLS,
    _______, KC_F7  , KC_F8  , KC_F9  , KC_F16 , KC_F17 , KC_F18 , KC_F22 , KC_F23 , KC_F24 , XXXXXXX, _______
    _______, _______, _______, _______, XXXXXXX, XXXXXXX         , XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX
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
   [_RAISE2] = LAYOUT_ortho_4x12_1x2uC(
    KC_TILD, KC_EXLM, KC_AT  , KC_HASH, KC_DLR , KC_PERC, KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_INS ,
    KC_CAPS, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_UNDS, KC_PLUS, KC_LBRC, KC_RBRC, KC_PIPE,
    _______, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, _______, KC_MRWD, KC_VOLU, KC_MFFD,
    _______, _______, _______, XXXXXXX, _______, BSP_DEL         , _______, _______, KC_MPRV, KC_VOLD, KC_MNXT
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
    QK_BOOT, DB_TOGG, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, RGB_TOG, RGB_MOD , RGB_SPI, RGB_HUI, RGB_SAI, RGB_VAI,
    CLR_RST, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, RGB_M_P, RGB_RMOD, RGB_SPD, RGB_HUD, RGB_SAD, RGB_VAD,
    EE_CLR , XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX , XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, _______, XXXXXXX         , _______ , XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX
  ),
  
  /* Special (Special key)
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
  [_SPECIAL] = LAYOUT_ortho_4x12_1x2uC(
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, KC_ACL2, KC_BTN4, KC_WH_U, KC_BTN5,
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, DM_REC1, DM_REC2, XXXXXXX, KC_ACL1, KC_WH_L, KC_WH_D, KC_WH_R,
    XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, DM_PLY1, DM_PLY2, XXXXXXX, KC_ACL0, KC_BTN1, KC_MS_U, KC_BTN2,
    XXXXXXX, XXXXXXX, XXXXXXX, _______, XXXXXXX, DM_RSTP     , XXXXXXX, KC_BTN3, KC_MS_L, KC_MS_D, KC_MS_R
  ),  
};


/* Resolve keys
 * ------------------------------------------------------------------------- */

#define IS_CAPS_ON() (host_keyboard_led_state().caps_lock)

#define KEYCODE_AT_LAYER(layer, col, row) \
  keymap_key_to_keycode((layer), (keypos_t){(col), (row)})

#define KEYCODE_AT_LAYER_BELOW(col, row) \
  keymap_key_to_keycode( \
    layer_switch_get_layer((keypos_t){(col), (row)}), \
    (keypos_t){(col), (row)} \
  )

static bool is_layer_changer(uint16_t kc) {
  // built-in layer functions; covers MO(0–31), LT(0–31,…), LM, TT, TG, OSL, TO/DF
  if (kc >= QK_MODS) {
      if (kc >= QK_MOMENTARY    && kc <  QK_MOMENTARY    + 0x100) return true;
      if (kc >= QK_LAYER_TAP    && kc <  QK_LAYER_TAP    + 0x100) return true;
      if (kc >= QK_LAYER_MOD    && kc <  QK_LAYER_MOD    + 0x100) return true;
      if (kc >= QK_TOGGLE_LAYER && kc <  QK_TOGGLE_LAYER + 0x100) return true;
      if (kc >= QK_ONE_SHOT_LAYER && kc < QK_ONE_SHOT_LAYER + 0x100) return true;
      if (kc >= QK_TO           && kc <  QK_TO           + 0x100) return true;
      if (kc >= QK_LAYER_TAP_TOGGLE && kc < QK_LAYER_TAP_TOGGLE + 0x100) return true;
  }

  // custom layer keycodes
  switch(kc) {
    case LOWER:
    case LOWER2:
    case RAISE:
    case RAISE2:
    case EXTEND:
      return true;
  }

  return false;
}

/* Timers
 * ------------------------------------------------------------------------- */

void handle_timer(
  uint16_t *timer_data,
  uint8_t timer_duration,
  uint8_t *ramp,
  uint8_t ramp_start,
  uint8_t ramp_end,
  bool *ramp_direction,
  uint8_t ramp_step
)
{
  if (timer_elapsed(*timer_data) > timer_duration) {
    *ramp += *ramp_direction ? ramp_step : -ramp_step;
    if (*ramp <= ramp_start ||
      *ramp >= ramp_end) {
      *ramp_direction = !*ramp_direction;
    }
    *timer_data = timer_read();
  }
}

void handle_timers(void) {
  handle_timer(
    &timer_danger.timer_data, 10,
    &timer_danger.ramp,
    timer_danger.ramp_start, timer_danger.ramp_end,
    &timer_danger.ramp_direction, 1
  );
  handle_timer(
    &timer_caps.timer_data, 1,
    &timer_caps.ramp,
    timer_caps.ramp_start, timer_caps.ramp_end,
    &timer_caps.ramp_direction, 1
  );
  handle_timer(
    &timer_layer_v.timer_data, 10,
    &timer_layer_v.ramp,
    timer_layer_v.ramp_start, timer_layer_v.ramp_end,
    &timer_layer_v.ramp_direction, 1
  );
  handle_timer(
    &timer_layer_s.timer_data, 5,
    &timer_layer_s.ramp,
    timer_layer_s.ramp_start, timer_layer_s.ramp_end,
    &timer_layer_s.ramp_direction, 1
  );
}


/* Lighting logic
 * ------------------------------------------------------------------------- */

// Unique color for keycode
void light_keycode(const uint8_t led_index,
           const uint8_t col,
           const uint8_t row,
           const uint8_t layer)
{
  // Color definitions
  const HSV hsv_default = rgb_matrix_get_hsv();
  const HSV hsv_fkey = {12, 250, 120};//const HSV hsv_fkey = {31, 211, 120};
  const HSV hsv_num = {122, 205, 120};
  const HSV hsv_danger = {0, 255, timer_danger.ramp};
  const HSV hsv_caps_inactive = {248, 255, 91};
  const HSV hsv_caps_word_active = {248, 255, timer_caps.ramp}; //231, 163};
  const HSV hsv_caps_active = {248, 255, 180};
  const HSV hsv_mouse_btn = {12, 250, 120};

  // Read keycode from position
  const uint16_t keycode = KEYCODE_AT_LAYER(layer, col, row);

  HSV hsv = {0, 0, 0};
  if ((keycode >= KC_F1 && keycode <= KC_F12) || (keycode >= KC_F13 && keycode <= KC_F24)) {
    hsv = hsv_fkey;
  }
  else if (keycode == QK_BOOT || keycode == EE_CLR
    || keycode == CLR_RST || keycode == DB_TOGG) {
    hsv = hsv_danger;
  }
  else if (keycode == KC_TAB || keycode == KC_CAPS || keycode == CW_TOGG) {
    if (IS_CAPS_ON()) { 
      hsv = hsv_caps_active;
    }
    else if (is_caps_word_on()) {
      hsv = hsv_caps_word_active;
    } else {
      if (keycode != KC_TAB) {
        hsv = hsv_caps_inactive;
      } else {
        hsv = hsv_default;
      }
    }
  }
  else if ((keycode >= KC_1 && keycode <= KC_0) || 
    (keycode >= KC_KP_SLASH && keycode <= KC_KP_DOT) || 
    keycode == KC_KP_EQUAL || keycode == KC_KP_COMMA || keycode == KC_KP_EQUAL_AS400) {
    hsv = hsv_num;
  }
  else if (keycode >= KC_BTN1 && keycode <= KC_BTN5) {
    hsv = hsv_mouse_btn;
  }
  else if (keycode == KC_TRNS) {
    // Resolve key below
    uint16_t keycode_below = KEYCODE_AT_LAYER_BELOW(col, row);

    // Check if key below is a layer shift key
    if ((keycode_below == LOWER || keycode_below == RAISE) || keycode_below == MO(_SPECIAL)) {
      // Brighten default board color
      hsv = (HSV){rgb_matrix_get_hue(), rgb_matrix_get_sat(), 255};
      //hsv = brighten(hsv, 255, false);
      hsv.v = timer_layer_v.ramp;
      hsv.s = timer_layer_s.ramp;
    }
    else {
      // Blank KC_TRNS
      rgb_matrix_set_color(led_index, 0, 0, 0);
      return;
    }
  }
  else if (keycode == KC_NO) {
    // Blank KC_NO
    rgb_matrix_set_color(led_index, 0, 0, 0);
    return;
  }
  else {
    if (layer != 0) hsv = hsv_default;
    else return;  // Skip unknown keycodes
  }

  // Enforce config limit
  if (hsv.v > RGBLIGHT_LIMIT_VAL) hsv.v = RGBLIGHT_LIMIT_VAL;

  // Apply color
  //rgb_matrix_sethsv(hsv.h, hsv.s, hsv.v);
  RGB rgb = hsv_to_rgb(hsv);
  rgb_matrix_set_color(led_index, rgb.r, rgb.g, rgb.b);
}

void boot_light_effect(void)
{
  if (boot_animation > boot_animation_stop) {
    boot_animation = 0;
    rgb_matrix_reload_from_eeprom();
  } else {
    const bool prev_direction = timer_boot_animation.ramp_direction;
    handle_timer(
      &timer_boot_animation.timer_data, 2,
      &timer_boot_animation.ramp,
      timer_boot_animation.ramp_start, timer_boot_animation.ramp_end,
      &timer_boot_animation.ramp_direction, 1
    );
    if (prev_direction != timer_boot_animation.ramp_direction) boot_animation++;

    rgb_matrix_sethsv_noeeprom(
      rgb_matrix_get_hue(),
      rgb_matrix_get_sat(),
      timer_boot_animation.ramp
    );
  }
}


/* Scanning
 * ------------------------------------------------------------------------- */

bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
  // TODO

  // Old code, leave as-is since I want to integrate it into the changes later.
  // --------------------------------------------------------------------------

  static uint8_t saved_mods  = 0;
  uint16_t       tmp_keycode = keycode;

  // Filter out the actual keycode from MT and LT keys.
  if ((keycode >= QK_MOD_TAP && keycode <= QK_MOD_TAP_MAX) || (keycode >= QK_LAYER_TAP && keycode <= QK_LAYER_TAP_MAX)) {
    tmp_keycode &= 0xFF;
  }

  switch (tmp_keycode) {
    case KC_TAB:
      if (!record->event.pressed) {
        if (IS_CAPS_ON()) tap_code(KC_CAPS);
        else if (is_caps_word_on()) caps_word_off();
        else return true;
      } else {
        if (IS_CAPS_ON() || is_caps_word_on()) return false;
        else return true;
      }
      return false;
      break;
    case KC_CAPS:
      if (record->event.pressed) {
        saved_mods = get_mods() & MOD_MASK_SHIFT;

        if (saved_mods) {  // One shift pressed
          del_mods(saved_mods);
          if (is_caps_word_on()) caps_word_off();
          register_code(KC_CAPS);
          add_mods(saved_mods);
        } else {
          if (IS_CAPS_ON()) tap_code(KC_CAPS);
          else caps_word_toggle();
        }
      } else {
        unregister_code(KC_CAPS);
      }
      return false;
      break;
    case BSP_DEL:
      if (record->event.pressed) {
        saved_mods = get_mods() & MOD_MASK_SHIFT;

        if (saved_mods == MOD_MASK_SHIFT) {  // Both shifts pressed
          register_code(KC_DEL);
        } else if (saved_mods) {       // One shift pressed
          del_mods(saved_mods);      // Remove any Shifts present
          register_code(KC_DEL);
          add_mods(saved_mods);      // Add shifts again
        } else {
          register_code(KC_BSPC);
        }
      } else {
        unregister_code(KC_DEL);
        unregister_code(KC_BSPC);
      }
      return false;
      break;
    case LOWER2:
      if (record->event.pressed) {
        
      } else {

      }
    case LOWER:
      if (record->event.pressed) {
        layer_on(_LOWER);
        update_tri_layer(_LOWER, _RAISE, _ADJUST);
      } else {
        layer_off(_LOWER);
        update_tri_layer(_LOWER, _RAISE, _ADJUST);
      }
      return false;
      break;
    case RAISE:
      if (record->event.pressed) {
        layer_on(_RAISE);
        update_tri_layer(_LOWER, _RAISE, _ADJUST);
      } else {
        layer_off(_RAISE);
        update_tri_layer(_LOWER, _RAISE, _ADJUST);
      }
      return false;
      break;
    case CLR_RST:
      eeconfig_init();
      reset_keyboard();
      break;
  }


  return true;
}

layer_state_t layer_state_set_user(layer_state_t new_state) {
  // TODO
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
  if (boot_animation > 0) {
    boot_light_effect();
    return true;  // skip
  }

  const uint8_t layer = get_highest_layer(layer_state);
  for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
    for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
      uint8_t led_index = g_led_config.matrix_co[row][col];

      // Skip indices without LEDs
      if (led_index < led_min || led_index > led_max
        || led_index == NO_LED) continue;

      // Light up the keycode
      light_keycode(led_index, col, row, layer);
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
  timer_boot_animation.ramp = timer_boot_animation_default.ramp;
}
