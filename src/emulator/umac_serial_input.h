#ifndef MCX_MAC_UMAC_SERIAL_INPUT_H_
#define MCX_MAC_UMAC_SERIAL_INPUT_H_

#include <stdbool.h>

/* Poll the board's existing debug UART from the emulator thread. */
void umac_serial_input_init(void);
void umac_serial_input_poll(void);
void umac_serial_input_report(void);
bool umac_serial_guest_paused(void);
bool umac_serial_test_pattern_enabled(void);

#endif
