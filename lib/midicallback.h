#ifndef LIB_MIDI_CALLBACK_H
#define LIB_MIDI_CALLBACK_H 1

// #define DEBUG_MIDI 1
#define MIDI_MAX_NOTES 128
#define MIDI_MAX_TIME_ON 10000  // 10 seconds
#define MIDI_RESET_EVERY_BEAT 16
#define MIDI_CLOCK_MULTIPLIER 2

uint32_t note_hit[MIDI_MAX_NOTES];
int midi_bpm_detect[7];
uint8_t midi_bpm_detect_i = 0;
bool note_on[MIDI_MAX_NOTES];
uint32_t midi_last_time = 0;
uint32_t midi_delta_sum = 0;
uint32_t midi_delta_count = 0;
#define MIDI_DELTA_COUNT_MAX 32
uint32_t midi_timing_count = 0;
const uint8_t midi_timing_modulus = 24;

void midi_note_off(int note) {
#ifdef DEBUG_MIDI
  printf("note_off: %d\n", note);
#endif
}

bool get_sysex_param_float_value(const char *param_name, const uint8_t *sysex,
                                 size_t length, float *out_value) {
  size_t param_len = strlen(param_name);

  // Check if the SysEx message is long enough and contains the parameter name
  if (length > param_len && memcmp(sysex, param_name, param_len) == 0) {
    // Allocate a temporary buffer for the float part
    char value_str[length - param_len + 1];

    // Copy the float part into the buffer
    for (size_t i = param_len; i < length; i++) {
      value_str[i - param_len] = sysex[i];
    }

    // Null-terminate the string
    value_str[length - param_len] = '\0';

    // Convert the extracted string to a float and store it in out_value
    *out_value = strtof(value_str, NULL);
    return true;
  }

  // Return false if the parameter name is not found or the message is invalid
  return false;
}

void midi_sysex_callback(uint8_t *sysex, int length) {
#ifdef DEBUG_MIDI
  // build a string from the sysex buffer
  char sysex_str[length + 2 + 7];
  sysex_str[0] = 's';
  sysex_str[1] = 'y';
  sysex_str[2] = 's';
  sysex_str[3] = 'e';
  sysex_str[4] = 'x';
  sysex_str[5] = ':';
  sysex_str[6] = ' ';
  for (int j = 0; j < length; j++) {
    sysex_str[j + 7] = sysex[j];
  }
  sysex_str[length + 7] = '\n';
  send_buffer_as_sysex(sysex_str, length + 7 + 1);
#endif
  float val;
  if (get_sysex_param_float_value("version", sysex, length, &val)) {
    printf("v1.0.0");
  } else {
    Yoctocore_process_sysex(&yocto, sysex);
    // clear the sysex buffer
  }
}

void midi_note_on(int note, int velocity) {
#ifdef DEBUG_MIDI
  printf("note_on: %d\n", note);
#endif
}

void midi_start() {
#ifdef DEBUG_MIDI
  printf("[midicallback] midi start\n");
#endif
  midi_timing_count = 24 * MIDI_RESET_EVERY_BEAT - 1;
}
void midi_continue() {
#ifdef DEBUG_MIDI
  printf("[midicallback] midi continue (starting)\n");
#endif
  midi_start();
}
void midi_stop() {
#ifdef DEBUG_MIDI
  printf("[midicallback] midi stop\n");
#endif
  midi_timing_count = 24 * MIDI_RESET_EVERY_BEAT - 1;
}

// Comparator function for qsort
int compare_ints(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

// Function to find the median of an array with 7 elements
int findMedian(int arr[], uint8_t size) {
  // Create a copy of the array to avoid modifying the original
  int arrCopy[size];
  memcpy(arrCopy, arr, sizeof(int) * size);

  // Sort the copy of the array
  qsort(arrCopy, size, sizeof(int), compare_ints);

  // Return the middle element from the sorted copy

  return arrCopy[size / 2];
}

void midi_timing() {
  midi_timing_count++;
  if (midi_timing_count % (24 * MIDI_RESET_EVERY_BEAT) == 0) {
    // reset
#ifdef DEBUG_MIDI
    printf("[midicallback] midi resetting\n");
#endif
  }
  if (midi_timing_count % (midi_timing_modulus / MIDI_CLOCK_MULTIPLIER) == 0) {
    // soft sync
    // TODO
  }
  uint32_t now_time = time_us_32();
  if (midi_last_time > 0) {
    midi_delta_sum += now_time - midi_last_time;
    midi_delta_count++;
    if (midi_delta_count == MIDI_DELTA_COUNT_MAX) {
      midi_bpm_detect[midi_bpm_detect_i] =
          (int)round(1250000.0 * MIDI_CLOCK_MULTIPLIER * MIDI_DELTA_COUNT_MAX /
                     (float)(midi_delta_sum));
      midi_bpm_detect_i++;
      if (midi_bpm_detect_i == 7) {
        midi_bpm_detect_i = 0;
      }
      // sort midi_bpm_detect

      int bpm_new = findMedian(midi_bpm_detect, 7);
      //       if (bpm_new > 60 && bpm_new < 260 && bpm_new != g_bpm) {
      //         g_bpm = bpm_new;
      // #ifdef DEBUG_MIDI
      //         printf("[midicallback] midi bpm = %d\n", bpm_new);
      // #endif
      //       }

      //   if (bpm_input - 7 != bpm_set) {
      //     // set bpm
      //   }
      midi_delta_count = 0;
      midi_delta_sum = 0;
    }
  }
  midi_last_time = now_time;
}
#endif