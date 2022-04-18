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
            configBuf[0] = 0;
            puts("updated config");
            is_flash_save_valid();
        }
        else
        {
            const char* configBuf;
            if (is_flash_save_valid())
            {
                puts("reverting to saved");
                configBuf = get_flash_save_data();
            }
            else
            {
                puts("reverting to default");
                configBuf = defaultConfigStr;
            }

            config.parse(configBuf);
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

int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_usb_init();

    midi_init(uart0, UART_TX_Gpio, UART_RX_Gpio);
    
    const char* configStr = is_flash_save_valid() ? get_flash_save_data() : defaultConfigStr;
    config.parse(configStr);

    // cancel any previous notes
    for (byte note=1; note<120; ++note)
        midi_note_off(config.getChannel(), note);

    i2c_init(I2C_Block, I2C_Baud);
    gpio_set_function(I2C_SDA_Gpio, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Gpio, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Gpio);
    gpio_pull_up(I2C_SCL_Gpio);

    initError();

    Nunchuk nchk(I2C_Block);
    
    lastMs = millis();

    for(;;)
    {
        loop(nchk);
    }

    return 0;
}
