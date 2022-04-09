//********************************************************************************
//  **  m i d i s i s t e r  **
//
// connect nunchuck data to GPIO4(pin6) and clock to GPIO5(pin7)
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "util.h"
#include "nunchuk.h"


constexpr int LedPin = 25;
constexpr int I2C_SDA_Pin = 4;
constexpr int I2C_SCL_Pin = 5;
constexpr int I2C_Baud = 100 * 1000;



int main() {
    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);
    gpio_put(LedPin, 1);

    stdio_init_all();

    sleep_ms(4000);
    printf("midisister booting...\n");

    i2c_init(i2c0, I2C_Baud);
    gpio_set_function(I2C_SDA_Pin, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_Pin, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_Pin);
    gpio_pull_up(I2C_SCL_Pin);


    for (;;)
    {
        initError();

        printf("trying to talk to the nunchuk...\n");
        Nunchuk nchk(i2c0);

        for (int i=0; i<6; ++i)
        {
            nchk.update();

            gpio_put(LedPin, 0);
            sleep_ms(250);
            gpio_put(LedPin, 1);
            sleep_ms(250);
        }
    }

    return 0;
}
