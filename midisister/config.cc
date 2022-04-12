#include "config.h"
#include "nunchuk.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>



float getRawVal(const Nunchuk& nchk, Input input)
{
    switch (input)
    {
        case Input::JoyX:      return nchk.getJoyX();
        case Input::JoyXNeg:   return std::max(-nchk.getJoyX(), 0.f);
        case Input::JoyXPos:   return std::max(nchk.getJoyX(), 0.f);

        case Input::JoyY:      return nchk.getJoyY();
        case Input::JoyYNeg:   return std::max(-nchk.getJoyY(), 0.f);
        case Input::JoyYPos:   return std::max(nchk.getJoyY(), 0.f);

        case Input::AccelX:    return nchk.getAccelX();
        case Input::AccelY:    return nchk.getAccelY();
        case Input::AccelZ:    return nchk.getAccelZ();
    }
    onError();
    return 0.f;
}

uint16_t remapClamped(float val, float fromLo, float fromHi, uint16_t toLo, uint16_t toHi)
{
    if (fabsf(fromHi - fromLo) < 0.001f)
        return toLo;

    float normalised = std::clamp((val - fromLo) / (fromHi - fromLo), 0.f, 1.f);
    float scaled = normalised * float(toHi - toLo);
    int final = int(scaled) + toLo;
    return uint16_t(std::clamp<int>(final, toLo, toHi));
}

uint16_t Mapping::getVal(const Nunchuk& nchk) const
{
    float raw = getRawVal(nchk, input);
    return remapClamped(raw, fromLo, fromHi, toLo, toHi);
}

void skipToWs(const char*& curr)
{
    while (*curr && !isspace(*curr))
        ++curr;
}
void skipWs(const char*& curr)
{
    while (*curr && isspace(*curr))
        ++curr;
}

template<typename T>
T parseIntegral(const char* start, const char** outEnd)
{
    int parsed = strtol(start, (char**)outEnd, 10);
    T val = T(parsed);
    if (parsed != int(val))
    {
        onError();
        return T(std::clamp<int>(parsed, std::numeric_limits<T>::min(), std::numeric_limits<T>::max()));
    }
    return val;
}

inline byte parseByte(const char* start, const char** outEnd)   { return parseIntegral<byte>(start, outEnd); }
inline uint16_t parseUShort(const char* start, const char** outEnd)   { return parseIntegral<uint16_t>(start, outEnd); }

inline float parseFloat(const char*& curr) 
{
    return strtof(curr, const_cast<char**>(&curr));
}

Key parseKey(const char*& str)
{
    Key key = Key::C;

    skipWs(str);
    switch(*str)
    {
        case 'A': case 'a': key = Key::A; break;
        case 'B': case 'b': key = Key::B; break;
        case 'C': case 'c': key = Key::C; break;
        case 'D': case 'd': key = Key::D; break;
        case 'E': case 'e': key = Key::E; break;
        case 'F': case 'f': key = Key::F; break;
        case 'G': case 'g': key = Key::G; break;

        default:
            printf("invalid key %c", *str);
            onError();
            return Key::C;
    }

    ++str;
    if (*str == 'b')
        key = Key((uint(key) + 12 - 1) % 12);
    else if (*str == '#')
        key = Key((uint(key) + 1) % 12);
    else if (*str && !isspace(*str))
    {
        printf("invalid key %c%c", *(str-1), *str);
        onError();
    }

    return key;
}

void Config::parseScale(const char*& curr)
{
    numScaleNotes = 0;
    skipWs(curr);

    while(isdigit(*curr) && numScaleNotes < MaxScaleNotes)
    {
        scaleNotes[numScaleNotes] = parseByte(curr, &curr);
        ++numScaleNotes;
        skipWs(curr);
    }
}

Input parseJoystick(const char*& curr, Input fullAxis, Input neg, Input pos)
{
    ++curr;
    char mod = *curr;
    if (!mod || isspace(mod))
        return fullAxis;

    skipToWs(curr);
    if (mod == '+')
        return pos;
    else if (mod == '-')
        return neg;
    onError();
    return fullAxis;
}

Input parseInput(const char*& curr)
{
    skipWs(curr);
    if (!*curr)
    {
        onError();
        skipToWs(curr);
        return Input::AccelX;
    }

    switch(*curr)
    {
        case 'A': case 'a':
        {
            ++curr;
            char axis = *curr;
            ++curr;
            switch (axis)
            {
                case 'X': case 'x': return Input::AccelX;
                case 'Y': case 'y': return Input::AccelY;
                case 'Z': case 'z': return Input::AccelZ;
            }
            onError();
            skipToWs(curr);
            return Input::AccelX;
        }

        case 'J': case 'j':
            ++curr;
            switch(*curr)
            {
                case 'X': case 'x': return parseJoystick(curr, Input::JoyX, Input::JoyXNeg, Input::JoyXPos);
                case 'Y': case 'y': return parseJoystick(curr, Input::JoyY, Input::JoyYNeg, Input::JoyYPos);
            }
            onError();
            skipToWs(curr);
            return Input::JoyX;
    }

    onError();
    skipToWs(curr);
    return Input::AccelX;
}

void parseMapping(Mapping& mapping, const char*& curr)
{
#define BAIL_ON_EOS     if (!*curr) { onError(); return; }

    mapping.input = parseInput(curr);
    skipWs(curr);   BAIL_ON_EOS;

    // optional remap values
    bool useDefaultRemap = true;
    if (!isalpha(*curr))
    {
        useDefaultRemap = false;
        mapping.fromLo = parseFloat(curr); BAIL_ON_EOS;
        mapping.fromHi = parseFloat(curr); BAIL_ON_EOS;
        mapping.toLo = parseUShort(curr, &curr); BAIL_ON_EOS;
        mapping.toHi = parseUShort(curr, &curr); BAIL_ON_EOS;
    }
    else
    {
        mapping.fromLo = -1.0f;
        mapping.fromHi = 1.0f;

        if (mapping.input == Input::JoyXNeg || mapping.input == Input::JoyXPos ||
            mapping.input == Input::JoyYNeg || mapping.input == Input::JoyYPos)
        {
            mapping.fromLo = 0.0f;
        }

        mapping.toLo = 0;
        mapping.toHi = 127;
    }

    skipWs(curr); BAIL_ON_EOS;
    const char* destStart = curr;
    skipToWs(curr); BAIL_ON_EOS;
    const char* destEnd = curr;
    switch(*destStart)
    {
        case 'C': case 'c':     // cc
            mapping.destType = Dest::ControlChange;
            mapping.destParam = parseByte(curr, &curr); BAIL_ON_EOS;
            break;
        
        case 'P': case 'p':     // pb
            mapping.destType = Dest::PitchBend;
            if (useDefaultRemap)
                mapping.toHi = 16383;
            break;

        case 'N': case 'n':     // note
            mapping.destType = Dest::Note;
            break;

        default:
            onError();
            *(char*)destEnd = 0;
            printf("unknown destination '%s'\n", destStart);
            return;
    }
}


void Config::initScale()
{
    validNotes.clear();
    validNotes.reserve((lastOctave - firstOctave + 1) * numScaleNotes);
    for (byte octave=firstOctave; octave<=lastOctave; ++octave)
    {
        byte offset = byte(key) + (octave * 12);
        for (uint i=0; i<numScaleNotes; ++i)
        {
            validNotes.push_back(offset + scaleNotes[i]);
        }
    }
}


void Config::parse(const char* config)
{
    // reset to blank
    *this = Config{};
    clearError();

    const char* curr = config;

    for (;;)
    {
        skipWs(curr);
        if (!*curr)
            break;
            
        auto cmdStart = curr;
        skipToWs(curr);
        auto cmdEnd = curr;
        
        switch(*cmdStart)
        {
            case 'C':   // CHANNEL
                channel = std::clamp<byte>(parseByte(curr, &curr), 0, 15);
                break;

            case 'R':   // ROOT
                key = parseKey(curr);
                break;

            case 'S':   // SCALE
                parseScale(curr);
                break;

            case 'O':   // OCTAVES
                firstOctave = parseByte(curr, &curr);
                lastOctave = parseByte(curr, &curr);
                break;

            case 'B':   // BPM
                bpm = parseByte(curr, &curr);
                break;

            case 'D':   // DIVISION
                division = parseFloat(curr);
                break;

            case 'M':   // MAP
                if (numMappings < MaxMappings)
                {
                    parseMapping(mappings[numMappings], curr);
                    if (mappings[numMappings].destType == Dest::Note)
                        notesMapping = &mappings[numMappings];

                    ++numMappings;
                }
                else
                {
                    onError();
                    puts("too many mappings");
                }
                break;

            default:
                onError();
                *(char*)cmdEnd = 0;
                printf("unknown command '%s'\n", cmdStart);
                return;
        }
    }

    initScale();
    autoRepeatMs = uint32_t((60.0f * 1000.0 / bpm) * division);
}


byte Config::quantiseNote(uint16_t incoming) const
{
    auto foundIt = std::lower_bound(begin(validNotes), end(validNotes), incoming);
    if (foundIt == end(validNotes) || (foundIt+1) == end(validNotes))
    {
        return validNotes.back();
    }

    int lo = *foundIt;
    int hi = *(foundIt + 1);

    int dLo = incoming - lo;
    int dHi = hi - incoming;
    if (dHi < dLo)
        return hi;
    
    return lo;
}
