#pragma once

#include "util.h"
#include <vector>


class Nunchuk;


enum class Key : uint8_t
{
    C,  Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B,
};


enum class Input : uint8_t
{
    JoyX, JoyXNeg, JoyXPos,
    JoyY, JoyYNeg, JoyYPos,
    AccelX, AccelY, AccelZ,
};

enum class Dest : uint8_t
{
    ControlChange,
    PitchBend,
    Note,
};

struct Mapping
{
    Input input = Input::JoyY;
    Dest destType = Dest::ControlChange;
    uint16_t destParam = 1;

    float fromLo = -1.0f;
    float fromHi = 1.0f;
    uint16_t toLo = 0;
    uint16_t toHi = 127;

    uint16_t getVal(const Nunchuk& nchk) const;
};


// config description looks like:
// CHAN 1 ROOT C SCALE 0 0 0 1 5 7 11 OCTAVES 2 7 BPM 100 DIV 0.5 MAP ax -1 1 36 100 note MAP jx- cc 16 MAP jx+ cc 19 MAP jy pb MAP ay cc 17 MAP az 1 -1 0 127 cc 18

class Config
{
public:
    using Notes = std::vector<byte>;
    static const uint MaxMappings = 10;

    bool parse(const char* config);

    bool areNotesEnabled() const        { return notesMapping != nullptr; }
    uint8_t getMappedNote(const Nunchuk& nchk) const;
    byte quantiseNote(uint16_t incoming) const;

    byte getChannel() const             { return channel; }
    uint32_t getAutoRepeatMs() const    { return autoRepeatMs; }

    const Mapping* getMappings() const  { return mappings; }
    uint getNumMappings() const         { return numMappings; }

private:
    void parseScale(const char*& str);
    void refreshScaleNotes();

private:
    static constexpr uint MaxScaleNotes = 16;

    byte channel = 1;   // 0-f  =>  1-16

    Key key = Key::C;
    byte scaleNotes[MaxScaleNotes] = {};
    byte numScaleNotes = 0;
    byte bpm = 100;
    byte firstOctave = 2;
    byte lastOctave = 7;
    float division = 0.5f;
    
    Mapping mappings[MaxMappings] = {};
    byte numMappings = 0;

    // computed based on the above
    Notes validNotes;
    uint32_t autoRepeatMs = 250;
    const Mapping* notesMapping = nullptr;
};
