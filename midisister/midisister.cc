//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"

#include "util.h"
#include "nunchuk.h"


using std::begin, std::end;

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


byte channel = 1;
byte playingNote = 0;
byte ledState = 0;
uint32_t lastMs = 0;
int lastNoteMs = 0;
int bpm = 100;
float division = 0.25;
uint32_t autoRepeatMs = uint32_t((60.0f * 1000.0 / bpm) * division);
std::vector<byte> validNotes;

void initScale()
{
    const byte scaleNotes[] = { 0, 3, 5, 7, 9, 11 };
    const byte numScaleNotes = sizeof(scaleNotes) / sizeof(scaleNotes[0]);
    const byte key = 0;     // C == 0
    const byte lowOctave = 2;
    const byte hiOctave = 7;
    
    validNotes.clear();
    validNotes.reserve((hiOctave - lowOctave + 1) * numScaleNotes);
    for (byte octave=lowOctave; octave<hiOctave-1; ++octave)
    {
        byte offset = key + (octave * 12);
        for (byte scaleNote : scaleNotes)
        {
            validNotes.push_back(offset + scaleNote);
        }
    }
}

byte quantize(byte incoming)
{
    auto foundIt = std::lower_bound(begin(validNotes), end(validNotes), incoming);
    if (foundIt == end(validNotes) || (foundIt+1) == end(validNotes))
    {
        return validNotes.back();
    }

    int lo = *foundIt;
    int hi = *(foundIt + 1);

    int dLo = incoming - lo;
    int dHi = hi - incoming;
    if (dHi < dLo)
        return hi;
    
    return lo;
}

void loop(Nunchuk& nchk)
{
    uint32_t nowMs = millis();
    uint32_t deltaMs = nowMs - lastMs;
    lastMs = nowMs;

    nchk.update();
        
    float x = std::clamp(0.5f * (1.0f + nchk.getAccelX()), 0.f, 1.f);
    byte rawNote = byte(x * 56.f + 20.f);
    byte note = quantize(rawNote);

    bool autoRepeat = false;
    if (nchk.getBtnZ())
    {
        int timeSinceLastNoteMs = nowMs - lastNoteMs;
        if (timeSinceLastNoteMs >= autoRepeatMs && note != playingNote)
            autoRepeat = true;
    }

    if (nchk.wasZPressed() || autoRepeat)
    {
        if (playingNote)
            midi_note_off(channel, playingNote);

        midi_note_on(channel, note);
        playingNote = note;
        lastNoteMs = nowMs;

        ledState = 1 - ledState;
        gpio_put(LedPin, ledState);
    }
    else if (nchk.getBtnC() && playingNote)
    {
        midi_note_off(channel, playingNote);            
        playingNote = 0;
    }
}


int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_usb_init();

    // initialise MIDI on UART 0, gpio 0 & 1
    uart_init(uart0, 31250);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    // cancel any previous notes
    for (byte note=1; note<120; ++note)
        midi_note_off(channel, note);

    initScale();

    //sleep_ms(3000);
    printf("midisister booting...\n");

    i2c_init(i2c0, I2C_Baud);
    gpio_set_function(I2C_SDA_Pin, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Pin, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Pin);
    gpio_pull_up(I2C_SCL_Pin);

    initError();

    printf("trying to talk to the nunchuk...\n");
    Nunchuk nchk(i2c0);
    
    lastMs = millis();

    for(;;)
    {
        loop(nchk);
    }

    return 0;
}
