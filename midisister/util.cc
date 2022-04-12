#include "util.h"

#include "pico/stdlib.h"


constexpr int ErrorPin = 18;


void initError()
{
    gpio_init(ErrorPin);
    gpio_set_dir(ErrorPin, GPIO_OUT);
    gpio_put(ErrorPin, 0);
}

void onError()
{
    gpio_put(ErrorPin, 1);
}
void clearError()
{
    gpio_put(ErrorPin, 0);
}
