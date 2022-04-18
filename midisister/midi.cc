#include "midi.h"

#include "pico/stdlib.h"
#include <cstdio>


namespace {

uart_inst_t* MidiUartBlock = uart0;

};


void midi_init(uart_inst_t* block, uint8_t txGpio, uint8_t rxGpio)
{
    MidiUartBlock = block;
    
    uart_init(MidiUartBlock, 31250);
    gpio_set_function(txGpio, GPIO_FUNC_UART);
    gpio_set_function(rxGpio, GPIO_FUNC_UART);
}

void midi_note_on(uint8_t channel, uint8_t note, uint8_t vel)
{
    uint8_t message[3] = { uint8_t(0x90 | channel), note, vel };
    uart_write_blocking(MidiUartBlock, message, 3);
    printf(">NOTEON:%d,%d,%d\n", int(channel), int(note), int(vel));
}

void midi_note_off(uint8_t channel, uint8_t note)
{
    uint8_t message[3] = { uint8_t(0x80 | channel), note, 0 };
    uart_write_blocking(MidiUartBlock, message, 3);
    printf(">NOTEOFF:%d,%d\n", int(channel), int(note));
}

void midi_pitchbend(uint8_t channel, uint16_t pitchbend)
{
    uint8_t lsb = uint8_t(pitchbend & 0x7f);
    uint8_t msb = uint8_t(pitchbend >> 7);
    uint8_t message[3] = { uint8_t(0xe0 | channel), lsb, msb };
    uart_write_blocking(MidiUartBlock, message, 3);
    //printf(">PB:%d,%d, %02x:%02x\n", int(channel), int(pitchbend), int(msb), int(lsb));
}
void midi_cc(uint8_t channel, uint8_t cc, uint8_t val)
{
    uint8_t message[3] = { uint8_t(0xB0 | channel), cc, val };
    uart_write_blocking(MidiUartBlock, message, 3);
}
