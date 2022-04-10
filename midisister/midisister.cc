//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cmath>
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
void midi_pitchbend(uint8_t channel, int16_t pitchbend)
{
    uint16_t offset = uint16_t(std::clamp<int>(pitchbend + 8192, 0, 16383));
    uint8_t lsb = uint8_t(offset & 0x7f);
    uint8_t msb = uint8_t(offset >> 7);
    uint8_t message[3] = { uint8_t(0xe0 | channel), lsb, msb };
    uart_write_blocking(uart0, message, 3);
    //printf(">PB:%d,%d, %02x:%02x\n", int(channel), int(offset), int(msb), int(lsb));
}
void midi_cc(uint8_t channel, uint8_t cc, uint8_t val)
{
    uint8_t message[3] = { uint8_t(0xB0 | channel), cc, val };
    uart_write_blocking(uart0, message, 3);
}


byte channel = 1;
byte playingNote = 0;
byte ledState = 0;
int16_t lastPitchBend = 0;
byte lastYcc = 64;
byte lastZcc = 64;
byte lastNegXcc = 127;
byte lastPosXcc = 0;
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
    for (byte octave=lowOctave; octave<=hiOctave; ++octave)
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
    if (!deltaMs)
    {
        sleep_ms(1);
        return;
    }
    lastMs = nowMs;

    nchk.update();
        
    float x = std::clamp(0.5f * (1.0f + nchk.getAccelX()), 0.f, 1.f);
    byte rawNote = byte(x * 64.f + 36.f);
    byte note = quantize(rawNote);

    bool autoRepeat = false;
    if (nchk.getBtnC() && nchk.getBtnZ())
    {
        int timeSinceLastNoteMs = nowMs - lastNoteMs;
        if (timeSinceLastNoteMs >= autoRepeatMs && note != playingNote)
            autoRepeat = true;
    }

    if (nchk.wasZPressed() || autoRepeat)
    {
        //printf("   accel=%f  x=%f  raw=%d  note=%d\n", nchk.getAccelX(), x, int(rawNote), int(note));

        if (playingNote)
            midi_note_off(channel, playingNote);

        midi_note_on(channel, note);
        playingNote = note;
        lastNoteMs = nowMs;

        ledState = 1 - ledState;
        gpio_put(LedPin, ledState);
    }

    if (nchk.wasZReleased() && playingNote)
    {
        midi_note_off(channel, playingNote);            
        playingNote = 0;
    }

    if (fabsf(nchk.getJoyY()) > 0.001f)
    {
        float rawPitchBend = nchk.getJoyY();
        int16_t pitchBend = int16_t(rawPitchBend * 8192.f);
        if (pitchBend != lastPitchBend)
        {
            midi_pitchbend(channel, pitchBend);
            lastPitchBend = pitchBend;
        }
    }

    byte ycc = byte(std::clamp(int(((nchk.getAccelY() + 1.f) * 0.5f) * 127.f), 0, 127));
    if (ycc != lastYcc)
    {
        midi_cc(channel, 54, ycc);
        lastYcc = ycc;
    }
    byte zcc = byte(std::clamp(int(((nchk.getAccelZ() + 1.f) * 0.5f) * 127.f), 0, 127));
    if (zcc != lastZcc)
    {
        midi_cc(channel, 55, zcc);
        lastZcc = zcc;
    }

    byte negXcc = byte(std::clamp(int(((nchk.getJoyX() + 1.f)) * 127.f), 0, 127));
    if (negXcc != lastNegXcc)
    {
        midi_cc(channel, 43, negXcc);
        lastNegXcc = negXcc;
    }
    byte posXcc = byte(std::clamp(int(nchk.getJoyX() * 127.f), 0, 127));
    if (posXcc != lastPosXcc)
    {
        midi_cc(channel, 19, 127 - posXcc);
        lastPosXcc = posXcc;
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
