#pragma once

#include <cstdlib>
#include "hardware/i2c.h"
#include "util.h"



// based on info from https://www.xarg.org/2016/12/using-a-wii-nunchuk-with-arduino/
class Nunchuk
{
    static constexpr byte NunchukAddress = 0x52;
    static constexpr byte StateAddr = 0;
    static constexpr byte CalibrationAddr = 0x20;
    static constexpr byte IdentAddr = 0xFA;

public:
    Nunchuk(i2c_inst_t* i2cBlock);

    void update();

private:
    template<typename Buf>
    void writeBlocking(const Buf& buf);

    template<typename Buf>
    void readBlocking(byte addr, Buf& buf);

    void initNoEncryption();
    void getIdent();
    void getCalibration();

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

