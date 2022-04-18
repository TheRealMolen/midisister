#pragma once

#include <cstdint>


bool is_flash_save_valid();
const char* get_flash_save_data();
void save_flash_data(const uint8_t* data);