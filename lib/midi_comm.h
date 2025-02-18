#ifndef LIB_MIDI_COMM_H
#define LIB_MIDI_COMM_H 1
#include <stdarg.h>  // Include this header for va_start, va_end, etc.
#include <stdio.h>   // Include for vsnprintf

typedef void (*callback_int_int_int)(int, int, int);
typedef void (*callback_int_int)(int, int);
typedef void (*callback_int)(int);
typedef void (*callback_int32)(int32_t);
typedef void (*callback_uint8_uint8)(uint8_t, uint8_t);
typedef void (*callback_uint8)(uint8_t);
typedef void (*callback_uint16)(uint16_t);
typedef void (*callback_uint8_buffer)(uint8_t *buffer, int length);
typedef void (*callback_void)();


uint32_t send_buffer_as_sysex(char *buffer, uint32_t bufsize) {
  uint8_t sysex_data[bufsize + 2];  // +2 for SysEx start and end bytes

  sysex_data[0] = 0xF0;  // Start of SysEx
  for (uint32_t i = 0; i < bufsize; i++) {
    sysex_data[i + 1] = buffer[i];  // Copy buffer into SysEx data
  }
  sysex_data[bufsize + 1] = 0xF7;  // End of SysEx

  uint32_t v = tud_midi_n_stream_write(0, 0, sysex_data, sizeof(sysex_data));
  tud_task();
  return v;
}

uint32_t send_text_as_sysex(const char *text) {
  while (!tud_ready()) {
    tud_task();
  }
  uint32_t text_length = strlen(text);  // Get the length of the text
  uint8_t sysex_data[text_length + 2];  // +2 for SysEx start and end bytes

  sysex_data[0] = 0xF0;  // Start of SysEx
  for (uint32_t i = 0; i < text_length; i++) {
    sysex_data[i + 1] = text[i];  // Copy text into SysEx data
  }
  sysex_data[text_length + 1] = 0xF7;  // End of SysEx

  // Call the stream write function with the SysEx message
  uint32_t v = tud_midi_n_stream_write(0, 0, sysex_data, sizeof(sysex_data));
  tud_task();
  return v;
}

void send_midi_clock() {
  // Ensure TinyUSB stack is initialized and ready
  if (tud_ready()) {
    // Construct the MIDI message
    // MIDI Timing Clock message format: 0xF8
    uint8_t midi_message[1];
    midi_message[0] = 0xF8;  // Timing Clock command

    // Send the MIDI message
    tud_midi_n_stream_write(0, 0, midi_message, sizeof(midi_message));
    tud_task();
  }
}

void send_midi_start() {
  // Ensure TinyUSB stack is initialized and ready
  if (tud_ready()) {
    // Construct the MIDI message
    // MIDI Start message format: 0xFA
    uint8_t midi_message[1];
    midi_message[0] = 0xFA;  // Start command

    // Send the MIDI message
    tud_midi_n_stream_write(0, 0, midi_message, sizeof(midi_message));
    tud_task();
  }
}

void send_midi_stop() {
  // Ensure TinyUSB stack is initialized and ready
  if (tud_ready()) {
    // Construct the MIDI message
    // MIDI Stop message format: 0xFC
    uint8_t midi_message[1];
    midi_message[0] = 0xFC;  // Stop command

    // Send the MIDI message
    tud_midi_n_stream_write(0, 0, midi_message, sizeof(midi_message));
    tud_task();
  }
}

void send_midi_note_on(uint8_t note, uint8_t velocity) {
  // Ensure TinyUSB stack is initialized and ready
  if (tud_ready()) {
    // MIDI cable number 0, Note On event, channel 1
    uint8_t channel = 0;  // MIDI channels are 0-15

    // Construct the MIDI message
    // MIDI Note On message format: 0x9n, where n is the channel number
    uint8_t midi_message[3];
    midi_message[0] = 0x90 | channel;  // Note On command with channel
    midi_message[1] = note;            // MIDI note number
    midi_message[2] = velocity;        // Note velocity

    // Send the MIDI message
    tud_midi_n_stream_write(0, 0, midi_message, sizeof(midi_message));
    tud_task();
  }
}

int printf_sysex(const char *format, ...) {
  if (!tud_ready()) {
    return 0;
  }
  va_list args;
  va_start(args, format);
  int text_length = vsnprintf(NULL, 0, format, args);
  va_end(args);

  char text[text_length + 1];  // +1 for null terminator
  va_start(args, format);
  vsnprintf(text, text_length + 1, format, args);
  va_end(args);

  return send_text_as_sysex(text);
}

typedef void (*midi_comm_callback)(uint8_t, uint8_t, uint8_t, uint8_t);

uint8_t midi_buffer[32];
uint8_t midi_sysex_buffer[1024];
bool midi_sysex_active = false;
uint8_t midi_sysex_index = 0;

void midi_comm_task(callback_uint8_buffer sysex_callback,
                    callback_int_int_int midi_note_on,
                    callback_int_int midi_note_off,
                    callback_int_int_int midi_key_pressure_callback,  // 0xa0
                    callback_int_int_int midi_cc_callback,            // 0xb0
                    callback_int_int midi_program_change_callback,    // 0xc0
                    callback_int_int midi_channel_pressure_callback,  // 0xd0
                    callback_int_int midi_pitch_bend_callback,        // 0xe0
                    callback_void midi_start, callback_void midi_continue,
                    callback_void midi_stop, callback_void midi_timing) {
  uint32_t bytes_read = 0;
  if (tud_midi_n_available(0, 0)) {
    bytes_read = tud_midi_n_stream_read(0, 0, midi_buffer, 3);
  } else {
    return;
  }
  if (bytes_read == 0) {
    return;
  }
  // for (int i = 0; i < bytes_read; i++) {
  //   printf_sysex("[%d]: %x\n", i, midi_buffer[i]);
  // }

  for (int i = 0; i < bytes_read; i++) {
    if (midi_buffer[i] == 0xF0) {
      // Start of SysEx
      midi_sysex_active = true;
      midi_sysex_index = 0;
    } else if (midi_buffer[i] == 0xF7) {
      // End of SysEx
      if (midi_sysex_active && midi_sysex_index > 0) {
        midi_sysex_active = false;
        if (sysex_callback != NULL) {
          // Call the callback with the completed SysEx message
          sysex_callback(midi_sysex_buffer, midi_sysex_index);
        }
      }
      // Reset the SysEx buffer
      midi_sysex_index = 0;
      memset(midi_sysex_buffer, 0, sizeof(midi_sysex_buffer));
    } else if (midi_sysex_active) {
      // Append to the SysEx buffer
      if (midi_sysex_index < sizeof(midi_sysex_buffer)) {
        midi_sysex_buffer[midi_sysex_index++] = midi_buffer[i];
      } else {
        // Buffer overflow, reset
        midi_sysex_active = false;
        midi_sysex_index = 0;
        memset(midi_sysex_buffer, 0, sizeof(midi_sysex_buffer));
      }
    }
  }
  if (midi_sysex_active) {
    return;
  }

  uint8_t messageType = midi_buffer[0] & 0xF0;  // Extract the message type
  uint8_t channel = midi_buffer[0] & 0x0F;      // Extract the channel (0-15)

  if (midi_buffer[0] == 0xf8) {
    // timing received
    usb_midi_present = true;
    if (midi_timing != NULL) {
      midi_timing();
    }
    return;
  } else if (midi_buffer[0] == 0xfa) {
    // start received
    usb_midi_present = true;
    if (midi_start != NULL) {
      midi_start();
    }
    return;
  } else if (midi_buffer[0] == 0xfb) {
    // continue received
    usb_midi_present = true;
    if (midi_continue != NULL) {
      midi_continue();
    }
    return;
  } else if (midi_buffer[0] == 0xfc) {
    // stop received
    usb_midi_present = true;
    if (midi_stop != NULL) {
      midi_stop();
    }
    return;
  } else if (messageType == 0xa0 && bytes_read > 2) {
    // key pressure
    usb_midi_present = true;
    if (midi_key_pressure_callback != NULL) {
      // note number, pressure value
      midi_key_pressure_callback(channel, midi_buffer[1], midi_buffer[2]);
    }
  } else if (messageType == 0xb0 && bytes_read > 2) {
    // control change received
    usb_midi_present = true;
    if (midi_cc_callback != NULL) {
      midi_cc_callback(channel, midi_buffer[1], midi_buffer[2]);
    }
    return;
  } else if (messageType == 0xc0 && bytes_read > 1) {
    // program change received
    usb_midi_present = true;
    if (midi_program_change_callback != NULL) {
      midi_program_change_callback(channel, midi_buffer[1]);
    }
    return;
  } else if (messageType == 0xd0 && bytes_read > 1) {
    // channel pressure received
    usb_midi_present = true;
    if (midi_channel_pressure_callback != NULL) {
      midi_channel_pressure_callback(channel, midi_buffer[1]);
    }
    return;
  } else if (messageType == 0xe0 && bytes_read > 2) {
    // pitch bend received
    usb_midi_present = true;
    if (midi_pitch_bend_callback != NULL) {
      // convert the two bytes to a 14-bit value
      int pitch_bend = (midi_buffer[2] << 7) | midi_buffer[1];
      midi_pitch_bend_callback(channel, pitch_bend);
    }
    return;
  } else if (messageType == 0x80 && bytes_read > 1) {
    // note off received
    usb_midi_present = true;
    if (midi_note_off != NULL) {
      midi_note_off(channel, midi_buffer[1]);
    }
    return;
  } else if (messageType == 0x90 && bytes_read > 2) {
    // note on received
    usb_midi_present = true;
    if (midi_note_on != NULL) {
      if (bytes_read == 3) {
        // TODO: for some reason this is not working
        midi_note_on(channel, midi_buffer[1], midi_buffer[2]);
      } else {
        midi_note_on(channel, midi_buffer[1], 0);
      }
    }
    return;
  }

  if (bytes_read == 3) {
    usb_midi_present = true;
    // Extract the status byte and MIDI channel
    // Extract the note number and velocity
    uint8_t note = midi_buffer[1];
    uint8_t velocity = midi_buffer[2];
    if (messageType == 176 && channel == 0 && note == 0) {
      send_text_as_sysex("command=reset");
      sleep_ms(10);
      reset_usb_boot(0, 0);
    }
  }
}

// #define printf printf_sysex
#endif
