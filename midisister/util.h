#pragma once

#include <cstdio>
#include <type_traits>
#include "pico/stdlib.h"


using byte = uint8_t;


void initError();
void onError();


inline uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}



template<typename Buf>
byte* get_buf_ptr(Buf& buf)
{
    if constexpr (std::is_array<Buf>::value)
        return &buf[0];
    else
        return &buf;
}
template<typename Buf>
const byte* get_buf_ptr(const Buf& buf)
{
    if constexpr (std::is_array<Buf>::value)
        return &buf[0];
    else
        return &buf;
}

template<typename Buf>
void print_buf(const Buf& buf)
{
    int nbytes = sizeof(buf);
    
    auto p = get_buf_ptr(buf);
    auto end = p + nbytes;
    for (; p != end; ++p)
        printf("%02x ", *p);
}
