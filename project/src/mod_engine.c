#include "mod_engine.h"

#ifdef MOD_ENGINE_IMPLEMENTATION

#define BUTTON_B                          0x0020U
#define BUTTON_DPAD_LEFT                  0x0400U
#define BUTTON_LEFT_STICK                 0x4000U
#define BUTTON_RIGHT_STICK                0x8000U

#define ACTIVATOR_HOLD_DURATION_MS        500U
#define B_REPEAT_HOLD_DURATION_MS         500U
#define B_REPEAT_INTERVAL_MS              10U
#define RT_REPEAT_INTERVAL_MS             5U

static uint8_t repeat_features_enabled = 1;
static uint8_t left_dpad_held;
static uint32_t left_dpad_started_at;
static uint8_t b_repeat_active;
static uint8_t b_repeat_phase;
static uint32_t b_repeat_updated_at;
static uint8_t rt_held;
static uint8_t rt_repeat_zero;
static uint32_t rt_repeat_updated_at;
static uint8_t activator_held;
static uint8_t activator_toggled;
static uint32_t activator_started_at;

static void reset_repeat_state(uint32_t current_time_ms)
{
  left_dpad_started_at = left_dpad_held ? current_time_ms : 0;
  b_repeat_active = 0;
  b_repeat_phase = 0;
  b_repeat_updated_at = current_time_ms;
  rt_repeat_zero = 0;
  rt_repeat_updated_at = current_time_ms;
}

void mod_engine_toggle_repeat_features(uint32_t current_time_ms)
{
  repeat_features_enabled = !repeat_features_enabled;
  reset_repeat_state(current_time_ms);
}

uint8_t mod_engine_repeat_features_enabled(void)
{
  return repeat_features_enabled;
}

uint8_t mod_engine_process(controller_data_t *data, uint32_t current_time_ms)
{
  uint8_t was_left_dpad_held;
  uint8_t was_rt_held;
  uint8_t was_activator_held;
  uint8_t output_changed = 0;

  if(data == 0)
  {
    return 0;
  }

  was_activator_held = activator_held;
  activator_held = (data->buttons & (BUTTON_LEFT_STICK | BUTTON_RIGHT_STICK)) ==
                   (BUTTON_LEFT_STICK | BUTTON_RIGHT_STICK);
  if(activator_held && !was_activator_held)
  {
    activator_started_at = current_time_ms;
    activator_toggled = 0;
  }
  else if(!activator_held)
  {
    activator_started_at = 0;
    activator_toggled = 0;
  }

  if(activator_held && !activator_toggled &&
     current_time_ms - activator_started_at >= ACTIVATOR_HOLD_DURATION_MS)
  {
    activator_toggled = 1;
    mod_engine_toggle_repeat_features(current_time_ms);
    output_changed = 1;
  }

  was_left_dpad_held = left_dpad_held;
  was_rt_held = rt_held;
  left_dpad_held = (data->buttons & BUTTON_DPAD_LEFT) != 0;
  rt_held = data->rt != 0;

  if(left_dpad_held && !was_left_dpad_held)
  {
    left_dpad_started_at = current_time_ms;
    b_repeat_active = 0;
    b_repeat_phase = 0;
    b_repeat_updated_at = current_time_ms;
  }
  else if(!left_dpad_held)
  {
    left_dpad_started_at = 0;
    b_repeat_active = 0;
    b_repeat_phase = 0;
  }

  if(rt_held && !was_rt_held)
  {
    rt_repeat_zero = 0;
    rt_repeat_updated_at = current_time_ms;
  }
  else if(!rt_held)
  {
    rt_repeat_zero = 0;
  }

  if(repeat_features_enabled)
  {
    if(left_dpad_held &&
       current_time_ms - left_dpad_started_at >= B_REPEAT_HOLD_DURATION_MS)
    {
      if(!b_repeat_active)
      {
        b_repeat_active = 1;
        b_repeat_phase = 0;
        b_repeat_updated_at = current_time_ms;
        output_changed = 1;
      }
      else if(current_time_ms - b_repeat_updated_at >= B_REPEAT_INTERVAL_MS)
      {
        b_repeat_phase = (uint8_t)((b_repeat_phase + 1U) % 4U);
        b_repeat_updated_at = current_time_ms;
        output_changed = 1;
      }
    }

    if(rt_held &&
       current_time_ms - rt_repeat_updated_at >= RT_REPEAT_INTERVAL_MS)
    {
      rt_repeat_zero = !rt_repeat_zero;
      rt_repeat_updated_at = current_time_ms;
      output_changed = 1;
    }

    if(b_repeat_active)
    {
      if(b_repeat_phase == 0)
      {
        data->buttons |= BUTTON_B;
      }
      else
      {
        data->buttons &= (uint16_t)~BUTTON_B;
      }
    }

    if(rt_held && rt_repeat_zero)
    {
      data->rt = 0;
    }
  }

  return output_changed;
}

#endif