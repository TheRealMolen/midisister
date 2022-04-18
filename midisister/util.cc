#include "util.h"

#include "pico/stdlib.h"


constexpr int ErrorPin = 18;

static bool errorStatus = false;

void initError()
{
    gpio_init(ErrorPin);
    gpio_set_dir(ErrorPin, GPIO_OUT);
    gpio_put(ErrorPin, 0);
    errorStatus = false;
}

void onError()
{
    gpio_put(ErrorPin, 1);
    errorStatus = true;
}
void clearError()
{
    gpio_put(ErrorPin, 0);
    errorStatus = false;
}
bool hasErrorHappened()
{
    return errorStatus;
}


void StdinAsync::update()
{
    for (;;)
    {
        int nextChar = getchar_timeout_us(0);
        if (nextChar == PICO_ERROR_TIMEOUT)
            return;

        if (nextChar == '\n' || nextChar == '\r')
        {
            if (m_overflowed)
            {
                m_overflowed = false;
                m_readPos = 0;
            }
            else if (m_readPos > 0)
            {
                m_buffer[m_readPos] = 0;
                m_lineFn(m_buffer);

                m_readPos = 0;
            }
        }
        else if (m_overflowed)
        {
            continue;
        }
        else if ((m_readPos + 1) < MaxLineLength)
        {
            m_buffer[m_readPos] = char(nextChar);
            ++m_readPos;
        }
        else
        {
            puts("ERROR: line too long; dropping it all");
            onError();
            m_readPos = 0;
            m_overflowed = true;
        }
    }
}
