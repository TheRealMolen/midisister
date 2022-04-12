#include "nunchuk.h"
#include "util.h"

#include <algorithm>


Nunchuk::Nunchuk(i2c_inst_t* i2cBlock)
    : m_i2cBlock(i2cBlock)
{
    initNoEncryption();
    getIdent();
    getCalibration();
}

void Nunchuk::update()
{
    m_prevState = m_state;

    byte buf[6];
    readBlocking(StateAddr, buf);

    RawState raw;
    raw.setFromBuf(buf);

    m_state.set(raw, m_cal);
    sleep_ms(3);

    //m_state.dump();
    //printf("\r");
    // printf("    <-- ");
    // raw.dump();
    //puts("\n");
}


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


void Nunchuk::Calibration::setFromBuf(const byte* buf)
{
    accelX.zeroG = int(buf[0]) << 2;
    accelY.zeroG = int(buf[1]) << 2;
    accelZ.zeroG = int(buf[2]) << 2;
    byte lsb = buf[3];
    accelZ.zeroG |= lsb & 3;
    lsb >>= 2;
    accelY.zeroG |= lsb & 3;
    lsb >>= 2;
    accelX.zeroG |= lsb & 3;

    accelX.oneG = int(buf[4]) << 2;
    accelY.oneG = int(buf[5]) << 2;
    accelZ.oneG = int(buf[6]) << 2;
    lsb = buf[7];
    accelZ.oneG |= lsb & 3;
    lsb >>= 2;
    accelY.oneG |= lsb & 3;
    lsb >>= 2;
    accelX.oneG |= lsb & 3;

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

void Nunchuk::Calibration::JoyAxis::precalc()
{
    int negRange = ctr - min;
    recipNeg = 1.0f / float(negRange);
    int posRange = max - ctr;
    recipPos = 1.0f / float(posRange);
}

inline float Nunchuk::Calibration::JoyAxis::parseRaw(byte raw) const
{
    constexpr float deadzone = 0.1f;
    if (raw < ctr)
    {
        const float fullVal = (ctr - raw) * recipNeg;
        const float val = (fullVal - deadzone) * (1.0f  / (1.0f - (2.f * deadzone)));
        return -1.f * std::clamp(val, 0.f, 1.f);
    }
    else
    {
        const float fullVal = (raw - ctr) * recipPos;
        const float val = (fullVal - deadzone) * (1.0f  / (1.0f - (2.f * deadzone)));
        return std::clamp(val, 0.f, 1.f);
    }
}


void Nunchuk::Calibration::AccelAxis::precalc()
{
    int deltaG = oneG - zeroG;
    recipOneG = 1.0f / float(deltaG);
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


template<typename Buf>
void Nunchuk::writeBlocking(const Buf& buf)
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
void Nunchuk::readBlocking(byte addr, Buf& buf)
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

void Nunchuk::initNoEncryption()
{
    const byte InitStr[] = { 0xf0, 0x55 };
    const byte DisableEncryptionStr[] = { 0xfb, 0x00 };

    sleep_ms(100);
    writeBlocking(InitStr);
    sleep_ms(100);
    writeBlocking(DisableEncryptionStr);
    sleep_ms(100);
}

void Nunchuk::getIdent()
{
    byte ident[6];
    readBlocking(IdentAddr, ident);

    print_buf(ident);
    puts("\n");
}

void Nunchuk::getCalibration()
{
    byte buf[16];
    readBlocking(CalibrationAddr, buf);

    m_cal.setFromBuf(buf);

    // puts("calibration: ");
    // m_cal.dump();
    // puts("\n");
}
