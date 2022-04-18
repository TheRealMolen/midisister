#include "flash_save.h"
#include "util.h"

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <algorithm>
#include <cstring>


// workaround for flash header snafu
extern "C" {
#include "hardware/flash.h"
}

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


