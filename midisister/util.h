#pragma once

#include <cstdio>
#include <type_traits>
#include "pico/stdlib.h"


using byte = uint8_t;


class StdinAsync
{
public:
    using LineHandler = void(*)(const char*);

private:
    static constexpr uint MaxLineLength=1024;
    char m_buffer[MaxLineLength + 1];
    uint m_readPos = 0;
    bool m_overflowed = false;

    LineHandler m_lineFn = nullptr;

public:
    StdinAsync(LineHandler handler) : m_lineFn(handler) { /**/ }

    // NB. calls line handler when a complete line is received
    void update();
};



void initError();
void onError();
void clearError();
bool hasErrorHappened();


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



template<typename Integral>
inline Integral div_round_up(Integral val, Integral boundary)
{
    return (val + boundary - 1) / boundary;
}
