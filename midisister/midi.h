#pragma once

#include <cstdint>

#include "hardware/uart.h"


void midi_init(uart_inst_t* block = uart0, uint8_t txGpio = 0, uint8_t rxGpio = 1);

void midi_note_on(uint8_t channel, uint8_t note, uint8_t vel = 127);
void midi_note_off(uint8_t channel, uint8_t note);
void midi_pitchbend(uint8_t channel, uint16_t pitchbend);
void midi_cc(uint8_t channel, uint8_t cc, uint8_t val);
