add_executable(midisister
        midisister.cc
        nunchuk.cc
        util.cc
        )

# Pull in our (to be renamed) simple get you started dependencies
target_link_libraries(midisister pico_stdlib hardware_i2c)

# enable usb output, disable uart output
pico_enable_stdio_usb(midisister 1)
pico_enable_stdio_uart(midisister 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(midisister)

