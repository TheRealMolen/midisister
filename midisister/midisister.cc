//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#include "config.h"
#include "nunchuk.h"
#include "util.h"


using std::begin, std::end;

constexpr int LedPin = 25;
constexpr int I2C_SDA_Pin = 4;
constexpr int I2C_SCL_Pin = 5;
constexpr int I2C_Baud = 100 * 1000;


void midi_note_on(uint8_t channel, uint8_t note, uint8_t vel = 127)
{
    uint8_t message[3] = { uint8_t(0x90 | channel), note, vel };
    uart_write_blocking(uart0, message, 3);
    printf(">NOTEON:%d,%d,%d\n", int(channel), int(note), int(vel));
}
void midi_note_off(uint8_t channel, uint8_t note)
{
    uint8_t message[3] = { uint8_t(0x80 | channel), note, 0 };
    uart_write_blocking(uart0, message, 3);
    printf(">NOTEOFF:%d,%d\n", int(channel), int(note));
}
void midi_pitchbend(uint8_t channel, uint16_t pitchbend)
{
    uint8_t lsb = uint8_t(pitchbend & 0x7f);
    uint8_t msb = uint8_t(pitchbend >> 7);
    uint8_t message[3] = { uint8_t(0xe0 | channel), lsb, msb };
    uart_write_blocking(uart0, message, 3);
    //printf(">PB:%d,%d, %02x:%02x\n", int(channel), int(pitchbend), int(msb), int(lsb));
}
void midi_cc(uint8_t channel, uint8_t cc, uint8_t val)
{
    uint8_t message[3] = { uint8_t(0xB0 | channel), cc, val };
    uart_write_blocking(uart0, message, 3);
}

uint32_t lastMs = 0;

byte ledState = 0;

byte playingNote = 0;
int lastNoteMs = 0;

Config config;
uint16_t lastOutputVals[Config::MaxMappings] = {};


void onLineRead(const char* line)
{
    printf("read line '%s'\n", line);
    config.parse(line);
    puts("done parsing");
}

StdinAsync stdinAsync(onLineRead);


void loop(Nunchuk& nchk)
{
    stdinAsync.update();

    uint32_t nowMs = millis();
    uint32_t deltaMs = nowMs - lastMs;
    if (!deltaMs)
    {
        sleep_ms(1);
        return;
    }
    lastMs = nowMs;

    nchk.update();
        
    if (config.areNotesEnabled())
    {
        const Mapping& mapping = config.getNotesMapping();
        uint16_t note = config.quantiseNote(mapping.getVal(nchk));

        bool autoRepeat = false;
        if (nchk.getBtnC() && nchk.getBtnZ())
        {
            int timeSinceLastNoteMs = nowMs - lastNoteMs;
            if (timeSinceLastNoteMs >= config.getAutoRepeatMs() && note != playingNote)
                autoRepeat = true;
        }

        if (nchk.wasZPressed() || autoRepeat)
        {
            if (playingNote)
                midi_note_off(config.getChannel(), playingNote);

            midi_note_on(config.getChannel(), note);
            playingNote = note;
            lastNoteMs = nowMs;

            ledState = 1 - ledState;
            gpio_put(LedPin, ledState);
        }

        if (nchk.wasZReleased() && playingNote)
        {
            midi_note_off(config.getChannel(), playingNote);            
            playingNote = 0;
        }
    }

    for (uint i=0; i<config.getNumMappings(); ++i)
    {
        const Mapping& mapping = config.getMappings()[i];
        uint16_t val = mapping.getVal(nchk);
        if (val == lastOutputVals[i])
            continue;

        if (mapping.destType == Dest::ControlChange)
            midi_cc(config.getChannel(), byte(mapping.destParam), byte(val));
        else if (mapping.destType == Dest::PitchBend)
            midi_pitchbend(config.getChannel(), val);

        lastOutputVals[i] = val;
    }
}

static const char* defaultConfigStr = 
R"END(
    CHAN 1
    ROOT C
    SCALE 0 1 5 7 10
    OCTAVES 2 7
    BPM 100
    DIV 0.25
    
    MAP ax -1 1 48 100 note
    MAP jx- cc 16
    MAP jx+ cc 19
    MAP jy pb
    MAP ay cc 17
    MAP az 1 -1 0 127 cc 18
)END";

int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_usb_init();

    // initialise MIDI on UART 0, gpio 0 & 1
    uart_init(uart0, 31250);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    config.parse(defaultConfigStr);

    // cancel any previous notes
    for (byte note=1; note<120; ++note)
        midi_note_off(config.getChannel(), note);

    i2c_init(i2c0, I2C_Baud);
    gpio_set_function(I2C_SDA_Pin, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Pin, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Pin);
    gpio_pull_up(I2C_SCL_Pin);

    initError();

    Nunchuk nchk(i2c0);
    
    lastMs = millis();

    for(;;)
    {
        loop(nchk);
    }

    return 0;
}
