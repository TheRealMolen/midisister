//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

#include "util.h"
#include "nunchuk.h"


constexpr int LedPin = 25;
constexpr int I2C_SDA_Pin = 4;
constexpr int I2C_SCL_Pin = 5;
constexpr int I2C_Baud = 100 * 1000;


void midi_note_on(uint8_t channel, uint8_t note, uint8_t vel = 127)
{
    byte message[3] = { uint8_t(0x90 | channel), note, vel };
    uart_write_blocking(uart0, message, 3);
}
void midi_note_off(uint8_t channel, uint8_t note)
{
    byte message[3] = { uint8_t(0x80 | channel), note, 0 };
    uart_write_blocking(uart0, message, 3);
}


int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_usb_init();

    sleep_ms(4000);
    printf("midisister booting...\n");

    // initialise MIDI on UART 0, gpio 0 & 1
    uart_init(uart0, 31250);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    i2c_init(i2c0, I2C_Baud);
    gpio_set_function(I2C_SDA_Pin, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Pin, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Pin);
    gpio_pull_up(I2C_SCL_Pin);

    initError();

    printf("trying to talk to the nunchuk...\n");
    Nunchuk nchk(i2c0);

    byte channel = 1;
    byte playingNote = 0;
    byte led = 0;
    for (;;)
    {
        nchk.update();

        if (nchk.wasZPressed())
        {
            if (playingNote)
                midi_note_off(channel, playingNote);
            
            float x = std::clamp(0.5f * (1.0f + nchk.getAccelX()), 0.f, 1.f);
            byte note = byte(x * 48.f + 20.f);

            midi_note_on(channel, note);
            playingNote = note;

            led = 1 - led;
            gpio_put(LedPin, led);
        }
        else if (nchk.getBtnC() && playingNote)
        {
            midi_note_off(channel, playingNote);            
            playingNote = 0;
        }
    }

    return 0;
}
