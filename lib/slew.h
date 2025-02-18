#ifndef SLEW_LIB
#define SLEW_LIB 1

#include <stdint.h>

typedef struct Slew {
  float current_value;
  float target_value;
  float start_value;
  uint32_t start_time;
  uint32_t duration;
  uint32_t current_time;
} Slew;

void Slew_init(Slew *slew, uint32_t duration, float initial_value) {
  slew->current_value = initial_value;
  slew->target_value = initial_value;
  slew->start_value = initial_value;
  slew->start_time = 0;
  slew->duration = duration;
}

void Slew_set_target(Slew *slew, float target_value) {
  if (slew->target_value != target_value) {
    slew->start_value = slew->current_value;
    slew->target_value = target_value;
    slew->start_time = slew->current_time;
  }
}

void Slew_set_duration(Slew *slew, uint32_t duration) {
  slew->duration = duration;
  if (slew->duration == 0) {
    slew->current_value = slew->target_value;
  }
}

float Slew_smootherstep(float t) {
  // Quintic smootherstep function
  return t * t * t * (t * (t * 6 - 15) + 10);
}

float Slew_process(Slew *slew, float target_value, uint32_t current_time) {
  if (slew->duration == 0) {
    return target_value;
  }
  if (slew->target_value != target_value) {
    Slew_set_target(slew, target_value);
  }
  slew->current_time = current_time;

  uint32_t elapsed_time = current_time - slew->start_time;

  // If the elapsed time exceeds the duration, finish the transition
  if (elapsed_time >= slew->duration) {
    slew->current_value = slew->target_value;
    return slew->current_value;
  }

  // Calculate normalized time t in the range [0, 1]
  float t = (float)elapsed_time / (float)slew->duration;

  // Ensure t is clamped to [0, 1]
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  // Use smootherstep interpolation
  float smooth_step = Slew_smootherstep(t);

  // Compute the current value based on the smootherstep interpolation
  slew->current_value = slew->start_value +
                        (slew->target_value - slew->start_value) * smooth_step;
  return slew->current_value;
}

float Slew_process2(Slew *slew, uint32_t current_time) {
  Slew_process(slew, slew->target_value, current_time);
}

#endif