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

using byte = uint8_t;

constexpr int LedPin = 25;
constexpr int ErrorPin = 18;
constexpr int I2C_SDA_Pin = 4;
constexpr int I2C_SCL_Pin = 5;
constexpr int I2C_Baud = 100 * 1000;


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


// based on info from https://www.xarg.org/2016/12/using-a-wii-nunchuk-with-arduino/
class Nunchuk
{
    static constexpr byte NunchukAddress = 0x52;
    static constexpr byte StateAddr = 0;
    static constexpr byte CalibrationAddr = 0x20;
    static constexpr byte IdentAddr = 0xFA;

public:
    Nunchuk(i2c_inst_t* i2cBlock)
        : m_i2cBlock(i2cBlock)
    {
        initNoEncryption();
        getIdent();
        getCalibration();
    }

    void update()
    {
        byte buf[6];
        readBlocking(StateAddr, buf);

        RawState raw;
        raw.setFromBuf(buf);

        m_state.set(raw, m_cal);
        m_state.dump();
        puts("    <-- ");
        raw.dump();
        puts("\n");
    }

private:
    template<typename Buf>
    void writeBlocking(const Buf& buf)
    {
        int nbytes = sizeof(buf);
        
        // puts("..writing ");
        // print_buf(buf);
        // puts("\n");

        int nwritten = i2c_write_blocking(m_i2cBlock, NunchukAddress, get_buf_ptr(buf), nbytes, false);
        if (nwritten != nbytes)
        {
            onError();
            printf("tried to write %dB but wrote %d\n", nbytes, nwritten);
        }
    }

    template<typename Buf>
    void readBlocking(byte addr, Buf& buf)
    {
        writeBlocking(addr);

        sleep_ms(3);

        int nbytes = sizeof(buf);
        int nread = i2c_read_blocking(m_i2cBlock, NunchukAddress, get_buf_ptr(buf), nbytes, false);
        if (nread != nbytes)
        {
            onError();
            printf("tried to read %dB but wrote %d\n", nbytes, nread);
        }
    }

    void initNoEncryption()
    {
        const byte InitStr[] = { 0xf0, 0x55 };
        const byte DisableEncryptionStr[] = { 0xfb, 0x00 };

        sleep_ms(100);
        writeBlocking(InitStr);
        sleep_ms(100);
        writeBlocking(DisableEncryptionStr);
        sleep_ms(100);
    }

    void getIdent()
    {
        byte ident[6];
        readBlocking(IdentAddr, ident);

        print_buf(ident);
        puts("\n");
    }

    void getCalibration()
    {
        byte buf[16];
        readBlocking(CalibrationAddr, buf);

        m_cal.setFromBuf(buf);

        puts("calibration: ");
        m_cal.dump();
        puts("\n");
    }

private:
    struct Calibration
    {
        struct JoyAxis
        {
            uint8_t  min, ctr, max;
            float recipNeg, recipPos;

            void precalc();
            inline float parseRaw(byte raw) const;
        };

        struct AccelAxis
        {
            uint16_t zeroG, oneG;
            float recipOneG;

            void precalc();
            inline float parseRaw(uint16_t raw) const;
        };

        AccelAxis accelX, accelY, accelZ;
        JoyAxis  joyX, joyY;
        
        void setFromBuf(const byte* buf);
        void dump() const;
    };

    struct RawState
    {
        uint8_t  joyX, joyY;
        bool     btnC, btnZ;
        uint16_t accelX, accelY, accelZ;

        void setFromBuf(const byte* buf);
        void dump() const;
    };

    struct State
    {
        float joyX, joyY;
        float accelX, accelY, accelZ;
        bool  btnC, btnZ;

        void set(const RawState& raw, const Calibration& cal);
        void dump() const;
    };

    i2c_inst_t* m_i2cBlock;

    Calibration m_cal;
    State       m_state;
};

void Nunchuk::RawState::setFromBuf(const byte* buf)
{
    joyX = buf[0];
    joyY = buf[1];
    accelX = int(buf[2]) << 2;
    accelY = int(buf[3]) << 2;
    accelZ = int(buf[4]) << 2;

    byte b = buf[5];
    btnZ = (b & 1) == 0;
    b >>= 1;
    btnC = (b & 1) == 0;
    b >>= 1;
    accelX |= b & 3;
    b >>= 2;
    accelY |= b & 3;
    b >>= 2;
    accelZ |= b;
}

void Nunchuk::RawState::dump() const
{
    printf("joy %d,%d  accel %d,%d,%d  %c %c",
        int(joyX)-128, int(joyY)-128,
        int(accelX), int(accelY), int(accelZ),
        btnC?'C':'c', btnZ?'Z':'z');
}

void Nunchuk::Calibration::JoyAxis::precalc()
{
    int negRange = ctr - min;
    recipNeg = 1.0f / float(negRange);
    int posRange = max - ctr;
    recipPos = 1.0f / float(posRange);
}

void Nunchuk::Calibration::AccelAxis::precalc()
{
    int oneG = oneG - zeroG;
    recipOneG = 1.0f / float(oneG);
}

void Nunchuk::Calibration::setFromBuf(const byte* buf)
{
    accelX.zeroG = int(buf[0]) << 2;
    accelY.zeroG = int(buf[1]) << 2;
    accelZ.zeroG = int(buf[2]) << 2;
    byte lsb = buf[3];
    accelX.zeroG |= lsb & 3;
    lsb >>= 2;
    accelY.zeroG |= lsb & 3;
    lsb >>= 2;
    accelZ.zeroG |= lsb & 3;

    accelX.oneG = int(buf[4]) << 2;
    accelY.oneG = int(buf[5]) << 2;
    accelZ.oneG = int(buf[6]) << 2;
    lsb = buf[7];
    accelX.oneG |= lsb & 3;
    lsb >>= 2;
    accelY.oneG |= lsb & 3;
    lsb >>= 2;
    accelZ.oneG |= lsb & 3;

    joyX.max = buf[8];
    joyX.min = buf[9];
    joyX.ctr = buf[10];
    joyY.max = buf[11];
    joyY.min = buf[12];
    joyY.ctr = buf[13];

    accelX.precalc();
    accelY.precalc();
    accelZ.precalc();
    joyX.precalc();
    joyY.precalc();
}

void Nunchuk::Calibration::dump() const
{
    printf("accelZeroG %d,%d,%d   accelOneG %d,%d,%d   joyX %d,%d,%d   joyY %d,%d,%d",
        int(accelX.zeroG), int(accelY.zeroG), int(accelZ.zeroG),
        int(accelX.oneG), int(accelY.oneG), int(accelZ.oneG),
        int(joyX.min)-128, int(joyX.ctr)-128, int(joyX.max)-128, 
        int(joyY.min)-128, int(joyY.ctr)-128, int(joyY.max)-128);
}

inline float Nunchuk::Calibration::JoyAxis::parseRaw(byte raw) const
{
    constexpr float deadzone = 0.1f;
    if (raw < ctr)
    {
        const float fullVal = (ctr - raw) * recipNeg;
        const float val = (fullVal - deadzone) * (1.0f  / (1.0f - deadzone));
        return -1.f * std::clamp(val, 0.f, 1.f);
    }
    else
    {
        const float fullVal = (raw - ctr) * recipPos;
        const float val = (fullVal - deadzone) * (1.0f  / (1.0f - deadzone));
        return std::clamp(val, 0.f, 1.f);
    }
}

inline float Nunchuk::Calibration::AccelAxis::parseRaw(uint16_t raw) const
{
    int centred = raw - zeroG;
    return float(centred) * recipOneG;
}

void Nunchuk::State::set(const RawState& raw, const Calibration& cal)
{
    accelX = cal.accelX.parseRaw(raw.accelX);
    accelY = cal.accelY.parseRaw(raw.accelY);
    accelZ = cal.accelZ.parseRaw(raw.accelZ);

    joyX = cal.joyX.parseRaw(raw.joyX);
    joyY = cal.joyY.parseRaw(raw.joyY);

    btnC = raw.btnC;
    btnZ = raw.btnZ;
}

void Nunchuk::State::dump() const
{
    printf("joy %.03f,%.03f  accel %.03f,%.03f,%.03f  %c %c",
        joyX, joyY,
        accelX, accelY, accelZ,
        btnC?'C':'c', btnZ?'Z':'z');
}


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
