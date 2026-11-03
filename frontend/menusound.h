#pragma once
// The game list's sounds: HeartGold and SoulSilver's own menu sounds (the cursor, a choice, a
// window opening and closing, as their start menu and lists play them: pokeheartgold's
// start_menu.c, list_menu_2d.c), taken from the player's ROM when one is in the list, so that nothing of the
// game is in this program. Each is read from the ROM's sound archive (data/sound/
// gs_sound_data.sdat: the sequence, its instrument bank and wave archive) and played through
// once, as the DS's sound driver would, into a stereo buffer at the output's rate; the list then
// plays those buffers. Without a HeartGold or SoulSilver ROM there are no menu sounds.
//
// The list's music: the player's own song (music.ogg or music.wav beside the saves, GAME LIST:
// MUSIC), played over and over under the menu sounds while the list is up, or their songs (the
// .ogg and .wav files in a folder named music there), one after another by name. Nothing is built
// in. An Ogg file's LOOPSTART and LOOPLENGTH (or LOOPEND) tags, in samples, as game music ripped
// to Ogg often has them, are its loop: a song alone goes back to LOOPSTART at the loop's end, and
// in a folder the next song starts from its LOOPSTART there (the first time, the first song from
// its start).
//
// Only what these short sequences use is played: notes and rests, the track's volume, expression,
// pan, transposition and pitch bend, the instrument's envelope (or the track's own), several
// tracks, loops and calls. A command that is not followed (random values, variables) ends its
// track. The driver's behaviour follows its well-known tables: the envelope's rates, the
// decibel curves of velocity and volume, an update every 64 * 2728 cycles of the 33 MHz clock,
// 48 ticks to the quarter note.
#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// (Compiled on its own, frontend/thirdparty/stb_vorbis.c: its macros would clash with ours.)
#define STB_VORBIS_HEADER_ONLY
#include "thirdparty/stb_vorbis.c"

#include <dirent.h>

namespace MenuSound
{
enum Sound
{
    SELECT, // the cursor moved (SEQ_SE_DP_SELECT)
    DECIDE, // a choice made (SEQ_SE_DP_DECIDE)
    WINDOW, // a window opened (SEQ_SE_DP_WIN_OPEN)
    CLOSE,  // a window closed (SEQ_SE_GS_GEARCANCEL)
    COUNT
};

namespace detail
{
// A file read in pieces: a ROM is up to 512 MB, of which a few hundred kilobytes are wanted.
struct File
{
    FILE* f = nullptr;
    ~File() { if (f) fclose(f); }
    bool Read(uint64_t at, void* out, size_t size) const
    {
        return f && fseek(f, (long)at, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
    }
    std::vector<uint8_t> Bytes(uint64_t at, size_t size) const
    {
        std::vector<uint8_t> v(size);
        if (!Read(at, v.data(), size)) v.clear();
        return v;
    }
};

inline uint32_t U32(const std::vector<uint8_t>& b, size_t at) { return at + 4 <= b.size() ? b[at] | b[at + 1] << 8 | b[at + 2] << 16 | (uint32_t)b[at + 3] << 24 : 0; }
inline uint16_t U16(const std::vector<uint8_t>& b, size_t at) { return at + 2 <= b.size() ? (uint16_t)(b[at] | b[at + 1] << 8) : 0; }
inline uint8_t U8(const std::vector<uint8_t>& b, size_t at) { return at < b.size() ? b[at] : 0; }

// The ROM's file of that path (the DS's file name table), as its start and size, or false.
inline bool FindRomFile(const File& rom, const char* path, uint64_t& start, uint32_t& size)
{
    uint32_t hdr[4];
    if (!rom.Read(0x40, hdr, sizeof hdr)) return false;
    const std::vector<uint8_t> fnt = rom.Bytes(hdr[0], hdr[1]);
    if (fnt.empty()) return false;
    uint32_t dir = 0xF000;
    std::string rest = path;
    for (;;)
    {
        const size_t slash = rest.find('/');
        const std::string name = rest.substr(0, slash);
        size_t p = U32(fnt, (dir & 0xFFF) * 8);
        uint32_t id = U16(fnt, (dir & 0xFFF) * 8 + 4);
        int found = -1;
        bool isDir = false;
        while (p < fnt.size())
        {
            const uint8_t l = fnt[p++];
            if (l == 0) break;
            const std::string n((const char*)&fnt[p], std::min<size_t>(l & 0x7F, fnt.size() - p));
            p += l & 0x7F;
            if (l & 0x80)
            {
                const uint16_t sub = U16(fnt, p);
                p += 2;
                if (n == name) { found = sub; isDir = true; break; }
            }
            else
            {
                if (n == name) { found = (int)id; break; }
                id++;
            }
        }
        if (found < 0) return false;
        if (slash == std::string::npos)
        {
            if (isDir) return false;
            uint32_t range[2];
            if (!rom.Read(hdr[2] + (uint64_t)found * 8, range, sizeof range) || range[1] < range[0]) return false;
            start = range[0];
            size = range[1] - range[0];
            return true;
        }
        if (!isDir) return false;
        dir = (uint32_t)found;
        rest = rest.substr(slash + 1);
    }
}

// The sound archive: its tables kept, its files read when wanted.
struct Sdat
{
    const File* rom = nullptr;
    uint64_t base = 0;
    std::vector<uint8_t> symb, info, fat;

    bool Open(const File& r, uint64_t at, uint32_t size)
    {
        rom = &r;
        base = at;
        const std::vector<uint8_t> h = r.Bytes(at, 0x40);
        if (h.size() < 0x40 || memcmp(h.data(), "SDAT", 4) != 0) return false;
        auto block = [&](size_t i) {
            const uint32_t o = U32(h, 0x10 + i * 8), s = U32(h, 0x14 + i * 8);
            return o && s && o + s <= size ? r.Bytes(at + o, s) : std::vector<uint8_t>();
        };
        symb = block(0);
        info = block(1);
        fat = block(2);
        return !info.empty() && !fat.empty();
    }
    std::vector<uint8_t> FileBytes(uint32_t id) const
    {
        const size_t e = 12 + (size_t)id * 16;
        if (e + 8 > fat.size()) return {};
        return rom->Bytes(base + U32(fat, e), U32(fat, e + 4));
    }
    // An INFO record of kind (0 sequences, 2 banks, 3 wave archives), as its offset in info.
    size_t Record(int kind, uint32_t index) const
    {
        const uint32_t list = U32(info, 8 + kind * 4);
        if (index >= U32(info, list)) return 0;
        return U32(info, list + 4 + index * 4);
    }
    // The sequence of that name (SYMB), or -1.
    int SequenceNamed(const char* name) const
    {
        if (symb.empty()) return -1;
        const uint32_t list = U32(symb, 8);
        const uint32_t n = U32(symb, list);
        for (uint32_t i = 0; i < n; i++)
        {
            const uint32_t o = U32(symb, list + 4 + i * 4);
            if (o && o < symb.size() && strncmp((const char*)&symb[o], name, symb.size() - o) == 0) return (int)i;
        }
        return -1;
    }
};

// A wave: its samples (-1..1), rate and loop.
struct Wave
{
    std::vector<float> s;
    double rate = 0;
    bool loop = false;
    size_t loopStart = 0;
};

inline Wave DecodeWave(const std::vector<uint8_t>& swar, uint32_t index)
{
    Wave w;
    if (swar.size() < 0x3C || memcmp(swar.data(), "SWAR", 4) != 0 || index >= U32(swar, 0x38)) return w;
    const size_t at = U32(swar, 0x3C + index * 4);
    const uint8_t type = U8(swar, at);
    w.loop = U8(swar, at + 1) != 0;
    w.rate = U16(swar, at + 2);
    const size_t loopWords = U16(swar, at + 6), lenWords = U32(swar, at + 8);
    const size_t data = at + 12, bytes = std::min((loopWords + lenWords) * 4, swar.size() > data ? swar.size() - data : 0);
    if (type == 0) // PCM8
    {
        for (size_t i = 0; i < bytes; i++) w.s.push_back((int8_t)swar[data + i] / 128.0f);
        w.loopStart = loopWords * 4;
    }
    else if (type == 1) // PCM16
    {
        for (size_t i = 0; i + 1 < bytes; i += 2) w.s.push_back((int16_t)U16(swar, data + i) / 32768.0f);
        w.loopStart = loopWords * 2;
    }
    else if (type == 2 && bytes >= 4) // IMA ADPCM, as the DS decodes it
    {
        static const int16_t steps[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
            50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
            449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
            2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
            13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
        static const int8_t moves[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
        int sample = (int16_t)U16(swar, data), idx = std::min<int>(88, U16(swar, data + 2) & 0x7F);
        for (size_t i = 4; i < bytes; i++)
            for (int half = 0; half < 2; half++)
            {
                const int n = half ? swar[data + i] >> 4 : swar[data + i] & 15;
                const int step = steps[idx];
                int diff = step / 8;
                if (n & 1) diff += step / 4;
                if (n & 2) diff += step / 2;
                if (n & 4) diff += step;
                sample = n & 8 ? std::max(-0x7FFF, sample - diff) : std::min(0x7FFF, sample + diff);
                idx = std::min(88, std::max(0, idx + moves[n & 7]));
                w.s.push_back(sample / 32768.0f);
            }
        w.loopStart = loopWords ? (loopWords * 4 - 4) * 2 : 0;
    }
    if (w.loopStart >= w.s.size()) w.loop = false;
    return w;
}

// An instrument as a note finds it: what plays (PCM wave, square wave of a duty, noise), its
// note at the wave's own pitch, envelope and pan.
struct Instrument
{
    int type = 0; // 1 PCM, 2 square, 3 noise
    uint16_t wave = 0, archive = 0;
    uint8_t note = 60, attack = 127, decay = 127, sustain = 127, release = 127, pan = 64;
};

inline bool InstrumentFor(const std::vector<uint8_t>& sbnk, uint32_t program, int note, Instrument& out)
{
    if (sbnk.size() < 0x3C || memcmp(sbnk.data(), "SBNK", 4) != 0 || program >= U32(sbnk, 0x38)) return false;
    const size_t rec = 0x3C + program * 4;
    int type = U8(sbnk, rec);
    size_t at = U16(sbnk, rec + 1);
    if (type == 16) // drum set: one instrument for each note from low to high
    {
        const int low = U8(sbnk, at), high = U8(sbnk, at + 1);
        if (note < low || note > high) return false;
        at += 2 + (size_t)(note - low) * 12;
        type = U16(sbnk, at);
        at += 2;
    }
    else if (type == 17) // key split: up to eight ranges of notes
    {
        int region = 0;
        while (region < 8 && U8(sbnk, at + region) && note > U8(sbnk, at + region)) region++;
        if (region == 8 || !U8(sbnk, at + region)) return false;
        at += 8 + (size_t)region * 12;
        type = U16(sbnk, at);
        at += 2;
    }
    if (type < 1 || type > 3) return false;
    out.type = type;
    out.wave = U16(sbnk, at);
    out.archive = U16(sbnk, at + 2);
    out.note = U8(sbnk, at + 4);
    out.attack = U8(sbnk, at + 5);
    out.decay = U8(sbnk, at + 6);
    out.sustain = U8(sbnk, at + 7);
    out.release = U8(sbnk, at + 8);
    out.pan = U8(sbnk, at + 9);
    return true;
}

// The driver's decibel curve of a volume or velocity (0-127) squared, in tenths of a decibel.
inline int SquareDb(int v)
{
    if (v <= 0) return -723;
    return std::max(-723, (int)std::lround(400.0 * std::log10(std::min(v, 127) / 127.0)));
}
// The same on the plain curve (a volume): 20 log10(v / 127) decibels.
inline int Db(int v)
{
    if (v <= 0) return -723;
    return std::max(-723, (int)std::lround(200.0 * std::log10(std::min(v, 127) / 127.0)));
}
inline int AttackRate(int a)
{
    static const uint8_t high[19] = { 0, 1, 5, 14, 26, 38, 51, 63, 73, 84, 92, 100, 109, 116, 123, 127, 132, 137, 143 };
    return a >= 0x6D ? high[0x7F - std::min(a, 0x7F)] : 0xFF - a;
}
inline int FallingRate(int v)
{
    if (v >= 0x7F) return 0xFFFF;
    if (v == 0x7E) return 0x3C00;
    if (v < 0x32) return v * 2 + 1;
    return 0x1E00 / (0x7E - v);
}

// The sequence played through into stereo samples at `rate`.
class Renderer
{
public:
    Renderer(const Sdat& sdat, int rate) : sdat_(sdat), rate_(rate) {}

    std::vector<int16_t> Play(int seq)
    {
        std::vector<int16_t> out;
        std::vector<float> mixed;
        const size_t rec = sdat_.Record(0, (uint32_t)seq);
        if (!rec) return out;
        const uint16_t fileId = U16(sdat_.info, rec), bankId = U16(sdat_.info, rec + 4);
        seqVolume_ = U8(sdat_.info, rec + 6);
        const std::vector<uint8_t> sseq = sdat_.FileBytes(fileId);
        if (sseq.size() < 0x1C || memcmp(sseq.data(), "SSEQ", 4) != 0) return out;
        data_.assign(sseq.begin() + std::min<size_t>(U32(sseq, 0x18), sseq.size()), sseq.end());
        const size_t bank = sdat_.Record(2, bankId);
        if (!bank) return out;
        sbnk_ = sdat_.FileBytes(U16(sdat_.info, bank));
        for (int i = 0; i < 4; i++)
        {
            const uint16_t war = U16(sdat_.info, bank + 4 + i * 2);
            const size_t wrec = war == 0xFFFF ? 0 : sdat_.Record(3, war);
            archives_[i] = wrec ? sdat_.FileBytes(U16(sdat_.info, wrec)) : std::vector<uint8_t>();
        }

        tracks_.clear();
        tracks_.push_back(Track());
        // One update of the driver: 64 * 2728 cycles of the 33,513,982 Hz clock.
        const double update = 64.0 * 2728.0 / 33513982.0;
        double sampleDebt = 0;
        int tempoCounter = 0;
        // (At most four seconds: a sound that loops for ever is cut there.)
        for (int u = 0; u < (int)(4.0 / update); u++)
        {
            tempoCounter += tempo_;
            while (tempoCounter >= 240)
            {
                tempoCounter -= 240;
                Tick();
            }
            bool sounding = false;
            for (Channel& c : channels_)
            {
                UpdateEnvelope(c);
                sounding |= c.active;
            }
            bool running = false;
            for (const Track& t : tracks_) running |= !t.ended;
            sampleDebt += update * rate_;
            const int n = (int)sampleDebt;
            sampleDebt -= n;
            Mix(mixed, n);
            if (!running && !sounding) break;
        }
        // (The last few milliseconds, quiet, trimmed; a sound of several tracks that reaches the
        // top of the range, the gear's cancel, brought down to just under it as a whole rather
        // than clipped.)
        float peak = 0;
        for (float v : mixed) peak = std::max(peak, std::fabs(v * kGain));
        const float scale = kGain * (peak > 0.95f ? 0.95f / peak : 1.0f);
        out.resize(mixed.size());
        for (size_t i = 0; i < mixed.size(); i++) out[i] = (int16_t)std::lround(mixed[i] * scale * 32767.0f);
        while (out.size() >= 2 && out[out.size() - 1] == 0 && out[out.size() - 2] == 0) out.resize(out.size() - 2);
        return out;
    }

private:
    struct Track
    {
        size_t pos = 0;
        int wait = 0;
        bool ended = false;
        int program = 0, volume = 127, expression = 127, pan = 64, transpose = 0, bend = 0, bendRange = 2;
        int attack = -1, decay = -1, sustain = -1, release = -1;
        bool noteWait = true;
        std::vector<size_t> calls;
        size_t loopStart = 0;
        int loopCount = -1;
    };
    struct Channel
    {
        bool active = false;
        int track = 0;
        int type = 0, duty = 0;
        const Wave* wave = nullptr;
        double pos = 0, baseRate = 0;
        int note = 60, baseNote = 60, velocity = 127, length = 0;
        int attack = 0, decay = 0, sustainLevel = 0, release = 0;
        int state = 0; // 0 attack, 1 decay, 2 sustain, 3 release
        int envelope = -92544;
        int pan = 64;
        uint16_t lfsr = 0x7FFF;
    };

    const Sdat& sdat_;
    int rate_;
    int seqVolume_ = 127, tempo_ = 120;
    std::vector<uint8_t> data_, sbnk_;
    std::vector<uint8_t> archives_[4];
    std::vector<Track> tracks_;
    std::vector<Channel> channels_;
    // (archive * 65536 + wave; a deque, as the notes playing keep pointers to their waves)
    std::deque<std::pair<uint32_t, Wave>> waves_;

    int Var(Track& t)
    {
        int v = 0;
        for (int i = 0; i < 4 && t.pos < data_.size(); i++)
        {
            const uint8_t b = data_[t.pos++];
            v = v << 7 | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
    int Byte(Track& t) { return t.pos < data_.size() ? data_[t.pos++] : 0; }
    int Word(Track& t) { const int lo = Byte(t); return lo | Byte(t) << 8; }
    int Triple(Track& t) { const int a = Word(t); return a | Byte(t) << 16; }

    const Wave* WaveOf(int archive, int index)
    {
        const uint32_t key = (uint32_t)archive << 16 | (uint32_t)index;
        for (auto& w : waves_)
            if (w.first == key) return w.second.s.empty() ? nullptr : &w.second;
        if (archive < 0 || archive > 3) return nullptr;
        waves_.push_back({ key, DecodeWave(archives_[archive], (uint32_t)index) });
        return waves_.back().second.s.empty() ? nullptr : &waves_.back().second;
    }

    void NoteOn(int ti, int note, int velocity, int length)
    {
        Track& t = tracks_[ti];
        note = std::min(127, std::max(0, note + t.transpose));
        Instrument ins;
        if (!InstrumentFor(sbnk_, (uint32_t)t.program, note, ins)) return;
        Channel c;
        c.active = true;
        c.track = ti;
        c.type = ins.type;
        if (ins.type == 1)
        {
            c.wave = WaveOf(ins.archive, ins.wave);
            if (!c.wave) return;
            c.baseRate = c.wave->rate;
        }
        else
        {
            // (Square waves and noise: their note runs the channel's timer at 16,756,991 / 8006 Hz,
            // eight steps to a square wave's cycle.)
            c.duty = ins.wave & 7;
            c.baseRate = 16756991.0 / 8006.0;
        }
        c.note = note;
        c.baseNote = ins.note;
        c.velocity = velocity;
        c.length = length;
        c.attack = AttackRate(t.attack >= 0 ? t.attack : ins.attack);
        c.decay = FallingRate(t.decay >= 0 ? t.decay : ins.decay);
        c.sustainLevel = SquareDb(t.sustain >= 0 ? t.sustain : ins.sustain) << 7;
        c.release = FallingRate(t.release >= 0 ? t.release : ins.release);
        c.pan = ins.pan;
        c.envelope = -92544;
        for (Channel& old : channels_)
            if (!old.active)
            {
                old = c;
                return;
            }
        channels_.push_back(c);
    }

    void Tick()
    {
        // Notes count down their length, then fall away (release).
        for (Channel& c : channels_)
            if (c.active && c.state != 3 && c.length > 0 && --c.length == 0) c.state = 3;
        for (size_t ti = 0; ti < tracks_.size(); ti++)
        {
            if (tracks_[ti].ended) continue;
            if (tracks_[ti].wait > 0 && --tracks_[ti].wait > 0) continue;
            int guard = 0;
            while (!tracks_[ti].ended && tracks_[ti].wait == 0 && guard++ < 1000) Step((int)ti);
        }
    }

    void Step(int ti)
    {
        Track& t = tracks_[ti];
        if (t.pos >= data_.size())
        {
            t.ended = true;
            return;
        }
        const int cmd = Byte(t);
        if (cmd < 0x80)
        {
            const int velocity = Byte(t), length = Var(t);
            NoteOn(ti, cmd, velocity, length);
            if (tracks_[ti].noteWait) tracks_[ti].wait = length;
            return;
        }
        switch (cmd)
        {
        case 0x80: t.wait = Var(t); break;
        case 0x81: t.program = Var(t); break;
        case 0x93:
        {
            const int n = Byte(t);
            const int at = Triple(t);
            (void)n;
            Track nt;
            nt.pos = (size_t)at;
            tracks_.push_back(nt); // (t may now be moved: not used again here)
            break;
        }
        case 0x94: t.pos = (size_t)Triple(t); break;
        case 0x95:
        {
            const int at = Triple(t);
            t.calls.push_back(t.pos);
            t.pos = (size_t)at;
            break;
        }
        case 0xC0: t.pan = Byte(t); break;
        case 0xC1: t.volume = Byte(t); break;
        case 0xC3: t.transpose = (int8_t)Byte(t); break;
        case 0xC4: t.bend = (int8_t)Byte(t); break;
        case 0xC5: t.bendRange = Byte(t); break;
        case 0xC7: t.noteWait = Byte(t) != 0; break;
        case 0xD0: t.attack = Byte(t); break;
        case 0xD1: t.decay = Byte(t); break;
        case 0xD2: t.sustain = Byte(t); break;
        case 0xD3: t.release = Byte(t); break;
        case 0xD4: t.loopCount = Byte(t); t.loopStart = t.pos; break;
        case 0xD5: t.expression = Byte(t); break;
        case 0xE1: tempo_ = Word(t); break;
        case 0xFC:
            if (t.loopCount < 0) break;
            if (t.loopCount == 0 || --t.loopCount > 0) t.pos = t.loopStart;
            else t.loopCount = -1;
            break;
        case 0xFD:
            if (t.calls.empty()) t.ended = true;
            else { t.pos = t.calls.back(); t.calls.pop_back(); }
            break;
        case 0xFE: Word(t); break; // (the tracks to allocate)
        case 0xFF: t.ended = true; break;
        default:
            if ((cmd >= 0xC2 && cmd <= 0xD7) || cmd == 0xC6) Byte(t); // (priority, tie, modulation, portamento...)
            else if (cmd == 0xE0 || cmd == 0xE3) Word(t); // (modulation delay, pitch sweep)
            else if (cmd >= 0xB0 && cmd <= 0xBD) { Byte(t); Word(t); } // (variables)
            else t.ended = true; // (random values, variables as arguments: not followed)
            break;
        }
    }

    void UpdateEnvelope(Channel& c)
    {
        if (!c.active) return;
        switch (c.state)
        {
        case 0:
            c.envelope = -(int)(((int64_t)-c.envelope * c.attack) >> 8);
            if (c.envelope == 0) c.state = 1;
            break;
        case 1:
            c.envelope -= c.decay;
            if (c.envelope <= c.sustainLevel)
            {
                c.envelope = c.sustainLevel;
                c.state = 2;
            }
            break;
        case 2: break;
        case 3:
            c.envelope -= c.release;
            if (c.envelope <= -92544) c.active = false;
            break;
        }
    }

    // The output's level: HeartGold's window sound as loud as the game makes it in this program
    // (emulated, at full volume), measured.
    static constexpr float kGain = 1.65f;

    void Mix(std::vector<float>& out, int n)
    {
        const size_t first = out.size();
        out.resize(first + (size_t)n * 2, 0.0f);
        float* mix = &out[first];
        for (Channel& c : channels_)
        {
            if (!c.active) continue;
            const Track& t = tracks_[(size_t)c.track];
            // (The velocity on the squared curve, the volumes on the plain one: compared with
            // HeartGold's own sounds recorded from the emulator, the squared curve for the volumes
            // made the window's sound 11 dB too quiet and its changes of volume far too deep.)
            const int db = std::max(-723, (c.envelope >> 7) + SquareDb(c.velocity) + Db(t.volume) + Db(t.expression) +
                Db(seqVolume_));
            const float amp = (float)std::pow(10.0, db / 200.0);
            const int pan = std::min(127, std::max(0, t.pan + c.pan - 64));
            const float left = amp * (127 - pan) / 127.0f, right = amp * pan / 127.0f;
            const double semis = c.note - c.baseNote + t.bend * t.bendRange / 128.0;
            const double step = c.baseRate * std::pow(2.0, semis / 12.0) / rate_;
            for (int i = 0; i < n && c.active; i++)
            {
                float v = 0;
                if (c.type == 1)
                {
                    size_t p = (size_t)c.pos;
                    if (p >= c.wave->s.size())
                    {
                        if (!c.wave->loop)
                        {
                            c.active = false;
                            break;
                        }
                        const double len = (double)(c.wave->s.size() - c.wave->loopStart);
                        c.pos = c.wave->loopStart + std::fmod(c.pos - c.wave->loopStart, len);
                        p = (size_t)c.pos;
                    }
                    v = c.wave->s[p];
                }
                else if (c.type == 2)
                    v = ((int)std::fmod(c.pos, 8.0) <= c.duty) ? 0.5f : -0.5f;
                else
                {
                    // (Noise: the DS's 15-bit shift register, stepped by the timer.)
                    for (int64_t k = (int64_t)(c.pos + step) - (int64_t)c.pos; k > 0; k--)
                    {
                        if (c.lfsr & 1) c.lfsr = (uint16_t)((c.lfsr >> 1) ^ 0x6000);
                        else c.lfsr >>= 1;
                    }
                    v = (c.lfsr & 1) ? -0.5f : 0.5f;
                }
                c.pos += step;
                mix[(size_t)i * 2] += v * left;
                mix[(size_t)i * 2 + 1] += v * right;
            }
        }
    }
};

// One of the player's songs, read from its file: decoded a little at a time (Ogg Vorbis) or all
// at once (WAV).
struct Song
{
    std::vector<unsigned char> file; // (the Ogg file, which stb_vorbis reads from memory)
    stb_vorbis* ogg = nullptr;
    std::vector<int16_t> wav;
    // (In samples of one channel at the file's rate: where the song is, and its loop. No loop
    // end: the whole song.)
    uint64_t at = 0, loopStart = 0, loopEnd = 0;
    int channels = 0, rate = 0;

    Song() = default;
    Song(const Song&) = delete;
    Song& operator=(const Song&) = delete;
    ~Song()
    {
        if (ogg) stb_vorbis_close(ogg);
    }
    bool Open(const std::string& path)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        // (At most 64 MB: a WAV of six minutes.)
        if (size > 0 && size <= (64 << 20))
        {
            file.resize((size_t)size);
            if (fread(file.data(), 1, file.size(), f) != file.size()) file.clear();
        }
        fclose(f);
        if (file.empty()) return false;
        if (file.size() > 4 && !memcmp(file.data(), "OggS", 4))
        {
            int error = 0;
            ogg = stb_vorbis_open_memory(file.data(), (int)file.size(), &error, nullptr);
            if (!ogg)
            {
                fprintf(stderr, "[music] %s: not an Ogg Vorbis file this can read (%d)\n", path.c_str(), error);
                return false;
            }
            const stb_vorbis_info info = stb_vorbis_get_info(ogg);
            channels = info.channels;
            rate = (int)info.sample_rate;
            const stb_vorbis_comment tags = stb_vorbis_get_comment(ogg);
            uint64_t length = 0;
            for (int i = 0; i < tags.comment_list_length; i++)
            {
                const char* t = tags.comment_list[i];
                if (!SDL_strncasecmp(t, "LOOPSTART=", 10)) loopStart = strtoull(t + 10, nullptr, 10);
                else if (!SDL_strncasecmp(t, "LOOPLENGTH=", 11)) length = strtoull(t + 11, nullptr, 10);
                else if (!SDL_strncasecmp(t, "LOOPEND=", 8)) loopEnd = strtoull(t + 8, nullptr, 10);
            }
            if (length) loopEnd = loopStart + length;
            const uint64_t total = stb_vorbis_stream_length_in_samples(ogg);
            if (loopEnd <= loopStart || (total && loopEnd > total)) loopStart = loopEnd = 0;
        }
        else
        {
            SDL_AudioSpec spec = {};
            Uint8* data = nullptr;
            Uint32 bytes = 0;
            if (!SDL_LoadWAV_RW(SDL_RWFromConstMem(file.data(), (int)file.size()), 1, &spec, &data, &bytes))
            {
                fprintf(stderr, "[music] %s: %s\n", path.c_str(), SDL_GetError());
                return false;
            }
            // (Any WAV SDL reads, as 16-bit samples with its own channels and rate.)
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, spec.channels, spec.freq) < 0)
            {
                SDL_FreeWAV(data);
                return false;
            }
            std::vector<Uint8> buf((size_t)bytes * std::max(1, cvt.len_mult));
            memcpy(buf.data(), data, bytes);
            SDL_FreeWAV(data);
            cvt.buf = buf.data();
            cvt.len = (int)bytes;
            if (cvt.needed) SDL_ConvertAudio(&cvt);
            wav.resize((size_t)(cvt.needed ? cvt.len_cvt : cvt.len) / 2);
            memcpy(wav.data(), buf.data(), wav.size() * 2);
            file.clear();
            channels = spec.channels;
            rate = spec.freq;
        }
        if (channels < 1 || channels > 2 || rate <= 0 || (!ogg && wav.size() < (size_t)channels))
        {
            fprintf(stderr, "[music] %s: %d channels at %d Hz: only mono or stereo\n", path.c_str(), channels, rate);
            return false;
        }
        fprintf(stderr, "[music] %s: %d channels at %d Hz", path.c_str(), channels, rate);
        if (loopEnd) fprintf(stderr, ", looping from %.3f s to %.3f s", loopStart / (double)rate, loopEnd / (double)rate);
        fprintf(stderr, "\n");
        return true;
    }
    void Seek(uint64_t to)
    {
        if (ogg) stb_vorbis_seek(ogg, (unsigned)to);
        at = to;
    }
    // Up to `frames` samples (of all its channels) into `out`; none at the loop's end, or the
    // song's.
    int Decode(int16_t* out, int frames)
    {
        if (loopEnd) frames = (int)std::min<uint64_t>((uint64_t)frames, loopEnd > at ? loopEnd - at : 0);
        if (frames <= 0) return 0;
        int got = 0;
        if (ogg)
            got = stb_vorbis_get_samples_short_interleaved(ogg, channels, out, frames * channels);
        else
        {
            const uint64_t left = wav.size() / channels > at ? wav.size() / channels - at : 0;
            got = (int)std::min<uint64_t>((uint64_t)frames, left);
            memcpy(out, wav.data() + at * channels, (size_t)got * channels * 2);
        }
        at += (uint64_t)got;
        return got;
    }
};

// The player's songs, played one after another and round again: the first from its start, then
// at each one's loop end (or its end, without one) the next from its loop's start (or its start),
// with no pause between them. One song goes round its own loop. All changed to the output's rate
// and two channels.
struct Music
{
    std::vector<std::unique_ptr<Song>> songs;
    size_t current = 0;
    SDL_AudioStream* stream = nullptr;
    bool draining = false; // (the last of a song in another format still in the stream)
    std::string from;

    ~Music() { Close(); }
    void Close()
    {
        if (stream) SDL_FreeAudioStream(stream);
        stream = nullptr;
        songs.clear();
        current = 0;
        draining = false;
        from.clear();
    }
    bool Open(const std::vector<std::string>& paths, int rate)
    {
        Close();
        for (const std::string& path : paths)
        {
            std::unique_ptr<Song> song(new Song);
            if (song->Open(path)) songs.push_back(std::move(song));
        }
        if (songs.empty()) return false;
        rate_ = rate;
        stream = NewStream();
        if (!stream)
        {
            Close();
            return false;
        }
        for (const std::string& path : paths) from += path + "|";
        return true;
    }
    // The next `n` samples (stereo, at the output's rate).
    void Read(int16_t* out, int n)
    {
        int16_t piece[2048];
        int have = 0;
        // (A bound on the turns: songs that give nothing, say a loop of no length, end in silence.)
        for (int turns = 0; have < n && stream && turns < 4096; turns++)
        {
            const int available = SDL_AudioStreamAvailable(stream) / 2;
            if (available > 0)
            {
                const int got = std::max(0, SDL_AudioStreamGet(stream, out + have, std::min(available, n - have) * 2)) / 2;
                have += got;
                if (got > 0) turns = 0;
                continue;
            }
            if (draining)
            {
                SDL_FreeAudioStream(stream);
                stream = NewStream();
                draining = false;
                continue;
            }
            Song& song = *songs[current];
            const int got = song.Decode(piece, 1024);
            if (got > 0)
            {
                if (SDL_AudioStreamPut(stream, piece, got * song.channels * 2) != 0) break;
                continue;
            }
            // (The loop's end: the next song, from its loop's start. In another format, it waits
            // for this one's last samples to come out of the stream.)
            const size_t next = (current + 1) % songs.size();
            Song& following = *songs[next];
            following.Seek(following.loopStart);
            if (following.channels != song.channels || following.rate != song.rate)
            {
                SDL_AudioStreamFlush(stream);
                draining = true;
            }
            current = next;
        }
        if (have < n) memset(out + have, 0, (size_t)(n - have) * 2);
    }

private:
    int rate_ = 48000;
    SDL_AudioStream* NewStream()
    {
        const Song& song = *songs[current];
        return SDL_NewAudioStream(AUDIO_S16SYS, (Uint8)song.channels, song.rate, AUDIO_S16SYS, 2, rate_);
    }
};

// The sounds, the music and the device that plays them (mixed in SDL's audio callback, Fill;
// what it reads is changed with the device locked).
struct State
{
    std::vector<int16_t> sounds[COUNT];
    std::string from; // (the ROM they were read from)
    SDL_AudioDeviceID device = 0;
    int rate = 48000;
    bool on = true;
    bool musicOn = false;
    int volume = 100;
    int fade = 256; // (the music's level as the list closes, out of 256)
    std::vector<int16_t> playing;
    size_t playingAt = 0;
    Music music;
    std::vector<int16_t> musicPiece;
};
inline State& Get()
{
    static State s;
    return s;
}

// The music at a little under half the menu sounds' level: a song is made as loud as it can be,
// the DS's sounds are not.
constexpr int MusicLevel = 45;

inline void SDLCALL Fill(void*, Uint8* bytes, int len)
{
    State& s = Get();
    int16_t* out = (int16_t*)bytes;
    const int n = len / 2;
    memset(out, 0, (size_t)len);
    if (!s.music.songs.empty() && s.musicOn && s.volume > 0)
    {
        s.musicPiece.resize((size_t)n);
        s.music.Read(s.musicPiece.data(), n);
        const int64_t gain = (int64_t)s.volume * MusicLevel * s.fade;
        for (int i = 0; i < n; i++) out[i] = (int16_t)(s.musicPiece[i] * gain / (10000 * 256));
    }
    if (s.on && s.playingAt < s.playing.size())
    {
        const int k = (int)std::min<size_t>((size_t)n, s.playing.size() - s.playingAt);
        for (int i = 0; i < k; i++)
            out[i] = (int16_t)std::clamp(out[i] + s.playing[s.playingAt + i] * s.volume / 100, -32768, 32767);
        s.playingAt += (size_t)k;
    }
}
} // namespace detail

// Reads the menu sounds from a HeartGold or SoulSilver ROM; false (and silence) if it has none.
inline bool LoadFrom(const std::string& rom, int rate)
{
    detail::File f;
    f.f = fopen(rom.c_str(), "rb");
    uint64_t at = 0;
    uint32_t size = 0;
    detail::Sdat sdat;
    if (!f.f || !detail::FindRomFile(f, "data/sound/gs_sound_data.sdat", at, size) || !sdat.Open(f, at, size))
    {
        fprintf(stderr, "[sound] no sound archive in %s\n", rom.c_str());
        return false;
    }
    static const char* names[COUNT] = { "SEQ_SE_DP_SELECT", "SEQ_SE_DP_DECIDE", "SEQ_SE_DP_WIN_OPEN", "SEQ_SE_GS_GEARCANCEL" };
    static const int numbers[COUNT] = { 1500, 1501, 1532, 2368 }; // (pokeheartgold's include/constants/sndseq.h)
    detail::State& s = detail::Get();
    bool any = false;
    for (int i = 0; i < COUNT; i++)
    {
        int seq = sdat.SequenceNamed(names[i]);
        if (seq < 0) seq = numbers[i];
        detail::Renderer r(sdat, rate);
        s.sounds[i] = r.Play(seq);
        any |= !s.sounds[i].empty();
        fprintf(stderr, "[sound] %s: sequence %d, %.0f ms\n", names[i], seq, s.sounds[i].size() / 2 * 1000.0 / rate);
    }
    if (any) s.from = rom;
    return any;
}

// The list's sounds from the first HeartGold or SoulSilver ROM among `roms` (game code, path),
// and its music (the songs in `folder`'s music folder, or music.ogg or music.wav in it), played on SDL's audio (started here, as the
// list may be shown before the game's own SDL), at `volume` (0 to 100, the settings' VOLUME):
// the sounds if `on` (GAME LIST: MENU SOUNDS), the music if `musicOn` (GAME LIST: MUSIC). Called
// again when the settings change.
inline void Start(const std::vector<std::pair<std::string, std::string>>& roms, bool on, int volume,
                  const std::string& folder = std::string(), bool musicOn = false)
{
    detail::State& s = detail::Get();
    std::string rom;
    for (const auto& r : roms)
        if (r.first.size() == 4 && (r.first.compare(0, 3, "IPK") == 0 || r.first.compare(0, 3, "IPG") == 0))
        {
            rom = r.second;
            break;
        }
    std::vector<std::string> songs;
    if (musicOn && !folder.empty())
    {
        if (DIR* d = opendir((folder + "/music").c_str()))
        {
            while (dirent* e = readdir(d))
            {
                std::string n = e->d_name;
                std::string lower = n;
                for (char& c : lower) c = (char)tolower((unsigned char)c);
                if (n[0] != '.' && lower.size() > 4 && (lower.compare(lower.size() - 4, 4, ".ogg") == 0 || lower.compare(lower.size() - 4, 4, ".wav") == 0))
                    songs.push_back(folder + "/music/" + n);
            }
            closedir(d);
            std::sort(songs.begin(), songs.end());
        }
        if (songs.empty())
            for (const char* name : { "/music.ogg", "/music.wav" })
                if (FILE* f = fopen((folder + name).c_str(), "rb"))
                {
                    fclose(f);
                    songs.push_back(folder + name);
                    break;
                }
    }
    std::string song; // (the songs as Music::from has them)
    for (const std::string& path : songs) song += path + "|";
    if (s.device)
    {
        // (Settings changed while the list is up. A ROM's sounds or a song not read when the list
        // opened, because they were off then, are read now.)
        SDL_LockAudioDevice(s.device);
        s.on = on;
        s.musicOn = musicOn;
        s.volume = std::max(0, std::min(100, volume));
        if (on && !rom.empty() && s.from != rom) LoadFrom(rom, s.rate);
        if (!song.empty() && s.music.from != song) s.music.Open(songs, s.rate);
        SDL_UnlockAudioDevice(s.device);
        return;
    }
    s.on = on;
    s.musicOn = musicOn;
    s.volume = std::max(0, std::min(100, volume));
    s.fade = 256;
    if ((!on || rom.empty()) && song.empty()) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "[sound] no audio: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want = {}, have = {};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = detail::Fill;
    s.device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s.device)
    {
        fprintf(stderr, "[sound] no audio device: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    s.rate = want.freq;
    // (Read once: the list is shown again after a GBA game.)
    bool any = false;
    if (on && !rom.empty()) any = s.from == rom || LoadFrom(rom, want.freq);
    if (!song.empty()) any |= s.music.Open(songs, want.freq);
    if (!any)
    {
        SDL_CloseAudioDevice(s.device);
        s.device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    s.playing.clear();
    s.playingAt = 0;
    SDL_PauseAudioDevice(s.device, 0);
}

// A sound, cutting off the one before (as the game's own menu sounds do).
inline void Play(Sound which)
{
    detail::State& s = detail::Get();
    if (!s.device || !s.on || s.volume == 0 || s.sounds[which].empty()) return;
    SDL_LockAudioDevice(s.device);
    s.playing = s.sounds[which];
    s.playingAt = 0;
    SDL_UnlockAudioDevice(s.device);
}

// The device closed (before the game opens its own sound), once the sound playing has finished
// (at most 0.8 seconds: the choice of a game is heard as it starts) and the music has faded out
// (0.4 seconds).
inline void Stop()
{
    detail::State& s = detail::Get();
    if (!s.device) return;
    for (int i = 0; i < 80; i++)
    {
        SDL_LockAudioDevice(s.device);
        s.fade = std::max(0, 256 - (i + 1) * 256 / 40);
        const bool sounding = s.on && s.playingAt < s.playing.size();
        const bool music = !s.music.songs.empty() && s.musicOn && s.fade > 0;
        SDL_UnlockAudioDevice(s.device);
        if (!sounding && !music) break;
        SDL_Delay(10);
    }
    SDL_CloseAudioDevice(s.device);
    s.device = 0;
    s.playing.clear();
    s.playingAt = 0;
    s.music.Close();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
} // namespace MenuSound
