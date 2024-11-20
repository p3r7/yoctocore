#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/rtc.h"
#include "hardware/structs/clocks.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/binary_info.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/types.h"

bool usb_midi_present = false;
#ifdef INCLUDE_MIDI
#include "bsp/board.h"
#include "tusb.h"
//
#include "lib/midi_comm.h"
#endif

// utility functions
#define util_clamp(x, a, b) ((x) > (b) ? (b) : ((x) < (a) ? (a) : (x)))

#define linlin(x, xmin, xmax, ymin, ymax)                                 \
  util_clamp((ymin + (x - xmin) * (ymax - ymin) / (xmax - xmin)), (ymin), \
             (ymax))

#define BLOCKS_PER_SECOND SAMPLE_RATE / SAMPLES_PER_BUFFER
static const uint32_t PIN_DCDC_PSM_CTRL = 23;
#define DURATION_HOLD 500
#define DURATION_HOLD_LONG 1250
#define FLASH_TARGET_OFFSET (5 * 256 * 1024)

#define MODE_MANUAL 0
#define MODE_PITCH 1
#define MODE_ENVELOPE 2
#define MODE_CC 3
#define MODE_MIDI_CLOCK 4
#define MODE_CLOCK 5
#define MODE_LFO 6

//
#include "ff.h" /* Obtains integer types */
//
#include "diskio.h" /* Declarations of disk functions */
//
#include "f_util.h"
#include "hw_config.h"
#include "my_debug.h"
#include "sd_card.h"
//
//
#include "lib/WS2812.h"
#include "lib/adsr.h"
#include "lib/dac.h"
#include "lib/filterexp.h"
#include "lib/knob_change.h"
#include "lib/mcp3208.h"
#include "lib/memusage.h"
#include "lib/pcg_basic.h"
#include "lib/random.h"
#include "lib/scene.h"
#include "lib/sdcard.h"
#include "lib/simpletimer.h"
#include "lib/slew.h"
// globals
float g_bpm = 120.0;
DAC dac;
WS2812 ws2812;
ADSR pool_adsr[8];
SimpleTimer pool_timer[16];
KnobChange pool_knobs[8];
MCP3208 mcp3208;
const uint8_t button_num = 9;
const uint8_t button_pins[9] = {8, 9, 20, 21, 22, 26, 27, 28, 0};
uint8_t button_values[9] = {0, 0, 0, 0, 0, 0};

#ifdef INCLUDE_MIDI
#include "lib/midi_comm.h"
#include "lib/midicallback.h"
#endif

void timer_callback_outputs(bool on, int user_data) {
  // printf("[timer_callback_outputs]: %d %d\n", on, user_data);
}

void timer_callback_sample_knob(bool on, int user_data) {
  char buf[128];
  // clear the buffer
  buf[0] = '\0';
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t val = MCP3208_read(&mcp3208, i, false);
    int16_t val_changed = KnobChange_update(&pool_knobs[i], val);
    sprintf(buf, "%s%d ", buf, val);

    if (val_changed != -1 &&
        scenes[state.scene].output[i].mode == MODE_MANUAL) {
      float voltage = linlin((float)val_changed, 0.0f, 1023.0f,
                             scenes[state.scene].output[i].min_voltage,
                             scenes[state.scene].output[i].max_voltage);
      Slew_set_target(&scenes[state.scene].output_process[i].slew, voltage);
    }
  }
  // printf("\n%s\n", buf);
}

void timer_callback_ws2812(bool on, int user_data) {
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t val = pool_knobs[i].last;
    WS2812_fill(&ws2812, i, val / 4, 0, 255 - val / 4);
  }
  WS2812_show(&ws2812);
}

int main() {
  // Set PLL_USB 96MHz
  const uint32_t main_line = 96;
  pll_init(pll_usb, 1, main_line * 16 * MHZ, 4, 4);
  clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                  main_line * MHZ, main_line / 2 * MHZ);
  // Change clk_sys to be 96MHz.
  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                  CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                  main_line * MHZ, main_line * MHZ);
  // CLK peri is clocked from clk_sys so need to change clk_peri's freq
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  main_line * MHZ, main_line * MHZ);
  // Reinit uart now that clk_peri has changed
  stdio_init_all();

#ifdef DO_OVERCLOCK
  set_sys_clock_khz(225000, true);
#else
  set_sys_clock_khz(125000, true);
#endif
  sleep_ms(10);

  // DCDC PSM control
  // 0: PFM mode (best efficiency)
  // 1: PWM mode (improved ripple)
  gpio_init(PIN_DCDC_PSM_CTRL);
  gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
  gpio_put(PIN_DCDC_PSM_CTRL, 1);  // PWM mode for less Audio noise

  // setup i2c
  i2c_init(i2c0, 50 * 1000);
  gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C0_SDA_PIN);
  gpio_pull_up(I2C0_SCL_PIN);
  i2c_init(i2c1, 50 * 1000);
  gpio_set_function(I2C1_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C1_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C1_SDA_PIN);
  gpio_pull_up(I2C1_SCL_PIN);

#ifdef INCLUDE_MIDI
  tusb_init();
#endif

  // // // load the Scene data
  // Scene_load_data();

  // Implicitly called by disk_initialize,
  // but called here to set up the GPIOs
  // before enabling the card detect interrupt:
  sd_init_driver();

  // initialize random library
  random_initialize();

  // initialize knobs
  for (uint8_t i = 0; i < 8; i++) {
    KnobChange_init(&pool_knobs[i], 10);
  }

  // setup buttons
  for (uint8_t i = 0; i < button_num; i++) {
    gpio_init(button_pins[i]);
    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
  }

  // initialize timers
  // first 8 timers are for each output and disabled by default
  for (uint8_t i = 0; i < 8; i++) {
    // TODO: setup callbacks
    SimpleTimer_init(&pool_timer[i], g_bpm, 4, 0, timer_callback_outputs, i);
  }
  // setup a timer at 5 milliseconds to sample the knobs
  SimpleTimer_init(&pool_timer[8], 60 * 15, 4, 0, timer_callback_sample_knob,
                   0);
  SimpleTimer_init(&pool_timer[9], 60 * 15, 4, 0, timer_callback_ws2812, 0);

  // initialize MCP3208
  MCP3208_init(&mcp3208, spi0, PIN_SPI_CSN, PIN_SPI_CLK, PIN_SPI_RX,
               PIN_SPI_TX);

  // // initialize WS2812
  WS2812_init(&ws2812, WS2812_PIN, pio0, WS2812_SM, 8);
  WS2812_set_brightness(&ws2812, 70);
  for (uint8_t i = 0; i < 8; i++) {
    WS2812_fill(&ws2812, i, 255, 0, 255);
  }
  WS2812_show(&ws2812);

  // initialize scenes
  Scenes_init();

  // initialize SD card
  // sleep_ms(1000);
  printf("[main]: initializing sd card\n");
  for (uint8_t i = SDCARD_CMD_GPIO - 1; i < SDCARD_D0_GPIO + 5; i++) {
    gpio_pull_up(i);
  }
  if (!run_mount()) {
    sleep_ms(1000);
    printf("[main]: failed to mount sd card\n");
  } else {
    // big_file_test("test.bin", 2, 0);  // perform read/write test
    Scene_load_data_sdcard();
  }

  // initialize dac
  DAC_init(&dac);
#ifdef DEBUG_VOLTAGE_CALIBRATION
  sleep_ms(5000);
  printf("DAC calibration\n");
  for (int8_t volts = -5; volts <= 10; volts++) {
    for (uint8_t i = 0; i < 8; i++) {
      DAC_set_voltage(&dac, i, volts);
    }
    DAC_update(&dac);
    printf("calibration at %d\n", volts);
    sleep_ms(5000);
  }
#endif
  for (uint8_t i = 0; i < 8; i++) {
    DAC_set_voltage(&dac, i, 0);
  }
  DAC_update(&dac);

  uint32_t ct = to_ms_since_boot(get_absolute_time());
  uint32_t ct_last_print = ct;
  uint32_t ct_next_bpm = ct + (60.0 / g_bpm * 1000);
  bool startup = false;
  while (true) {
#ifdef INCLUDE_MIDI
    tud_task();
    midi_comm_task(midi_sysex_callback, midi_note_on, midi_note_off, midi_start,
                   midi_continue, midi_stop, midi_timing);
#endif

    ct = to_ms_since_boot(get_absolute_time());
    if (ct - ct_last_print > 1000) {
      ct_last_print = ct;
      print_memory_usage();
      //   flash_mem_test();
      // printf("time: %lld\n", time_us_64());
      //  ClockPool_enable(0, true);
      //  ClockPool_reset_clock(0, 70, 1, 0, 5.0);
      //  read knobs

      if (!startup) {
        startup = true;
        printf("Startup complete\n");
        SimpleTimer_start(&pool_timer[8], ct);
        SimpleTimer_start(&pool_timer[9], ct);
      }
    }

    // if (ct > ct_next_bpm) {
    //   ct_next_bpm = ct + (60.0 / g_bpm * 1000);
    //   printf("BPM: %f\n", g_bpm);
    // }

    for (uint8_t i = 0; i < 10; i++) {
      SimpleTimer_process(&pool_timer[i], ct);
    }

    // read buttons
    for (uint8_t i = 0; i < button_num; i++) {
      bool val = 1 - gpio_get(button_pins[i]);
      if (val != button_values[i]) {
        printf("Button %d: %d\n", i, val);
        button_values[i] = val;
      }
    }

    // update based on the mode
    for (uint8_t i = 0; i < 8; i++) {
      if (scenes[state.scene].output[i].mode == MODE_MANUAL) {
        float val =
            Slew_process(&scenes[state.scene].output_process[i].slew, ct);
        DAC_set_voltage(&dac, i, val);
      } else if (scenes[state.scene].output[i].mode == MODE_PITCH) {
        // pitch
        // DAC_set_voltage(&dac, i, 0);
      } else if (scenes[state.scene].output[i].mode == MODE_ENVELOPE) {
        // envelope
        // DAC_set_voltage(&dac, i, 0);
      } else if (scenes[state.scene].output[i].mode == MODE_CC) {
        // cc
        // DAC_set_voltage(&dac, i, 0);
      } else if (scenes[state.scene].output[i].mode == MODE_MIDI_CLOCK) {
        // midi clock
        // DAC_set_voltage(&dac, i, 0);
      } else if (scenes[state.scene].output[i].mode == MODE_CLOCK) {
        // clock
        // DAC_set_voltage(&dac, i, 0);
      } else if (scenes[state.scene].output[i].mode == MODE_LFO) {
        // lfo
        // DAC_set_voltage(&dac, i, 0);
      }
    }

    Scene_save_data_sdcard();
  }
}
