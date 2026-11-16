// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. Copied as it is from NightShift's src/ai for Nightshift Drums.

#include "OfflineDrummer.h"

namespace nightshift::drummer::offline
{

using agents::Request;

namespace
{
    //==============================================================================
    // A bar being written: a lane of hit characters per drum ('.' rest, '1'-'9' how hard,
    // 'g' ghost, 'f' flam, 'r' drag), the steps a bar can be resampled finer for rolls
    struct Grid
    {
        int steps = 16;
        std::map<juce::String, std::string> lanes;

        explicit Grid (int s) : steps (s) {}

        void hit (const juce::String& voice, int step, char c)
        {
            if (step < 0 || step >= steps)
                return;

            auto& lane = lanes[voice];

            if ((int) lane.size() != steps)
                lane.assign ((size_t) steps, '.');

            lane[(size_t) step] = c;
        }

        char at (const juce::String& voice, int step) const
        {
            auto it = lanes.find (voice);
            return it == lanes.end() || step < 0 || step >= steps ? '.' : it->second[(size_t) step];
        }

        bool on (const juce::String& voice, int step) const     { return at (voice, step) != '.'; }

        void clear (const juce::String& voice, int from, int to)
        {
            if (auto it = lanes.find (voice); it != lanes.end())
                for (int s = std::max (0, from); s < std::min (steps, to); ++s)
                    it->second[(size_t) s] = '.';
        }

        void clearAll (int from, int to)
        {
            for (auto& [v, lane] : lanes)
                clear (v, from, to);
        }

        int count (const juce::String& voice) const
        {
            auto it = lanes.find (voice);
            return it == lanes.end() ? 0 : (int) std::count_if (it->second.begin(), it->second.end(), [] (char c) { return c != '.'; });
        }

        // Finer steps (a multiple), the hits where they were
        void resample (int newSteps)
        {
            if (newSteps == steps || newSteps % steps != 0)
                return;

            const int factor = newSteps / steps;

            for (auto& [v, lane] : lanes)
            {
                std::string finer ((size_t) newSteps, '.');

                for (int s = 0; s < steps; ++s)
                    finer[(size_t) (s * factor)] = lane[(size_t) s];

                lane = finer;
            }

            steps = newSteps;
        }

        Bar toBar() const
        {
            Bar bar;
            bar.steps = steps;

            for (auto& [voice, lane] : lanes)
                if (std::any_of (lane.begin(), lane.end(), [] (char c) { return c != '.'; }))
                    bar.lanes.push_back ({ voice, juce::String (lane) });

            return bar;
        }
    };

    char vel (int n)                    { return (char) ('0' + juce::jlimit (1, 9, n)); }

    //==============================================================================
    // The time signature as a drummer counts it: 16ths a bar, the groups the bar falls into
    // (4/4 in four beats; 7/8 in 2+2+3 eighths; 6/8 in two dotted quarters), and where the snare
    // goes with a straight feel (the backbeats) and in half-time
    struct Frame
    {
        int beats = 4, unit = 4;
        int steps = 16;
        std::vector<int> groupStarts;       // in 16ths
        std::vector<int> backbeats;
        int halfBackbeat = 8;
        int perBeat = 4;                    // 16ths a counted beat (a quarter in x/4, a group's average otherwise)
        bool fourFour() const               { return beats == 4 && unit == 4; }
        bool compound = false;
    };

    Frame frameFor (int beats, int unit)
    {
        Frame f;
        f.beats = beats;
        f.unit = unit;
        f.steps = juce::jmax (2, beats * 16 / unit);
        std::vector<int> groups;

        if (unit == 4 || unit == 2)
        {
            const int each = 16 / unit;

            for (int b = 0; b < beats; ++b)
                groups.push_back (each);
        }
        else if (unit == 8 && beats % 3 == 0)
        {
            f.compound = true;

            for (int b = 0; b < beats / 3; ++b)
                groups.push_back (6);
        }
        else
        {
            // Odd eighths (or sixteenths): twos with a three at the end (7/8 = 2+2+3)
            const int each = 16 / unit;
            int left = beats;

            while (left > 0)
            {
                const int g = left == 3 || left == 5 ? 3 : 2;
                groups.push_back (g * each);
                left -= g;
            }
        }

        int at = 0;

        for (auto g : groups)
        {
            f.groupStarts.push_back (at);
            at += g;
        }

        // The backbeat on every other group from the second (2 and 4 in 4/4; the 4 in 6/8)
        for (size_t i = 1; i < f.groupStarts.size(); i += 2)
            f.backbeats.push_back (f.groupStarts[i]);

        if (f.backbeats.empty())
            f.backbeats.push_back (f.steps / 2);

        f.halfBackbeat = f.groupStarts.size() >= 2 ? f.groupStarts[f.groupStarts.size() / 2] : f.steps / 2;
        f.perBeat = juce::jmax (1, f.steps / (int) f.groupStarts.size());
        return f;
    }

    bool isGroupStart (const Frame& f, int step)
    {
        return std::find (f.groupStarts.begin(), f.groupStarts.end(), step) != f.groupStarts.end();
    }

    //==============================================================================
    // What every generator works from
    struct Context
    {
        const Request& req;
        const agents::Style& style;
        const Frame& frame;
        juce::Random& rng;
        juce::String kind;              // the section: intro, verse, chorus, breakdown...
        bool halfTime = false;
        float energy = 0.5f, busy = 0.5f;
        double tempo = 120.0;
        int phase = 0;                  // where a polymetric cycle is, carried from bar to bar
        int cycle = 0;                  // which cycle (djent), chosen once a section
        juce::String note;              // what the chosen groove is, for the reply

        bool chance (float p)           { return rng.nextFloat() < p; }
        int pick (int n)                { return rng.nextInt (juce::jmax (1, n)); }
        juce::String id() const         { return style.id; }
    };

    // The snare's places in this feel
    std::vector<int> snareSteps (const Context& c)
    {
        if (c.halfTime)
            return { c.frame.halfBackbeat };

        return c.frame.backbeats;
    }

    //==============================================================================
    //  Metal: metalcore, deathcore, djent, thrash, death and black, metal
    //==============================================================================

    // A chug: the bar cut into groups of twos, threes and fours, a hit at the start of each (and
    // sometimes a double, "dun-dun"), the way a breakdown riff is built - 3-3-2, 3-3-3-3-2-2...
    std::vector<int> chugHits (Context& c, int length, juce::String& shape)
    {
        static const std::vector<std::vector<int>> shapes {
            { 3, 3, 2, 3, 3, 2 }, { 3, 3, 3, 3, 2, 2 }, { 3, 3, 2, 2, 2, 4 }, { 4, 3, 3, 2, 4 }, { 2, 3, 3, 2, 3, 3 },
            { 3, 3, 4, 3, 3 }, { 6, 3, 3, 4 }, { 3, 5, 3, 5 }, { 4, 4, 3, 3, 2 }, { 2, 2, 3, 3, 3, 3 } };

        std::vector<int> groups;

        if (length == 16)
        {
            groups = shapes[(size_t) c.pick ((int) shapes.size())];
        }
        else
        {
            for (int left = length; left > 0;)
            {
                const int g = left <= 4 ? left : (c.chance (0.6f) ? 3 : 2);
                groups.push_back (g);
                left -= g;
            }
        }

        juce::StringArray named;
        std::vector<int> hits;
        int at = 0;

        for (auto g : groups)
        {
            hits.push_back (at);

            // A double on some threes and fours ("dun-dun"), more of them the busier it is
            if (g >= 3 && c.chance (0.2f + 0.3f * c.busy))
                hits.push_back (at + 1);

            named.add (juce::String (g));
            at += g;
        }

        shape = named.joinIntoString ("-");
        return hits;
    }

    void metalFill (Grid& g, Context& c, bool intoBreakdown)
    {
        const int s = g.steps, beat = c.frame.perBeat;
        const int length = c.energy > 0.7f || intoBreakdown ? beat * 2 : (c.busy < 0.35f ? beat : beat * 2);
        const int from = s - length;
        g.clearAll (from, s);

        const int kind = c.pick (4);

        if (kind == 3 || intoBreakdown)
        {
            // Unison stops: snare, kick and crash together on the 8ths, a rest before the drop
            for (int t = from; t < s - 2; t += 2)
            {
                g.hit ("snare", t, '9');
                g.hit ("kick", t, '9');
                g.hit (c.chance (0.5f) ? "china" : "crash", t, '8');
            }

            c.note << ", unison stops into the next part";
            return;
        }

        // Down the toms in 16ths (snare, rack, rack, floor) with the double kick rolling under
        static const char* const around[] { "snare", "snare", "tom_high", "tom_high", "tom_mid", "tom_mid", "tom_floor", "tom_floor" };

        for (int t = from, i = 0; t < s; ++t, ++i)
        {
            const int segment = (i * 8) / std::max (1, length);
            const juce::String drum = around[juce::jlimit (0, 7, segment)];
            g.hit (drum, t, kind == 1 && (i % 4 == 0) ? 'f' : vel (7 + (i % 2 == 0 ? 1 : 0)));

            if (kind != 2)
                g.hit ("kick", t, '7');
            else if (i % 2 == 0)
                g.hit ("kick", t, '8');
        }
    }

    Grid metalBreakdown (Context& c)
    {
        Grid g (c.frame.steps);
        const int snare = c.frame.halfBackbeat;
        juce::String shape;
        auto hits = chugHits (c, g.steps, shape);

        for (auto h : hits)
            if (h != snare)
                g.hit ("kick", h, '9');

        g.hit ("snare", snare, '9');

        // Cymbals: the china on every hit that starts a group, or riding the quarters
        if (c.chance (0.55f))
        {
            for (auto h : hits)
                if (h != snare && h + 1 < g.steps && ! (std::find (hits.begin(), hits.end(), h - 1) != hits.end()))
                    g.hit ("china", h, '8');

            c.note = "half-time, the kick locked to a " + shape + " chug with the china on the hits";
        }
        else
        {
            for (int t = 0; t < g.steps; t += c.frame.perBeat)
                g.hit ("china", t, t == 0 ? '9' : '7');

            c.note = "half-time, the kick locked to a " + shape + " chug, the china riding the quarters";
        }

        // Weight under some chugs: the floor tom with the kick
        if (c.energy > 0.6f && c.chance (0.4f))
            for (auto h : hits)
                if (h != snare && c.chance (0.4f))
                    g.hit ("tom_floor", h, '8');

        return g;
    }

    Grid metalBlast (Context& c)
    {
        Grid g (c.frame.steps);
        const int type = c.pick (3);

        for (int t = 0; t < g.steps; ++t)
        {
            if (type == 0)
            {
                // Traditional: kick and ride together, snare in between, in 16ths
                if (t % 2 == 0)
                {
                    g.hit ("kick", t, '8');
                    g.hit ("ride", t, '7');
                }
                else
                {
                    g.hit ("snare", t, '8');
                }
            }
            else if (type == 1)
            {
                // Hammer: kick, snare and crash all together on the 8ths
                if (t % 2 == 0)
                {
                    g.hit ("kick", t, '9');
                    g.hit ("snare", t, '8');
                    g.hit (t % 4 == 0 ? "crash" : "ride", t, '7');
                }
            }
            else
            {
                // Bomb: double kick in 16ths, snare and ride on the 8ths
                g.hit ("kick", t, '8');

                if (t % 2 == 0)
                {
                    g.hit ("snare", t, '8');
                    g.hit ("ride", t, '6');
                }
            }
        }

        static const char* const names[] { "a traditional blast (kick and snare trading 16ths)", "a hammer blast (everything together on the 8ths)",
                                           "a bomb blast (16ths on the double kick, snare on the 8ths)" };
        c.note = names[type];
        return g;
    }

    Grid metalBar (Context& c)
    {
        const auto& f = c.frame;
        const auto st = c.id();
        Grid g (f.steps);

        if (c.kind == "breakdown")
            return metalBreakdown (c);

        const bool blastHere = c.req.blasts || (st == "blast" && c.kind != "intro")
                            || (st == "deathcore" && c.kind == "verse" && c.energy > 0.65f && c.chance (0.5f));

        if (blastHere && c.kind != "intro" && c.kind != "outro")
            return metalBlast (c);

        const auto snares = snareSteps (c);
        const int beat = f.perBeat;

        if (c.kind == "intro")
        {
            // A build: the floor tom in 8ths over the kick, a crash at the top of each bar
            for (int t = 0; t < g.steps; t += 2)
                g.hit ("tom_floor", t, t % beat == 0 ? '8' : '6');

            for (auto s : f.groupStarts)
                g.hit ("kick", s, '8');

            g.hit ("crash", 0, '9');
            c.note = "the floor tom in 8ths over the kick, a crash at the top of each bar";
            return g;
        }

        // Djent: the kick on a cycle that doesn't fit the bar (5, 7 or 9 16ths), the snare
        // holding half-time on 3, so the riff and the bar line keep crossing
        if (st == "djent" && c.kind != "chorus")
        {
            static const std::vector<std::vector<int>> cycles { { 0, 2, 3 }, { 0, 1, 4 }, { 0, 3, 5 }, { 0, 2, 4, 6 }, { 0, 1, 3, 6 } };
            static const int lengths[] { 5, 5, 7, 7, 9 };
            const int which = juce::jlimit (0, 4, c.cycle);
            const int length = lengths[which];

            for (int t = 0; t < g.steps; ++t)
            {
                const int pos = (c.phase + t) % length;

                if (std::find (cycles[(size_t) which].begin(), cycles[(size_t) which].end(), pos) != cycles[(size_t) which].end())
                    g.hit ("kick", t, pos == 0 ? '9' : '7');
            }

            g.clear ("kick", f.halfBackbeat, f.halfBackbeat + 1);
            g.hit ("snare", f.halfBackbeat, '9');

            for (int t = 0; t < g.steps; t += beat)
                g.hit ("china", t, '6');

            c.note = "a " + juce::String (length) + "-step kick cycle over the 4/4, snare holding half-time, so the riff keeps crossing the bar line";
            return g;
        }

        // Thrash: the skank beat (kick on the beats, snare on the offbeat 8ths), or gallops
        if (st == "thrash" || (c.req.gallops && c.kind == "verse"))
        {
            if (c.req.gallops || c.chance (0.4f))
            {
                for (int t = 0; t < g.steps; t += beat)
                {
                    g.hit ("kick", t, '9');
                    g.hit ("kick", t + beat / 2, '7');
                    g.hit ("kick", t + beat / 2 + 1, '7');
                }

                for (auto s : snares)
                    g.hit ("snare", s, '9');

                for (int t = 0; t < g.steps; t += 2)
                    g.hit (c.kind == "chorus" ? "crash" : "hat_closed", t, t % beat == 0 ? '8' : '6');

                c.note = "gallops on the kick (an 8th and two 16ths), snare on the backbeat";
            }
            else
            {
                for (int t = 0; t < g.steps; t += beat)
                {
                    g.hit ("kick", t, '9');
                    g.hit ("snare", t + beat / 2, '8');
                }

                for (int t = 0; t < g.steps; t += 2)
                    g.hit (c.kind == "chorus" ? "crash" : "ride", t, t % beat == 0 ? '8' : '6');

                c.note = "the skank beat: kick on the beats, snare on every offbeat 8th";
            }

            return g;
        }

        if (c.kind == "chorus" || c.kind == "hook" || c.kind == "drop")
        {
            // The chorus opens up: crash riding the quarters, the double kick rolling under it;
            // half-time (snare on 3) for a big, wide one, else the snare on 2 and 4
            const bool half = c.halfTime || c.chance (0.5f);
            const bool sixteenths = c.energy + c.req.busyKick * 0.5f > 0.55f || c.chance (0.4f);

            for (int t = 0; t < g.steps; t += sixteenths ? 1 : 2)
                g.hit ("kick", t, t % beat == 0 ? '9' : '7');

            for (int t = 0; t < g.steps; t += beat)
                g.hit (t == 0 || c.chance (0.75f) ? "crash" : "china", t, '8');

            const auto where = half ? std::vector<int> { f.halfBackbeat } : f.backbeats;

            for (auto s : where)
            {
                g.clear ("kick", s, s + 1);
                g.hit ("snare", s, '9');
            }

            c.note = juce::String (half ? "half-time" : "on 2 and 4") + ", crash on the quarters, the double kick rolling in "
                     + (sixteenths ? "16ths" : "8ths") + " underneath";
            return g;
        }

        if (c.kind == "bridge" || c.kind == "prechorus")
        {
            // The ride bell on the quarters over a syncopated kick, toms answering the snare
            juce::String shape;

            for (auto h : chugHits (c, g.steps, shape))
                g.hit ("kick", h, '8');

            for (auto s : snares)
            {
                g.clear ("kick", s, s + 1);
                g.hit ("snare", s, '9');
            }

            for (int t = 0; t < g.steps; t += beat)
                g.hit ("ride_bell", t, '7');

            c.note = "the ride bell on the quarters, the kick following a " + shape + " riff";
            return g;
        }

        // Verses: one of a metalcore drummer's three - the driving double kick under 2 and 4, the
        // skank beat, or a half-time groove with the kick syncopated to the riff
        const int choice = c.halfTime ? 2 : (c.energy > 0.75f ? c.pick (2) : c.pick (3));

        if (choice == 0)
        {
            const bool sixteenths = c.busy + c.req.busyKick > 0.45f;

            for (int t = 0; t < g.steps; t += sixteenths ? 1 : 2)
                g.hit ("kick", t, t % beat == 0 ? '9' : '7');

            for (auto s : snares)
            {
                g.clear ("kick", s, s + 1);
                g.hit ("snare", s, '9');
            }

            const bool ride = c.chance (0.4f);

            for (int t = 0; t < g.steps; t += 2)
                g.hit (ride ? "ride" : "hat_closed", t, t % beat == 0 ? '8' : '6');

            c.note = juce::String ("the double kick driving in ") + (sixteenths ? "16ths" : "8ths") + " under the snare on 2 and 4, "
                     + (ride ? "ride" : "hats") + " in 8ths";
        }
        else if (choice == 1)
        {
            for (int t = 0; t < g.steps; t += beat)
            {
                g.hit ("kick", t, '9');
                g.hit ("snare", t + beat / 2, '8');
            }

            for (int t = 0; t < g.steps; t += 2)
                g.hit ("hat_closed", t, '7');

            c.note = "the skank beat (kick on the beats, snare on the offbeats), driving and fast";
        }
        else
        {
            juce::String shape;

            for (auto h : chugHits (c, g.steps, shape))
                g.hit ("kick", h, '8');

            g.clear ("kick", f.halfBackbeat, f.halfBackbeat + 1);
            g.hit ("snare", f.halfBackbeat, '9');

            for (int t = 0; t < g.steps; t += 2)
                g.hit ("hat_closed", t, t % beat == 0 ? '8' : '5');

            c.note = "half-time, the kick syncopated to a " + shape + " riff, hats in 8ths";
        }

        return g;
    }

    //==============================================================================
    //  Trap and hip-hop: trap, drill, boom bap, hip-hop
    //==============================================================================

    // Hi-hat rolls on the last beat: 16th triplets (the bar goes to 48 steps) or 32nds (32)
    void trapRoll (Grid& g, Context& c, bool long_)
    {
        if (! c.frame.fourFour())
            return;

        // A bar already on the triplet grid (drill's hats) rolls in triplets
        const bool triplets = g.steps % 3 == 0 || (g.steps == 16 && c.chance (0.55f));
        const int fine = triplets ? 48 : 32;
        g.resample (fine);

        if (g.steps != fine)
            return;

        const int beat = fine / 4;
        const int from = long_ ? fine - beat * 2 : fine - beat;
        g.clear ("hat_closed", from, fine);
        const int every = triplets ? 2 : 1;
        int n = 0;

        for (int t = from; t < fine; t += every, ++n)
            g.hit ("hat_closed", t, vel (3 + (n * 6) / std::max (1, (fine - from) / every)));

        c.note << (triplets ? ", hat rolls in 16th triplets" : ", hat rolls in 32nds");
    }

    Grid trapBar (Context& c)
    {
        const auto& f = c.frame;
        const auto st = c.id();
        Grid g (f.steps);
        const int beat = f.perBeat;
        const int snare = f.fourFour() ? 8 : f.halfBackbeat;

        if (st == "boombap" || st == "hiphop")
        {
            // Boom bap: kick on 1 and around the "and" of 2, snare on 2 and 4 laid back, hats in
            // 8ths with a few 16th ghosts, an open hat on the "and" of 4 now and then
            static const std::vector<std::vector<int>> kicks { { 0, 6, 10 }, { 0, 7, 10 }, { 0, 3, 10 }, { 0, 6, 9, 10 }, { 0, 7, 8, 14 }, { 0, 10, 11 } };
            const auto& k = kicks[(size_t) c.pick ((int) kicks.size())];

            if (c.kind != "intro")
                for (auto s : k)
                    if (s < g.steps)
                        g.hit ("kick", s, s == 0 ? '8' : '7');

            for (auto s : f.backbeats)
                g.hit ("snare", s, '8');

            for (int t = 0; t < g.steps; t += 2)
                g.hit ("hat_closed", t, t % beat == 0 ? '6' : '4');

            if (c.busy + c.req.busyHats * 0.5f > 0.5f)
                for (int t = 1; t < g.steps; t += 2)
                    if (c.chance (0.35f))
                        g.hit ("hat_closed", t, '2');

            if (c.chance (0.5f) && g.steps > 14)
            {
                g.clear ("hat_closed", 14, 15);
                g.hit ("hat_open", 14, '5');
            }

            if (c.req.ghostNotes || c.chance (0.5f))
                for (int t : { 7, 15 })
                    if (t < g.steps && c.chance (0.6f))
                        g.hit ("snare", t, 'g');

            c.note = "the kick on 1 and around the \"and\" of 2, snare laid back on 2 and 4, hats in 8ths, swung 16ths";
            return g;
        }

        // Trap and drill: half-time, the clap and snare together on 3
        g.hit ("snare", snare, '8');
        g.hit ("clap", snare, '8');

        if (st == "drill")
        {
            // Drill: the second snare off the beat (the "a" of 3 or the "e" of 4), hats in
            // triplets, a kick that leaves room for the sliding 808
            const int second = f.fourFour() ? (c.chance (0.5f) ? 11 : 13) : -1;

            if (second >= 0)
                g.hit ("snare", second, '7');

            if (c.kind != "intro")
            {
                g.hit ("kick", 0, '8');

                if (f.fourFour())
                    g.hit ("kick", c.chance (0.5f) ? 6 : 10, '7');
            }

            if (f.fourFour())
            {
                g.resample (48);

                // Triplet 8ths (every 4 steps of 48), accents in threes
                for (int t = 0, n = 0; t < 48; t += 4, ++n)
                    g.hit ("hat_closed", t, n % 3 == 0 ? '6' : '4');
            }
            else
            {
                for (int t = 0; t < g.steps; t += 2)
                    g.hit ("hat_closed", t, '5');
            }

            c.note = "drill: the snare on 3 and again off the beat, hats in triplets, the kick leaving room for the 808";
            return g;
        }

        // Trap: hats in 16ths (8ths when it's sparse), the kick motif syncopated round the clap
        const bool sixteenths = c.busy + c.req.busyHats * 0.6f > 0.35f;

        for (int t = 0; t < g.steps; t += sixteenths ? 1 : 2)
            g.hit ("hat_closed", t, t % 2 == 0 ? '6' : '4');

        if (c.kind != "intro")
        {
            static const int places[] { 3, 6, 7, 10, 11, 13, 14 };
            g.hit ("kick", 0, '9');
            const int extra = 1 + c.pick (c.busy + c.req.busyKick * 0.5f > 0.5f ? 3 : 2);

            for (int i = 0; i < extra; ++i)
            {
                const int s = places[c.pick (7)];

                if (s < g.steps && s != snare)
                    g.hit ("kick", s, '8');
            }
        }

        if (c.chance (0.3f) && g.steps > 14)
            g.hit ("hat_open", 14, '5');

        c.note = c.kind == "intro" ? "hats and the clap alone, the kick holding back"
                                   : "half-time, clap and snare on 3, the kick syncopated round it, hats in " + juce::String (sixteenths ? "16ths" : "8ths");
        return g;
    }

    //==============================================================================
    //  Rock: rock, hard rock, pop-punk, punk
    //==============================================================================

    void rockFill (Grid& g, Context& c)
    {
        const int s = g.steps, beat = c.frame.perBeat;
        const int length = c.energy > 0.6f || c.busy > 0.6f ? beat * 2 : beat;
        const int from = s - length;
        g.clearAll (from, s);
        const bool punky = c.id() == "poppunk" || c.id() == "punk";

        if (punky && c.chance (0.5f))
        {
            // The snare in 16ths, flammed on the beats, getting louder
            for (int t = from, i = 0; t < s; ++t, ++i)
                g.hit ("snare", t, t % beat == 0 ? 'f' : vel (5 + (i * 4) / length));

            c.note << ", snare rolls into each part";
            return;
        }

        static const char* const around[] { "snare", "tom_high", "tom_mid", "tom_floor" };

        for (int t = from, i = 0; t < s; ++t, ++i)
            g.hit (around[juce::jlimit (0, 3, (i * 4) / length)], t, vel (6 + (i % 2 == 0 ? 2 : 0)));

        g.hit ("kick", s - beat, '8');
        c.note << ", fills round the toms into each part";
    }

    Grid rockBar (Context& c)
    {
        const auto& f = c.frame;
        const auto st = c.id();
        Grid g (f.steps);
        const int beat = f.perBeat;
        const auto snares = snareSteps (c);
        const bool loud = c.kind == "chorus" || c.kind == "hook" || c.kind == "drop";

        if (st == "punk" && c.kind != "intro")
        {
            // The d-beat: kick on 1 and its "and", snare on 2, kick on 3 and its "and", snare on 4
            for (int b = 0; b < (int) f.groupStarts.size(); ++b)
            {
                const int s = f.groupStarts[(size_t) b];
                const bool snareBeat = std::find (f.backbeats.begin(), f.backbeats.end(), s) != f.backbeats.end();
                g.hit (snareBeat ? "snare" : "kick", s, '9');

                if (! snareBeat)
                    g.hit ("kick", s + beat / 2, '8');
            }

            for (int t = 0; t < g.steps; t += 2)
                g.hit (loud ? "crash" : "ride", t, '7');

            c.note = "the d-beat (kick on 1 and its \"and\", snare on 2), " + juce::String (loud ? "crashes" : "ride") + " on the 8ths";
            return g;
        }

        if (st == "poppunk" && (c.kind == "verse" || loud) && ! c.halfTime && (loud || c.chance (0.5f)))
        {
            // The skate beat: kick on every beat, snare on every offbeat 8th, a crash or ride with it
            for (int t = 0; t < g.steps; t += beat)
            {
                g.hit ("kick", t, '9');
                g.hit ("snare", t + beat / 2, '8');
            }

            for (int t = 0; t < g.steps; t += 2)
                g.hit (loud ? "crash" : "ride", t, t % beat == 0 ? '8' : '6');

            c.note = "the skate beat: kick on every beat, snare on every offbeat, " + juce::String (loud ? "crash" : "ride") + " in 8ths";
            return g;
        }

        if (c.kind == "bridge" && c.chance (0.5f))
        {
            // A tom groove: the floor tom in 8ths with the snare on the backbeat
            for (int t = 0; t < g.steps; t += 2)
                g.hit ("tom_floor", t, t % beat == 0 ? '8' : '6');

            for (auto s : snares)
                g.hit ("snare", s, '9');

            g.hit ("kick", 0, '8');
            c.note = "a tom groove: the floor tom in 8ths under the snare";
            return g;
        }

        // The rock beat: kick on 1 and 3 with a push or two, snare on 2 and 4, 8ths on top
        // (the hats in the verse, the ride or an open hat in the chorus)
        g.hit ("kick", 0, '9');

        if (c.kind != "intro" || c.chance (0.5f))
        {
            if (! c.halfTime && f.groupStarts.size() >= 3)
                g.hit ("kick", f.groupStarts[2], '8');

            static const int pushes[] { 6, 10, 11, 14, 3 };
            const int extra = c.busy + c.req.busyKick * 0.5f > 0.6f ? 2 : (c.chance (0.6f) ? 1 : 0);

            for (int i = 0; i < extra; ++i)
                if (const int s = pushes[c.pick (5)]; s < g.steps && std::find (snares.begin(), snares.end(), s) == snares.end())
                    g.hit ("kick", s, '7');
        }

        for (auto s : snares)
            g.hit ("snare", s, '9');

        const juce::String top = loud ? (st == "hardrock" || c.chance (0.5f) ? "ride" : "hat_open") : "hat_closed";
        const bool sixteenths = c.busy + c.req.busyHats * 0.6f > 0.75f && top == "hat_closed";

        for (int t = 0; t < g.steps; t += sixteenths ? 1 : 2)
            g.hit (top, t, t % beat == 0 ? '8' : (t % 2 == 0 ? '6' : '4'));

        if (c.req.ghostNotes)
            for (int t = 3; t < g.steps; t += 4)
                if (! g.on ("snare", t) && c.chance (0.5f))
                    g.hit ("snare", t, 'g');

        c.note = juce::String (c.halfTime ? "half-time" : "kick on 1 and 3 with a push, snare on 2 and 4")
                 + ", " + (top == "hat_closed" ? juce::String ("hats") : top == "ride" ? juce::String ("the ride") : juce::String ("open hats")) + " in "
                 + (sixteenths ? "16ths" : "8ths");
        return g;
    }

    //==============================================================================
    //  Lofi, jazz and neo-soul
    //==============================================================================

    // Jazz on a triplet grid (three steps a beat): the ride's spang-a-lang, the hats on 2 and 4,
    // the kick feathered, the snare comping in between
    Grid jazzBar (Context& c)
    {
        const int beats = (int) c.frame.groupStarts.size();
        Grid g (beats * 3);
        const bool brushes = c.req.brushes || c.id() == "ballad";
        const bool fast = c.tempo > 220.0;

        for (int b = 0; b < beats; ++b)
        {
            const int t = b * 3;

            if (brushes)
            {
                // Brushes: the snare swept on every triplet, the backbeat a little stronger
                g.hit ("snare", t, b % 2 == 1 ? '4' : 'g');
                g.hit ("snare", t + 1, 'g');
                g.hit ("snare", t + 2, 'g');
            }
            else
            {
                g.hit ("ride", t, b % 2 == 1 ? '7' : '6');

                // The skip note on the last triplet of 2 and 4, leading into 3 and 1 (ding, ding-a
                // ding, ding-a), dropped when it's very fast
                if (b % 2 == 1 && ! fast)
                    g.hit ("ride", t + 2, '4');
            }

            if (b % 2 == 1)
                g.hit ("hat_pedal", t, '6');

            if (c.chance (brushes ? 0.5f : 0.8f))
                g.hit ("kick", t, 'g');
        }

        if (! brushes)
        {
            // Comping: a few snare hits on the triplets in between, one a bar louder; now and then
            // a bomb on the kick
            const int comps = 1 + c.pick (c.busy > 0.6f ? 4 : 3);

            for (int i = 0; i < comps; ++i)
            {
                const int t = c.pick (beats) * 3 + 1 + c.pick (2);
                g.hit ("snare", t, i == 0 ? '5' : 'g');
            }

            if (c.energy > 0.55f && c.chance (0.35f))
                g.hit ("kick", c.pick (beats) * 3 + 2, '6');
        }

        c.note = brushes ? "brushes swept in triplets, the hats on 2 and 4, the kick feathered"
                         : "the ride's spang-a-lang, hats on 2 and 4, the kick feathered, the snare comping in between";
        return g;
    }

    Grid lofiBar (Context& c)
    {
        const auto& f = c.frame;
        const auto st = c.id();

        if ((st == "jazz" || st == "ballad") && (f.unit == 4 || f.compound))
            return jazzBar (c);

        Grid g (f.steps);
        const int beat = f.perBeat;
        const bool soul = st == "neosoul";

        // Lofi and neo-soul: a lazy kick round the beat, the snare (or a rim click) late on 2 and
        // 4 with ghosts round it, the hats loose and uneven
        static const std::vector<std::vector<int>> lofiKicks { { 0, 7, 10 }, { 0, 3, 10 }, { 0, 10 }, { 0, 6, 10, 11 }, { 0, 7, 8 } };
        static const std::vector<std::vector<int>> soulKicks { { 0, 3, 7, 10 }, { 0, 6, 9, 13 }, { 0, 3, 10, 13 }, { 0, 7, 10, 14 }, { 0, 2, 7, 10 } };
        const auto& kicks = soul ? soulKicks : lofiKicks;
        const auto& k = kicks[(size_t) c.pick ((int) kicks.size())];

        if (c.kind != "intro")
            for (auto s : k)
                if (s < g.steps)
                    g.hit ("kick", s, s == 0 ? '7' : '6');

        const bool rim = c.kind == "intro" || (c.energy < 0.4f && c.chance (0.6f)) || (soul && c.kind == "verse" && c.chance (0.4f));

        for (auto s : snareSteps (c))
            g.hit (rim ? "sidestick" : "snare", s, '7');

        for (int t = 0; t < g.steps; t += 2)
            if (! c.chance (0.08f))
                g.hit ("hat_closed", t, vel (t % beat == 0 ? 5 : 3 + c.pick (2)));

        if (soul || c.busy + c.req.busyHats * 0.5f > 0.55f)
            for (int t = 1; t < g.steps; t += 2)
                if (c.chance (soul ? 0.6f : 0.35f))
                    g.hit ("hat_closed", t, '2');

        if (c.req.ghostNotes || soul || c.chance (0.5f))
            for (int t : { 3, 7, 11, 15 })
                if (t < g.steps && ! g.on ("snare", t) && c.chance (soul ? 0.5f : 0.3f))
                    g.hit ("snare", t, 'g');

        c.note = soul ? "the Dilla pocket: the kick pushing early, the snare dragging late with ghosts round it, the hats loose"
                      : "a lazy kick, the " + juce::String (rim ? "rim click" : "snare") + " behind the beat on 2 and 4, loose swung hats";
        return g;
    }

    void lofiFill (Grid& g, Context& c)
    {
        // A soft pickup rather than a fill: a couple of ghosts or a tom at the end of the bar
        const int s = g.steps;
        g.clear ("hat_closed", s - 2, s);
        g.hit ("snare", s - 2, 'g');
        g.hit ("snare", s - 1, c.chance (0.5f) ? '5' : 'g');

        if (c.chance (0.4f))
            g.hit ("tom_mid", s - 1, '5');

        c.note << ", soft pickups into each part";
    }

    void jazzFill (Grid& g, Context& c)
    {
        // Triplets on the snare and toms through the last beat (or two)
        const int beatSteps = 3;
        const int length = c.busy > 0.5f ? beatSteps * 2 : beatSteps;
        const int from = g.steps - length;
        g.clearAll (from, g.steps);
        static const char* const around[] { "snare", "snare", "tom_high", "tom_mid", "tom_floor", "snare" };

        for (int t = from, i = 0; t < g.steps; ++t, ++i)
            g.hit (around[i % 6], t, vel (4 + i % 3));

        c.note << ", triplet fills to hand over";
    }

    //==============================================================================
    //  Funk
    //==============================================================================

    // A disco chorus (four on the floor, the open hat on every offbeat) or the pocket, decided once
    // a section so its candidates are scored as what they are
    bool funkDisco (const Context& c)
    {
        return (c.kind == "chorus" || c.kind == "drop") && c.frame.fourFour() && c.cycle % 2 == 0;
    }

    // Funk: everything on the one. 16ths on the hats with the 8ths leaning, the snare cracking on 2
    // and 4 with ghost notes round it, the kick syncopated in between and never on a backbeat, an
    // open hat barking on an offbeat and choked on the next. Faster it plays 8ths; busier it goes
    // linear (no two limbs at once, the hats filling every gap); a chorus can go disco
    Grid funkBar (Context& c)
    {
        const auto& f = c.frame;
        Grid g (f.steps);
        const int beat = f.perBeat;
        const auto backbeats = snareSteps (c);
        const bool disco = funkDisco (c);
        const bool linear = ! disco && c.kind != "intro" && c.busy + c.req.busyHats * 0.3f > 0.65f && c.chance (0.6f);
        const bool sixteenths = c.tempo <= 118.0 || c.req.busyHats > 0.0f || linear;
        const bool bridge = c.kind == "bridge" || c.kind == "breakdown";

        auto isBackbeat = [&] (int s) { return std::find (backbeats.begin(), backbeats.end(), s) != backbeats.end(); };

        // The kick: the one, hard, then syncopations between the backbeats
        if (disco)
        {
            for (auto s : f.groupStarts)
                g.hit ("kick", s, s == 0 ? '9' : '8');
        }
        else if (f.fourFour() && ! c.halfTime)
        {
            static const std::vector<std::vector<int>> kicks {
                { 0, 3, 6, 10 }, { 0, 6, 10, 11 }, { 0, 2, 7, 10 }, { 0, 3, 10, 14 }, { 0, 7, 8, 11 },
                { 0, 6, 9, 14 }, { 0, 10, 13 }, { 0, 3, 7, 10, 15 }, { 0, 2, 10 }, { 0, 6, 8, 14 } };
            const auto& k = kicks[(size_t) c.pick ((int) kicks.size())];

            for (auto s : k)
                if (! bridge || s == 0 || c.chance (0.5f))
                    g.hit ("kick", s, s == 0 ? '9' : vel (6 + c.pick (2)));
        }
        else
        {
            // Odd meters and half-time: the one, then 16ths just off the groups
            g.hit ("kick", 0, '9');
            const int wanted = juce::jmax (2, f.steps / 5);

            for (int i = 0, tries = 0; i < wanted && tries < 40; ++tries)
            {
                const int s = 1 + c.pick (f.steps - 1);

                if (! isBackbeat (s) && ! g.on ("kick", s) && ! isGroupStart (f, s) && ! g.on ("kick", s - 1))
                {
                    g.hit ("kick", s, vel (6 + c.pick (2)));
                    ++i;
                }
            }
        }

        // The backbeat, and now and then its answer on the "a" of 2 (the displaced snare)
        for (auto s : backbeats)
            g.hit ("snare", s, '8');

        if (! disco && f.fourFour() && ! c.halfTime && c.kind != "intro" && c.chance (0.2f + 0.2f * c.busy) && ! g.on ("kick", 7))
            g.hit ("snare", 7, '6');

        // Ghost notes on the 16ths round the backbeat, where the kick isn't
        if (! disco && c.kind != "intro")
        {
            const float p = 0.3f + 0.3f * c.busy + (c.req.ghostNotes ? 0.25f : 0.0f);

            for (int s = 1; s < g.steps; s += 2)
                if (! g.on ("kick", s) && ! g.on ("snare", s) && ! g.on ("kick", s + 1) && c.chance (p))
                    g.hit ("snare", s, 'g');

            // Never without one: the "a" of 3 leading into the backbeat, or the nearest free 16th
            if (g.count ("snare") <= (int) backbeats.size() + (g.on ("snare", 7) ? 1 : 0))
                for (int s : { 11, 7, 15, 3, 5, 13 })
                    if (s < g.steps && ! g.on ("kick", s) && ! g.on ("snare", s))
                    {
                        g.hit ("snare", s, 'g');
                        break;
                    }
        }

        // The hats
        if (disco)
        {
            for (int s = 0; s < g.steps; s += 2)
                g.hit (s % beat == 0 ? "hat_pedal" : "hat_open", s, s % beat == 0 ? '5' : '7');
        }
        else if (linear)
        {
            for (int s = 0; s < g.steps; ++s)
                if (! g.on ("kick", s) && ! g.on ("snare", s))
                    g.hit ("hat_closed", s, vel (s % beat == 0 ? 6 : s % 2 == 0 ? 5 : 3));
        }
        else
        {
            for (int s = 0; s < g.steps; s += sixteenths && ! bridge ? 1 : 2)
                g.hit ("hat_closed", s, vel (s % beat == 0 ? 6 : s % 2 == 0 ? 5 : 3));

            // The bark: an open hat on an offbeat, choked by the next hat
            if (c.kind != "intro" && c.chance (0.75f))
            {
                static const int barks[] { 14, 6, 7, 10 };
                const int s = f.fourFour() ? barks[c.pick (c.energy > 0.55f ? 4 : 2)] : f.steps - 2;
                g.clear ("hat_closed", s, s + 1);
                g.hit ("hat_open", s, '7');
            }
        }

        c.note = disco ? "disco: four on the floor, the open hat on every offbeat, the snare on 2 and 4"
               : linear ? "linear funk: no two limbs at once, the hats filling every gap round the syncopated kick and the ghosted snare"
               : bridge ? "the pocket thinned out: the one, the backbeat and 8ths on the hats"
               : juce::String ("on the one: ") + (sixteenths ? "16ths" : "8ths") + " on the hats, the snare cracking on 2 and 4"
                   + (c.kind == "intro" ? "" : " with ghost notes round it") + ", the kick syncopated in between";
        return g;
    }

    void funkFill (Grid& g, Context& c)
    {
        // 16ths round the kit, the snare accents leading into the toms, a flam to start
        const int length = c.busy > 0.5f || c.energy > 0.6f ? c.frame.perBeat * 2 : c.frame.perBeat;
        const int from = g.steps - length;
        g.clearAll (from, g.steps);
        static const char* const around[] { "snare", "snare", "snare", "tom_high", "tom_high", "tom_mid", "tom_floor", "tom_floor" };
        static const char hits[] { 'f', 'g', '6', '6', '5', '6', '7', '8' };
        const int offset = 8 - length > 0 ? 8 - length : 0;

        for (int t = from, i = 0; t < g.steps; ++t, ++i)
            g.hit (around[(i + offset) % 8], t, hits[(i + offset) % 8]);

        g.hit ("kick", from, '7');
        c.note << ", 16th fills round the toms into the one";
    }

    //==============================================================================
    // Scoring a candidate against its style's rules: the kick where the genre wants it, about as
    // many as it should have, off the beat about as often, never fighting the snare, and not a
    // copy of the last section's
    struct Targets
    {
        float kicks = 4.0f, syncopation = 0.3f, kickWeight = 0.6f;
        bool kickOnOne = true, kickWithSnare = false;
    };

    Targets targetsFor (const Context& c)
    {
        Targets t;
        const auto family = juce::String (c.style.family);
        const float steps = (float) c.frame.steps;

        if (family == "metal")
        {
            t.kicks = (c.kind == "breakdown" ? 5.0f : 8.0f) * steps / 16.0f * (0.7f + 0.6f * c.busy);
            t.syncopation = c.kind == "breakdown" ? 0.55f : 0.4f;
            t.kickWeight = 0.25f;
            t.kickWithSnare = c.req.blasts || c.id() == "blast";
        }
        else if (family == "trap")
        {
            const bool boom = c.id() == "boombap" || c.id() == "hiphop";
            t.kicks = (boom ? 3.0f : 3.0f + c.busy * 2.0f) * steps / 16.0f;
            t.syncopation = boom ? 0.45f : 0.6f;
            t.kickOnOne = c.kind != "intro";
        }
        else if (family == "rock")
        {
            t.kicks = (3.0f + c.busy * 2.0f) * steps / 16.0f;
            t.syncopation = 0.3f;
        }
        else if (family == "funk")
        {
            const bool disco = funkDisco (c);
            t.kicks = (disco ? 4.0f : 3.5f + c.busy * 1.5f) * steps / 16.0f;
            t.syncopation = disco ? 0.0f : 0.6f;
            t.kickWeight = 0.8f;
        }
        else
        {
            t.kicks = (3.0f + c.busy * 1.5f) * steps / 16.0f;
            t.syncopation = c.id() == "neosoul" ? 0.55f : 0.4f;
            t.kickOnOne = c.kind != "intro";
        }

        t.kicks *= 1.0f + c.req.busyKick * 0.4f;
        return t;
    }

    float score (const Grid& g, const Context& c, const Targets& t, const Grid* previousSection)
    {
        float s = 0.0f;
        const int kicks = g.count ("kick");
        const int factor = std::max (1, g.steps / std::max (1, c.frame.steps));   // finer bars (rolls) count in 16ths

        if (t.kickOnOne)
            s += g.on ("kick", 0) ? 3.0f : -5.0f;

        s -= std::abs ((float) kicks - t.kicks) * t.kickWeight;

        int off = 0;

        for (int step = 0; step < g.steps; ++step)
            if (g.on ("kick", step) && ! isGroupStart (c.frame, step / factor))
                ++off;

        if (kicks > 0)
            s -= std::abs ((float) off / (float) kicks - t.syncopation) * 4.0f;

        if (! t.kickWithSnare)
            for (int step = 0; step < g.steps; ++step)
                if (g.on ("kick", step) && g.on ("snare", step) && g.at ("snare", step) != 'g')
                    s -= 2.0f;

        // Something new in each section: a kick part that isn't the last one's again
        if (previousSection != nullptr && previousSection->steps == g.steps)
        {
            int differs = 0;

            for (int step = 0; step < g.steps; ++step)
                differs += g.on ("kick", step) != previousSection->on ("kick", step) ? 1 : 0;

            s += differs > 2 ? 1.0f : -2.0f;
        }

        return s;
    }

    //==============================================================================
    // A section: the best of many candidates, played through its bars like a drummer would -
    // phrases of four, a variation on the fourth, a fill into the next part, a crash where it lands
    Grid mainBar (Context& c, const Grid* previousSection)
    {
        const auto family = juce::String (c.style.family);
        const auto targets = targetsFor (c);
        std::optional<Grid> best;
        float bestScore = -1.0e9f;
        juce::String bestNote;

        for (int i = 0; i < 20; ++i)
        {
            c.note = {};
            Grid g = family == "metal" ? metalBar (c) : family == "trap" ? trapBar (c) : family == "rock" ? rockBar (c)
                   : family == "funk" ? funkBar (c) : lofiBar (c);
            const float sc = score (g, c, targets, previousSection) + c.rng.nextFloat() * 0.5f;

            if (sc > bestScore)
            {
                bestScore = sc;
                best = g;
                bestNote = c.note;
            }
        }

        c.note = bestNote;
        return *best;
    }

    void vary (Grid& g, Context& c, int barInPhrase)
    {
        // The fourth bar of a phrase turns round: a pickup into the next bar
        if (barInPhrase != 3)
            return;

        const auto family = juce::String (c.style.family);
        const int s = g.steps;

        if (family == "trap")
        {
            const auto st = c.id();

            if (st == "trap" || st == "drill")
                trapRoll (g, c, false);
            else if (s > 15)
                g.hit ("kick", 15, '6');
        }
        else if (family == "metal" && c.kind != "breakdown")
        {
            g.clear ("hat_closed", s - 2, s);
            g.hit ("snare", s - 2, '8');
            g.hit ("snare", s - 1, '9');
        }
        else if (family == "funk")
        {
            // A pickup on the "a" of 4 into the one, the hat opening over it
            g.clear ("hat_closed", s - 2, s);
            g.hit ("hat_open", s - 2, '7');
            g.hit ("snare", s - 1, '7');
        }
        else if (family == "rock")
        {
            if (! g.on ("snare", s - 1))
                g.hit ("snare", s - 1, '7');

            if (s > 2)
            {
                g.clear ("hat_closed", s - 2, s - 1);
                g.hit ("hat_open", s - 2, '7');
            }
        }
        else if (! (c.id() == "jazz" || c.id() == "ballad"))
        {
            g.hit ("snare", s - 1, 'g');
        }
    }

    Section writeSection (Context& c, const juce::String& name, int bars, bool last, const juce::String& nextKind,
                          const Grid* previousSection, Grid& kept, juce::StringArray& notes)
    {
        Section section;
        section.name = name;
        const auto family = juce::String (c.style.family);
        const bool djent = c.id() == "djent";
        c.cycle = c.pick (5);
        auto main = mainBar (c, previousSection);
        kept = main;
        juce::String note = c.note;
        const bool slowBreakdown = c.kind == "breakdown" && (c.id() == "deathcore" || c.energy > 0.85f);

        for (int b = 0; b < bars; ++b)
        {
            // Djent's cycle carries on across the bar line, so each bar is written where it's got to
            if (djent && b > 0 && c.kind != "chorus" && c.kind != "breakdown")
            {
                c.phase = (c.phase + c.frame.steps) % 315;
                main = metalBar (c);
            }

            Grid g = main;

            // Deathcore's breakdowns go slower still: the snare on 3 only every other bar
            if (slowBreakdown && b % 2 == 1)
                g.clear ("snare", 0, g.steps);

            vary (g, c, b % 4);

            const bool endOfSection = b == bars - 1;

            if (endOfSection && ! last)
            {
                if (family == "metal")
                    metalFill (g, c, nextKind == "breakdown");
                else if (family == "rock")
                    rockFill (g, c);
                else if (family == "funk")
                    funkFill (g, c);
                else if (family == "trap")
                {
                    if (c.id() == "trap" || c.id() == "drill")
                    {
                        trapRoll (g, c, true);

                        // The kick drops out before the hook lands
                        if (nextKind == "hook" || nextKind == "drop" || nextKind == "chorus")
                            g.clear ("kick", g.steps / 2, g.steps);
                    }
                }
                else if (c.id() == "jazz" || c.id() == "ballad")
                    jazzFill (g, c);
                else
                    lofiFill (g, c);
            }

            // The ending: one big hit, then let it ring
            if (endOfSection && last && c.kind == "outro")
            {
                g.clearAll (1, g.steps);
                g.hit ("crash", 0, '9');
                g.hit ("kick", 0, '9');
            }

            // A crash where each part lands (not in lofi, jazz or trap, which don't)
            if (b == 0 && (family == "metal" || family == "rock" || family == "funk") && c.kind != "intro")
                g.hit (c.kind == "breakdown" ? "china" : "crash", 0, '9');

            section.bars.push_back (g.toBar());
        }

        if (note.isNotEmpty())
            notes.add (name + ": " + note + ".");

        return section;
    }

    //==============================================================================
    juce::String kitFor (const juce::String& style, bool brushes, const juce::StringArray& kitNames)
    {
        static const std::map<juce::String, juce::StringArray> preferences {
            { "metalcore", { "Crocell", "Muldjord", "DRS", "Red Zeppelin", "Black Pearl" } },
            { "deathcore", { "Crocell", "Muldjord", "DRS", "Red Zeppelin" } },
            { "djent",     { "Crocell", "Muldjord", "DRS", "Black Pearl" } },
            { "thrash",    { "Crocell", "Muldjord", "Red Zeppelin" } },
            { "blast",     { "Crocell", "Muldjord", "DRS" } },
            { "metal",     { "Crocell", "Muldjord", "Red Zeppelin" } },
            { "trap",      { "808" } },
            { "drill",     { "808" } },
            { "boombap",   { "Black Pearl", "909", "Red Zeppelin" } },
            { "hiphop",    { "909", "Black Pearl" } },
            { "rock",      { "Muldjord", "DRS", "Red Zeppelin", "Black Pearl" } },
            { "hardrock",  { "Muldjord", "Red Zeppelin", "DRS" } },
            { "poppunk",   { "DRS", "Muldjord", "Red Zeppelin", "Black Pearl" } },
            { "punk",      { "Red Zeppelin", "DRS", "Black Pearl" } },
            { "lofi",      { "Blonde Bop", "Black Pearl", "DRS Brushes" } },
            { "jazz",      { "Blonde Bop", "Buskman", "DRS" } },
            { "ballad",    { "DRS Brushes", "Blonde Bop" } },
            { "neosoul",   { "Blonde Bop Hot Rods", "Black Pearl", "Blonde Bop" } },
            { "funk",      { "Black Pearl", "DRS", "Red Zeppelin", "Blonde Bop Hot Rods" } },
        };

        juce::StringArray wanted;

        if (brushes)
            wanted.add ("DRS Brushes");

        if (auto it = preferences.find (style); it != preferences.end())
            wanted.addArray (it->second);

        for (auto& w : wanted)
        {
            // An exact name first, then one that has it in its name
            for (auto& k : kitNames)
                if (k.equalsIgnoreCase (w))
                    return k;

            for (auto& k : kitNames)
                if (k.containsIgnoreCase (w))
                    return k;
        }

        return {};
    }

    // The song form a style's drummer would play when no sections are asked for
    juce::StringArray formFor (const juce::String& style)
    {
        static const std::map<juce::String, juce::StringArray> forms {
            { "metalcore", { "intro", "verse", "chorus", "breakdown" } },
            { "deathcore", { "intro", "verse", "breakdown" } },
            { "djent",     { "verse", "chorus", "breakdown" } },
            { "thrash",    { "intro", "verse", "chorus" } },
            { "blast",     { "verse", "chorus" } },
            { "metal",     { "intro", "verse", "chorus" } },
            { "trap",      { "intro", "hook", "verse" } },
            { "drill",     { "intro", "hook", "verse" } },
            { "boombap",   { "intro", "verse", "hook" } },
            { "hiphop",    { "intro", "verse", "hook" } },
            { "rock",      { "verse", "chorus", "bridge" } },
            { "hardrock",  { "intro", "verse", "chorus" } },
            { "poppunk",   { "intro", "verse", "chorus", "bridge" } },
            { "punk",      { "verse", "chorus" } },
            { "lofi",      { "intro", "verse" } },
            { "funk",      { "intro", "verse", "chorus", "bridge" } },
            { "jazz",      { "verse", "chorus" } },
            { "ballad",    { "verse" } },
            { "neosoul",   { "intro", "verse", "hook" } },
        };

        if (auto it = forms.find (style); it != forms.end())
            return it->second;

        return { "verse", "chorus" };
    }

    juce::String sectionName (const juce::String& kind, const juce::String& style)
    {
        if (style == "jazz" || style == "ballad")
            return kind == "verse" ? "Head" : kind == "chorus" ? "Solos" : kind.substring (0, 1).toUpperCase() + kind.substring (1);

        if (kind == "prechorus")
            return "Pre-chorus";

        return kind.substring (0, 1).toUpperCase() + kind.substring (1);
    }
}

//==============================================================================
Result write (const juce::String& prompt, const Song& song, const agents::Request* previous,
              const juce::StringArray& kitNames, bool mayPickKit, const juce::String& currentKit,
              bool songIsBlank, juce::int64 seed)
{
    Result result;
    auto req = agents::Request::parse (prompt, previous);

    if (req.style.isEmpty())
    {
        req.style = "rock";
        req.understood.insert (0, "rock (no style named)");
    }

    const auto* style = agents::findStyle (req.style);

    if (style == nullptr)
        style = agents::findStyle ("rock");

    juce::Random rng (seed);
    const int beats = req.meterBeats > 0 ? req.meterBeats : song.meterBeats;
    const int unit = req.meterUnit > 0 ? req.meterUnit : song.meterUnit;
    const auto frame = frameFor (beats, unit);

    // The style's usual song, with anything asked for added where it goes (an intro first, an
    // outro last, the rest before the outro)
    if (req.addToForm)
    {
        auto full = formFor (style->id);

        for (auto& k : req.sections)
            if (! full.contains (k))
                full.insert (k == "intro" ? 0 : full.size() - (full[full.size() - 1] == "outro" && k != "outro" ? 1 : 0), k);

        req.sections = full;
        req.addToForm = false;
    }

    auto form = req.sections.isEmpty() ? formFor (style->id) : req.sections;
    const int defaultBars = req.bars > 0 ? req.bars : 8;

    // A half-time request, or a style that lives there (trap, drill, deathcore's breakdowns)
    Context c { req, *style, frame, rng };
    c.energy = req.energy;
    c.busy = req.busy;
    c.tempo = req.tempo > 0 ? req.tempo : (songIsBlank ? style->usual : song.bpm);

    juce::StringArray notes;
    std::optional<Grid> previousMain;
    auto& groove = result.groove;

    for (int i = 0; i < form.size(); ++i)
    {
        const auto kind = form[i];
        c.kind = kind;
        c.halfTime = req.halfTime || kind == "breakdown";
        c.energy = juce::jlimit (0.0f, 1.0f, req.energy + (kind == "chorus" || kind == "breakdown" || kind == "drop" ? 0.15f
                                                         : kind == "intro" || kind == "outro" ? -0.1f : 0.0f));
        const int bars = kind == "intro" || kind == "outro" ? std::min (4, defaultBars) : defaultBars;
        Grid kept (frame.steps);
        groove.sections.push_back (writeSection (c, sectionName (kind, style->id), bars, i == form.size() - 1,
                                                 i + 1 < form.size() ? form[i + 1] : juce::String(),
                                                 previousMain.has_value() ? &*previousMain : nullptr, kept, notes));
        previousMain = kept;
        groove.arrangement.add (groove.sections.back().name);
    }

    // The feel: how each style sits on the beat
    struct Feel { float swing; bool sixteenths; float loose; std::map<juce::String, float> push; };
    static const std::map<juce::String, Feel> feels {
        { "metalcore", { 0.0f, false, 2.5f, {} } },
        { "deathcore", { 0.0f, false, 2.5f, {} } },
        { "djent",     { 0.0f, false, 2.0f, {} } },
        { "thrash",    { 0.0f, false, 3.0f, { { "snare", -2.0f } } } },
        { "blast",     { 0.0f, false, 2.0f, {} } },
        { "metal",     { 0.0f, false, 3.0f, {} } },
        { "trap",      { 0.0f, false, 1.5f, {} } },
        { "drill",     { 0.0f, false, 2.0f, {} } },
        { "boombap",   { 0.58f, true, 8.0f, { { "snare", 12.0f }, { "hat_closed", 4.0f } } } },
        { "hiphop",    { 0.25f, true, 5.0f, { { "snare", 6.0f } } } },
        { "rock",      { 0.0f, false, 4.0f, { { "snare", 3.0f } } } },
        { "hardrock",  { 0.0f, false, 4.0f, { { "snare", 5.0f } } } },
        { "poppunk",   { 0.0f, false, 3.0f, { { "snare", -2.0f } } } },
        { "punk",      { 0.0f, false, 4.0f, { { "snare", -3.0f } } } },
        { "funk",      { 0.54f, true, 3.0f, { { "kick", -2.0f } } } },
        { "lofi",      { 0.6f, true, 12.0f, { { "snare", 18.0f }, { "sidestick", 18.0f }, { "kick", -5.0f }, { "hat_closed", 6.0f } } } },
        { "jazz",      { 0.0f, false, 7.0f, { { "ride", -3.0f }, { "snare", 5.0f } } } },
        { "ballad",    { 0.0f, false, 9.0f, {} } },
        { "neosoul",   { 0.55f, true, 16.0f, { { "snare", 25.0f }, { "sidestick", 22.0f }, { "kick", -10.0f }, { "hat_closed", 8.0f } } } },
    };

    if (auto it = feels.find (style->id); it != feels.end())
    {
        groove.swing = it->second.swing;
        groove.swingSixteenths = it->second.sixteenths;
        groove.looseMs = it->second.loose;
        groove.pushMs = it->second.push;
    }

    if (req.swing >= 0.0f)
        groove.swing = req.swing;

    // The kit: the style's, when it may choose
    groove.kit = mayPickKit ? kitFor (style->id, req.brushes, kitNames) : currentKit;

    if (groove.kit.isEmpty())
        groove.kit = currentKit;

    // The tempo: asked for, or the style's own in a song with nothing else in it yet
    if (req.tempo > 0)
        groove.tempo = req.tempo;
    else if (songIsBlank && (song.bpm < style->slowest || song.bpm > style->fastest || previous == nullptr))
        groove.tempo = style->usual;

    if (req.meterBeats > 0)
    {
        groove.meterBeats = req.meterBeats;
        groove.meterUnit = req.meterUnit;
    }

    const double tempo = groove.tempo > 0 ? groove.tempo : song.bpm;
    groove.title = juce::String (style->name) + ", " + juce::String (juce::roundToInt (tempo));

    // What it did, the way a drummer would say it
    juce::String reply = juce::String (style->name) + " at " + juce::String (juce::roundToInt (tempo)) + " bpm in "
                         + juce::String (beats) + "/" + juce::String (unit) + ". ";

    for (auto& n : notes)
        reply << n << " ";

    if (groove.tempo == 0 && (song.bpm < style->slowest || song.bpm > style->fastest))
        reply << "The song's at " << juce::roundToInt (song.bpm) << " bpm; " << style->name << " usually lives between " << style->slowest
              << " and " << style->fastest << ", so say \"" << style->usual << " bpm\" to move it. ";

    if (groove.kit.isNotEmpty() && mayPickKit)
        reply << "On the " << groove.kit << " kit.";

    groove.message = reply.trim();
    result.reply = groove.message;
    result.request = req;
    return result;
}

//==============================================================================
juce::var toJson (const Groove& g)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("title", g.title);
    o->setProperty ("message", g.message);
    o->setProperty ("kit", g.kit);
    o->setProperty ("tempo", g.tempo);
    o->setProperty ("meter", g.meterBeats > 0 ? juce::String (g.meterBeats) + "/" + juce::String (g.meterUnit) : juce::String());

    auto* feel = new juce::DynamicObject();
    feel->setProperty ("swing", g.swing);
    feel->setProperty ("swing_note", g.swingSixteenths ? "16th" : "8th");
    feel->setProperty ("looseness_ms", g.looseMs);
    juce::Array<juce::var> push;

    for (auto& [drum, ms] : g.pushMs)
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("drum", drum);
        p->setProperty ("ms", ms);
        push.add (juce::var (p));
    }

    feel->setProperty ("push", push);
    o->setProperty ("feel", juce::var (feel));

    juce::Array<juce::var> sections;

    for (auto& s : g.sections)
    {
        auto* section = new juce::DynamicObject();
        section->setProperty ("name", s.name);
        juce::Array<juce::var> bars;

        for (auto& b : s.bars)
        {
            auto* bar = new juce::DynamicObject();
            bar->setProperty ("steps", b.steps);
            juce::Array<juce::var> lanes;

            for (auto& l : b.lanes)
            {
                auto* lane = new juce::DynamicObject();
                lane->setProperty ("drum", l.voice);
                lane->setProperty ("hits", l.hits);
                lanes.add (juce::var (lane));
            }

            bar->setProperty ("lanes", lanes);
            bars.add (juce::var (bar));
        }

        section->setProperty ("bars", bars);
        sections.add (juce::var (section));
    }

    o->setProperty ("sections", sections);
    juce::Array<juce::var> arrangement;

    for (auto& a : g.arrangement)
        arrangement.add (a);

    o->setProperty ("arrangement", arrangement);
    return juce::var (o);
}

DrummerTurn turnFor (const juce::String& prompt, const Result& r)
{
    DrummerTurn turn;
    turn.prompt = prompt.trim();
    turn.reply = r.reply;
    turn.groove = juce::JSON::toString (toJson (r.groove), true);

    auto* answer = new juce::DynamicObject();
    answer->setProperty ("offline", true);
    answer->setProperty ("request", r.request.toVar());
    turn.answer = juce::JSON::toString (juce::var (answer), true);
    return turn;
}

std::optional<agents::Request> requestOf (const DrummerTurn& turn)
{
    const auto v = juce::JSON::parse (turn.answer);

    if (! (bool) v.getProperty ("offline", false))
        return {};

    return agents::Request::fromVar (v.getProperty ("request", {}));
}

} // namespace nightshift::drummer::offline
