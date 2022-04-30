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

    float getJoyX() const       { return m_state.joyX; }
    float getJoyY() const       { return m_state.joyY; }
    float getAccelX() const     { return m_state.accelX; }
    float getAccelY() const     { return m_state.accelY; }
    float getAccelZ() const     { return m_state.accelZ; }
    bool  getBtnC() const       { return m_state.btnC; }
    bool  getBtnZ() const       { return m_state.btnZ; }

    bool wasCPressed() const    { return m_state.btnC && !m_prevState.btnC; }
    bool wasZPressed() const    { return m_state.btnZ && !m_prevState.btnZ; }
    bool wasCReleased() const   { return !m_state.btnC && m_prevState.btnC; }
    bool wasZReleased() const   { return !m_state.btnZ && m_prevState.btnZ; }

private:
    template<typename Buf>
    bool writeBlocking(const Buf& buf);

    template<typename Buf>
    bool readBlocking(byte addr, Buf& buf);

    bool init();
    bool initNoEncryption();
    bool getIdent();
    bool getCalibration();

    void onError();

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
        bool  btnC=false, btnZ=false;

        void set(const RawState& raw, const Calibration& cal);
        void dump() const;
    };

    i2c_inst_t* m_i2cBlock;

    bool        m_ready = false;
    bool        m_error = false;

    Calibration m_cal;
    State       m_state;
    State       m_prevState;
};

