//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "config.h"
#include "flash_save.h"
#include "midi.h"
#include "nunchuk.h"
#include "util.h"

using std::begin, std::end;


constexpr int LedPin = 25;

i2c_inst_t* const I2C_Block = i2c1;
constexpr int I2C_SDA_Gpio = 26;
constexpr int I2C_SCL_Gpio = 27;
constexpr int I2C_Baud = 100 * 1000;

uart_inst_t* const UART_Block = uart0;
constexpr int UART_TX_Gpio = 16;
constexpr int UART_RX_Gpio = 17;

uint32_t lastMs = 0;

byte ledState = 0;

byte playingNote = 0;
int lastNoteMs = 0;

Config config;
uint16_t lastOutputVals[Config::MaxMappings] = {};

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

    END.
)END";


void hexdump(const void* start, uint len)
{
    auto charify = [](uint c) -> char { return (c >= 32 && c < 128) ? char(c) : ' '; };
    uint addr = 0;
    for(auto c = (const uint8_t*)start; c < ((const uint8_t*)start + len); c += 16, addr+=16)
    {
        printf("%08x: %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x %02x%02x  %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
            addr,
            uint(c[0]), uint(c[1]),   uint(c[2]), uint(c[3]),   uint(c[4]), uint(c[5]),   uint(c[6]), uint(c[7]), 
            uint(c[8]), uint(c[9]),   uint(c[10]), uint(c[11]), uint(c[12]), uint(c[13]), uint(c[14]), uint(c[15]),
            charify(c[0]), charify(c[1]),   charify(c[2]), charify(c[3]),   charify(c[4]), charify(c[5]),   charify(c[6]), charify(c[7]), 
            charify(c[8]), charify(c[9]),   charify(c[10]), charify(c[11]), charify(c[12]), charify(c[13]), charify(c[14]), charify(c[15])
            );
    }
}

constexpr uint MaxConfigSize = 4 * 1024;
char configBuf[MaxConfigSize] = {};
void onLineRead(const char* line)
{
    printf("read line '%s'\n", line);
    if (strncmp("dump", line, 4) == 0 && configBuf[0] == 0)
    {
        if (is_flash_save_valid())
            printf("saved config:--\n%s\n----------\n", get_flash_save_data());
        else
            puts("no data saved in flash");
        return;
    }
    else if (strncmp("hdmp", line, 4) == 0 && configBuf[0] == 0)
    {
        constexpr ptrdiff_t Flash_SaveBufOffset = (2*1024*1024) - (100*1024);
        const uint8_t* saveBuf = (const uint8_t*)(XIP_BASE + Flash_SaveBufOffset);
        hexdump(saveBuf, 512 + 64);
    }

    strcat(configBuf, line);
    if (!strstr(line, "END."))
    {
        strcat(configBuf, "\n");
    }
    else
    {
        if (config.parse(configBuf))
        {
            save_flash_data((const uint8_t*)configBuf);
            memset(configBuf, 0, sizeof(configBuf));
            puts("updated config");
            is_flash_save_valid();
        }
        else
        {
            const char* fallbackConfigBuf;
            if (is_flash_save_valid())
            {
                puts("reverting to saved");
                fallbackConfigBuf = get_flash_save_data();
            }
            else
            {
                puts("reverting to default");
                fallbackConfigBuf = defaultConfigStr;
            }

            config.parse(fallbackConfigBuf);
            // restore the error indicator
            onError();
        }
    }
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
        uint16_t note = config.getMappedNote(nchk);

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

int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_usb_init();

    midi_init(uart0, UART_TX_Gpio, UART_RX_Gpio);
    
    const char* configStr = is_flash_save_valid() ? get_flash_save_data() : defaultConfigStr;
    config.parse(configStr);

    i2c_init(I2C_Block, I2C_Baud);
    gpio_set_function(I2C_SDA_Gpio, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Gpio, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Gpio);
    gpio_pull_up(I2C_SCL_Gpio);

    // cancel any previous notes
    sleep_ms(1);
    for (byte note=1; note<120; ++note)
        midi_note_off(config.getChannel(), note);

    initError();

    Nunchuk nchk(I2C_Block);
    
    lastMs = millis();

    for(;;)
    {
        loop(nchk);
    }

    return 0;
}
