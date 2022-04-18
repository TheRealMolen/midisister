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
#include "hardware/sync.h"
#include "hardware/uart.h"

// workaround for flash header snafu
extern "C" {
#include "hardware/flash.h"
}

#include "config.h"
#include "nunchuk.h"
#include "util.h"

using std::begin, std::end;

constexpr ptrdiff_t Flash_SaveBufSize = 100 * 1024;
struct FlashSave
{
    static constexpr uint32_t Magic = 'NUN1';
    // NOTE because we're writing to flash and con only change 1->0, these are inverted
    enum Flags : uint32_t
    {
        Flag_None = ~0u,
        Flag_Invalid = 1 << 0,
    };

    uint32_t magic;
    uint32_t flags;
    uint32_t length;
    char data[];
};
static_assert(sizeof(FlashSave) == 12);
constexpr uint32_t Flash_MaxDataSize = Flash_SaveBufSize - sizeof(FlashSave);
constexpr ptrdiff_t Flash_SaveBufOffset = PICO_FLASH_SIZE_BYTES - Flash_SaveBufSize;
FlashSave* Flash_SaveBuf = (FlashSave*)(XIP_BASE + Flash_SaveBufOffset);

bool is_flash_save_valid()
{
    if (Flash_SaveBuf->magic != FlashSave::Magic)
    {
        puts("FLASH: nomagic");
        return false;
    }

    if ((Flash_SaveBuf->flags & FlashSave::Flag_Invalid) != 0)
    {
        puts("FLASH: invalid");
        return false;
    }

    return true;
}

const char* get_flash_save_data()
{
    if (!is_flash_save_valid())
    {
        puts("ERR: trying to read invalid flash");
        onError();
    }

    return Flash_SaveBuf->data;
}

template<typename Integral>
inline Integral div_round_up(Integral val, Integral boundary)
{
    return (val + boundary - 1) / boundary;
}

void save_flash_data(const uint8_t* data)
{
    const uint32_t dataLength = strlen((const char*)data) + 1;
    const uint32_t writeLength = sizeof(FlashSave) + dataLength;
    if (writeLength + 1 >= Flash_MaxDataSize)
    {
        puts("ERR: save data too big for buffer");
        onError();
        return;
    }

    uint32_t firstPageDataLen = std::min<uint32_t>(dataLength, FLASH_PAGE_SIZE - sizeof(FlashSave));

    // we need to set up our first page with the header on it
    uint32_t firstPageBuf[FLASH_PAGE_SIZE / sizeof(uint32_t)];
    FlashSave* bufToWrite = reinterpret_cast<FlashSave*>(firstPageBuf);
    bufToWrite->magic = FlashSave::Magic;
    bufToWrite->length = dataLength;
    memcpy(bufToWrite->data, data, firstPageDataLen);

    if (writeLength <= FLASH_PAGE_SIZE)
    {
        bufToWrite->flags &= ~FlashSave::Flag_Invalid;
        
        static_assert(FLASH_SECTOR_SIZE >= FLASH_PAGE_SIZE);
        printf("writing single flash page; %u bytes\n", writeLength);

        uint32_t savedIntrMask = save_and_disable_interrupts();
        flash_range_erase(Flash_SaveBufOffset, 1 * FLASH_SECTOR_SIZE);
        flash_range_program(Flash_SaveBufOffset, (uint8_t*)firstPageBuf, FLASH_PAGE_SIZE);
        restore_interrupts(savedIntrMask);
        return;
    }

    bufToWrite->flags = FlashSave::Flag_None;       // note: we first write it invalid, then rewrite this page at the end set to valid

    const uint32_t extraWriteLen = dataLength - firstPageDataLen;
    const uint32_t numPages = div_round_up<uint32_t>(writeLength, FLASH_PAGE_SIZE);
    const uint32_t numExtraPages = numPages - 1;
    const uint32_t numSectors = div_round_up<uint32_t>(numPages * FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE);

    printf("writing flash; %u bytes, %u pages, %u sector%s\n", writeLength, numPages, numSectors, (numSectors != 1) ? "s" : "");

    uint32_t savedIntrMask = save_and_disable_interrupts();
    flash_range_erase(Flash_SaveBufOffset, numSectors * FLASH_SECTOR_SIZE);
    // write page 1 invalid
    flash_range_program(Flash_SaveBufOffset, (uint8_t*)firstPageBuf, FLASH_PAGE_SIZE);
    // write remainder
    const uint8_t* sourcePtr = data + firstPageDataLen;
    flash_range_program(Flash_SaveBufOffset + FLASH_PAGE_SIZE, sourcePtr, numExtraPages * FLASH_PAGE_SIZE);
    // rewrite page 1 valid
    bufToWrite->flags &= ~FlashSave::Flag_Invalid;
    flash_range_program(Flash_SaveBufOffset, (uint8_t*)firstPageBuf, FLASH_PAGE_SIZE);
    restore_interrupts(savedIntrMask);
}



constexpr int LedPin = 25;

i2c_inst_t* const I2C_Block = i2c1;
constexpr int I2C_SDA_Gpio = 26;
constexpr int I2C_SCL_Gpio = 27;
constexpr int I2C_Baud = 100 * 1000;

uart_inst_t* const UART_Block = uart0;
constexpr int UART_TX_Gpio = 16;
constexpr int UART_RX_Gpio = 17;


void midi_note_on(uint8_t channel, uint8_t note, uint8_t vel = 127)
{
    uint8_t message[3] = { uint8_t(0x90 | channel), note, vel };
    uart_write_blocking(UART_Block, message, 3);
    printf(">NOTEON:%d,%d,%d\n", int(channel), int(note), int(vel));
}
void midi_note_off(uint8_t channel, uint8_t note)
{
    uint8_t message[3] = { uint8_t(0x80 | channel), note, 0 };
    uart_write_blocking(UART_Block, message, 3);
    printf(">NOTEOFF:%d,%d\n", int(channel), int(note));
}
void midi_pitchbend(uint8_t channel, uint16_t pitchbend)
{
    uint8_t lsb = uint8_t(pitchbend & 0x7f);
    uint8_t msb = uint8_t(pitchbend >> 7);
    uint8_t message[3] = { uint8_t(0xe0 | channel), lsb, msb };
    uart_write_blocking(UART_Block, message, 3);
    //printf(">PB:%d,%d, %02x:%02x\n", int(channel), int(pitchbend), int(msb), int(lsb));
}
void midi_cc(uint8_t channel, uint8_t cc, uint8_t val)
{
    uint8_t message[3] = { uint8_t(0xB0 | channel), cc, val };
    uart_write_blocking(UART_Block, message, 3);
}

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

    // initialise MIDI uart
    uart_init(UART_Block, 31250);
    gpio_set_function(UART_TX_Gpio, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_Gpio, GPIO_FUNC_UART);

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
