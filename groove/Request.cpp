// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. Copied as it is from NightShift's src/ai for Nightshift Drums.

#include "Request.h"

namespace nightshift::agents
{

const std::vector<Style>& styles()
{
    // The words that mean each style, longest first wins ("death metal" over "metal", "pop punk"
    // over "punk"); the tempo it usually lives at, and how far either way
    static const std::vector<Style> list {
        // Metalcore & metal
        { "metalcore", "Metalcore",            "metal", 120, 150, 200, { "metalcore", "metal core", "post-hardcore", "post hardcore", "hardcore", "melodic hardcore" } },
        { "deathcore", "Deathcore",            "metal", 90,  120, 190, { "deathcore", "death core", "slam", "downtempo" } },
        { "djent",     "Djent",                "metal", 100, 125, 160, { "djent", "prog metal", "progressive metal", "polymetric", "polyrhythmic metal" } },
        { "thrash",    "Thrash",               "metal", 150, 185, 240, { "thrash", "speed metal", "crossover thrash" } },
        { "blast",     "Death and black metal", "metal", 160, 200, 260, { "death metal", "black metal", "grind", "grindcore" } },
        { "metal",     "Metal",                "metal", 100, 140, 200, { "metal", "heavy metal", "nu metal", "nu-metal", "groove metal" } },

        // Trap & hip-hop
        { "trap",      "Trap",                 "trap",  120, 140, 170, { "trap", "rage", "phonk", "808s", "808" } },
        { "drill",     "Drill",                "trap",  138, 142, 148, { "drill", "uk drill", "ny drill" } },
        { "boombap",   "Boom bap",             "trap",  82,  90,  100, { "boom bap", "boombap", "boom-bap", "90s hip hop", "old school hip hop" } },
        { "hiphop",    "Hip-hop",              "trap",  80,  95,  110, { "hip hop", "hip-hop", "hiphop", "rap" } },

        // Rock & pop-punk
        { "rock",      "Rock",                 "rock",  90,  120, 160, { "rock", "indie", "alternative", "alt rock", "grunge", "garage" } },
        { "hardrock",  "Hard rock",            "rock",  100, 130, 170, { "hard rock", "stoner", "arena rock", "classic rock" } },
        { "poppunk",   "Pop-punk",             "rock",  150, 180, 220, { "pop punk", "pop-punk", "poppunk", "easycore", "skate punk", "emo" } },
        { "punk",      "Punk",                 "rock",  160, 190, 240, { "punk", "d-beat", "dbeat", "hardcore punk" } },

        // Lofi, jazz & neo-soul
        { "lofi",      "Lofi",                 "lofi",  65,  80,  92,  { "lofi", "lo-fi", "lo fi", "chillhop", "chill hop", "study beat" } },
        { "jazz",      "Jazz",                 "lofi",  100, 150, 260, { "jazz", "bebop", "be-bop", "big band", "jazz trio", "swing jazz" } },
        { "ballad",    "Jazz ballad",          "lofi",  55,  70,  90,  { "ballad", "jazz ballad" } },
        { "neosoul",   "Neo-soul",             "lofi",  68,  84,  98,  { "neo soul", "neo-soul", "neosoul", "r&b", "rnb", "dilla", "soul" } },

        // Funk
        { "funk",      "Funk",                 "funk",  85,  102, 125, { "funk", "funky", "p funk", "p-funk", "funk rock", "disco funk", "disco" } },
    };

    return list;
}

const Style* findStyle (const juce::String& id)
{
    for (auto& s : styles())
        if (id == s.id)
            return &s;

    return nullptr;
}

//==============================================================================
juce::var Request::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("style", style);
    o->setProperty ("tempo", tempo);
    o->setProperty ("meter", juce::Array<juce::var> { meterBeats, meterUnit });
    o->setProperty ("halfTime", halfTime);
    o->setProperty ("doubleTime", doubleTime);
    o->setProperty ("brushes", brushes);
    o->setProperty ("blasts", blasts);
    o->setProperty ("ghostNotes", ghostNotes);
    o->setProperty ("gallops", gallops);
    o->setProperty ("energy", energy);
    o->setProperty ("busy", busy);
    o->setProperty ("busyHats", busyHats);
    o->setProperty ("busyKick", busyKick);
    o->setProperty ("swing", swing);
    o->setProperty ("bars", bars);
    o->setProperty ("keyRoot", keyRoot);
    o->setProperty ("mode", mode);
    o->setProperty ("lowest", lowest);
    o->setProperty ("slides", slides);
    o->setProperty ("walking", walking);
    o->setProperty ("octaves", octaves);
    o->setProperty ("slap", slap);
    o->setProperty ("pedal", pedal);
    o->setProperty ("lockKick", lockKick);
    o->setProperty ("legato", legato);

    juce::Array<juce::var> list;

    for (auto& s : sections)
        list.add (s);

    o->setProperty ("sections", list);
    return juce::var (o);
}

Request Request::fromVar (const juce::var& v)
{
    Request r;
    r.style = v.getProperty ("style", {}).toString();
    r.tempo = v.getProperty ("tempo", 0);

    if (auto* m = v.getProperty ("meter", {}).getArray(); m != nullptr && m->size() == 2)
    {
        r.meterBeats = (*m)[0];
        r.meterUnit = (*m)[1];
    }

    r.halfTime = v.getProperty ("halfTime", false);
    r.doubleTime = v.getProperty ("doubleTime", false);
    r.brushes = v.getProperty ("brushes", false);
    r.blasts = v.getProperty ("blasts", false);
    r.ghostNotes = v.getProperty ("ghostNotes", false);
    r.gallops = v.getProperty ("gallops", false);
    r.energy = v.getProperty ("energy", 0.5);
    r.busy = v.getProperty ("busy", 0.5);
    r.busyHats = v.getProperty ("busyHats", 0.0);
    r.busyKick = v.getProperty ("busyKick", 0.0);
    r.swing = v.getProperty ("swing", -1.0);
    r.bars = v.getProperty ("bars", 0);
    r.keyRoot = v.getProperty ("keyRoot", -1);
    r.mode = v.getProperty ("mode", {}).toString();
    r.lowest = v.getProperty ("lowest", 0);
    r.slides = v.getProperty ("slides", false);
    r.walking = v.getProperty ("walking", false);
    r.octaves = v.getProperty ("octaves", false);
    r.slap = v.getProperty ("slap", false);
    r.pedal = v.getProperty ("pedal", false);
    r.lockKick = v.getProperty ("lockKick", false);
    r.legato = v.getProperty ("legato", 0.0);

    if (auto* list = v.getProperty ("sections", {}).getArray())
        for (auto& s : *list)
            r.sections.add (s.toString());

    return r;
}

//==============================================================================
namespace
{
    // The words, lower case, spaced out so a whole word is " word "
    juce::String spaced (const juce::String& words)
    {
        juce::String s;

        for (auto c : words.toLowerCase())
            s << (juce::CharacterFunctions::isLetterOrDigit (c) || c == '/' || c == '&' || c == '-' || c == '#' ? juce::String::charToString (c) : juce::String (" "));

        return " " + s.replace ("-", " - ").replace ("  ", " ") + " ";
    }

    bool has (const juce::String& text, const juce::String& phrase)
    {
        return text.contains (" " + phrase.toLowerCase().replace ("-", " - ") + " ");
    }

    bool hasAny (const juce::String& text, std::initializer_list<const char*> phrases)
    {
        for (auto p : phrases)
            if (has (text, p))
                return true;

        return false;
    }
}

Request Request::parse (const juce::String& words, const Request* previous)
{
    const auto text = spaced (words);
    const auto tokens = juce::StringArray::fromTokens (text, " ", "");
    Request r;

    // The style: the longest of any style's words in it ("death metal" over "metal"), the first
    // named of two as long ("lofi jazz" is lofi)
    const Style* named = nullptr;
    int longest = 0, earliest = std::numeric_limits<int>::max();

    for (auto& s : styles())
        for (auto& w : s.words)
            if (const int at = text.indexOf (" " + w.toLowerCase().replace ("-", " - ") + " "); at >= 0)
                if (w.length() > longest || (w.length() == longest && at < earliest))
                {
                    named = &s;
                    longest = w.length();
                    earliest = at;
                }

    // Blast beats are a technique, not a style ("deathcore blast beats" is deathcore); alone,
    // with nothing playing yet, they mean death metal
    if (named == nullptr && (previous == nullptr || previous->style.isEmpty()) && hasAny (text, { "blast", "blasts", "blast beat", "blast beats" }))
        named = findStyle ("blast");

    // Naming no style, it's a change to what's there; else it starts afresh
    if (named == nullptr && previous != nullptr && previous->style.isNotEmpty())
    {
        r = *previous;
        r.understood.clear();
    }
    else if (named != nullptr)
    {
        r.style = named->id;
        r.understood.add (named->name);
    }

    // Tempo: "150 bpm", "150bpm", "at 150", "tempo 150"
    for (int i = 0; i < tokens.size(); ++i)
    {
        auto t = tokens[i];
        const bool bpmAfter = i + 1 < tokens.size() && tokens[i + 1].startsWith ("bpm");
        const bool bpmJoined = t.endsWith ("bpm") && t.dropLastCharacters (3).containsOnly ("0123456789") && t.length() > 3;
        const bool atBefore = i > 0 && (tokens[i - 1] == "at" || tokens[i - 1] == "tempo");

        if (bpmJoined)
            t = t.dropLastCharacters (3);

        if (t.containsOnly ("0123456789") && t.isNotEmpty() && (bpmAfter || bpmJoined || atBefore))
            if (const int bpm = t.getIntValue(); bpm >= 40 && bpm <= 300)
            {
                r.tempo = bpm;
                r.understood.add (juce::String (bpm) + " bpm");
            }
    }

    // Faster and slower, from whatever tempo it had
    if (const auto* s = findStyle (r.style); s != nullptr && r.tempo == 0 && hasAny (text, { "faster", "slower" }))
    {
        const int from = previous != nullptr && previous->tempo > 0 ? previous->tempo : s->usual;
        r.tempo = juce::roundToInt (from * (has (text, "faster") ? 1.1 : 0.9));
        r.understood.add (juce::String (r.tempo) + " bpm");
    }

    // A time signature: "7/8", "6/8"; and bars a section: "8 bars"
    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto& t = tokens[i];

        if (t.containsChar ('/'))
        {
            const int beats = t.upToFirstOccurrenceOf ("/", false, false).getIntValue();
            const int unit = t.fromFirstOccurrenceOf ("/", false, false).getIntValue();

            if (beats >= 2 && beats <= 15 && (unit == 2 || unit == 4 || unit == 8 || unit == 16))
            {
                r.meterBeats = beats;
                r.meterUnit = unit;
                r.understood.add (t);
            }
        }

        if (i + 1 < tokens.size() && t.containsOnly ("0123456789") && tokens[i + 1].startsWith ("bar"))
            r.bars = juce::jlimit (1, 32, t.getIntValue());
    }

    // The feel
    if (hasAny (text, { "half time", "half - time", "halftime" }))
    {
        r.halfTime = true;
        r.doubleTime = false;
        r.understood.add ("half-time");
    }

    if (hasAny (text, { "double time", "double - time", "doubletime" }))
    {
        r.doubleTime = true;
        r.halfTime = false;
        r.understood.add ("double-time");
    }

    if (hasAny (text, { "blast", "blasts", "blasting", "blast beat", "blast beats" }))
    {
        r.blasts = true;
        r.understood.add ("blast beats");
    }

    if (hasAny (text, { "brush", "brushes" }))
    {
        r.brushes = true;
        r.understood.add ("brushes");
    }

    if (hasAny (text, { "ghost", "ghosts", "ghost notes", "ghosted" }))
    {
        r.ghostNotes = true;
        r.understood.add ("ghost notes");
    }

    if (hasAny (text, { "gallop", "galloping", "gallops" }))
    {
        r.gallops = true;
        r.understood.add ("gallops");
    }

    if (hasAny (text, { "double kick", "double - kick", "double bass", "double pedal", "16th kick", "16ths on the kick" }))
    {
        r.busyKick = std::max (r.busyKick, 0.7f);
        r.understood.add ("double kick");
    }

    // How hard and how busy
    if (hasAny (text, { "heavy", "heavier", "hard", "harder", "aggressive", "brutal", "crushing", "huge", "intense", "angry", "big", "bigger", "massive" }))
    {
        r.energy = juce::jmin (1.0f, r.energy + 0.3f);
        r.understood.add ("heavier");
    }

    if (hasAny (text, { "chill", "soft", "softer", "gentle", "calm", "mellow", "laid back", "lazy", "relaxed", "quiet", "quieter", "smooth" }))
    {
        r.energy = juce::jmax (0.0f, r.energy - 0.3f);
        r.understood.add ("gentler");
    }

    const bool hats = hasAny (text, { "hats", "hat", "hihat", "hihats", "hi hat", "hi hats", "hi - hat", "hi - hats", "cymbals" });
    const bool kick = hasAny (text, { "kick", "kicks", "bass drum" });
    const bool more = hasAny (text, { "busy", "busier", "more", "technical", "complex", "crazy", "fast hands", "lots of" });
    const bool less = hasAny (text, { "simple", "simpler", "minimal", "sparse", "sparser", "less", "fewer", "basic", "stripped", "stripped back" });

    if (hats && (more || less))
    {
        r.busyHats = juce::jlimit (-1.0f, 1.0f, r.busyHats + (more ? 0.5f : -0.5f));
        r.understood.add (more ? "busier hats" : "simpler hats");
    }
    else if (kick && (more || less))
    {
        r.busyKick = juce::jlimit (-1.0f, 1.0f, r.busyKick + (more ? 0.5f : -0.5f));
        r.understood.add (more ? "more kick" : "less kick");
    }
    else if (more || less)
    {
        r.busy = juce::jlimit (0.0f, 1.0f, r.busy + (more ? 0.3f : -0.3f));
        r.understood.add (more ? "busier" : "simpler");
    }

    // Swing
    if (hasAny (text, { "straight", "no swing", "quantized", "on the grid" }))
    {
        r.swing = 0.0f;
        r.understood.add ("straight");
    }
    else if (hasAny (text, { "lazy swing", "heavy swing", "drunk", "drunken" }))
    {
        r.swing = 0.65f;
        r.understood.add ("lazy swing");
    }
    else if (hasAny (text, { "light swing", "a bit of swing", "slight swing" }))
    {
        r.swing = 0.3f;
        r.understood.add ("light swing");
    }
    else if (hasAny (text, { "swung", "shuffle", "swingy" }) || (has (text, "swing") && r.style != "jazz"))
    {
        r.swing = 0.5f;
        r.understood.add ("swing");
    }

    // The key: "in E minor", "key of F#", "in Bb", "C# minor", "drop C" (a tuning: its note is the lowest)
    {
        auto pitchClass = [] (const juce::String& t, const juce::String& after) -> int
        {
            if (t.isEmpty() || t.length() > 3 || ! juce::String ("abcdefg").containsChar (t[0]))
                return -1;

            static const int natural[] { 9, 11, 0, 2, 4, 5, 7 };    // a b c d e f g
            int pc = natural[t[0] - 'a'];
            auto rest = t.substring (1);

            if (rest.endsWithChar ('m'))
                rest = rest.dropLastCharacters (1);

            if (rest == "#" || rest == "s" || after == "sharp")      pc += 1;
            else if (rest == "b" || after == "flat")                pc -= 1;
            else if (rest.isNotEmpty())                             return -1;

            return (pc + 12) % 12;
        };

        auto quality = [] (const juce::String& t, const juce::String& next) -> juce::String
        {
            if (next == "minor" || next == "min" || next == "m" || (t.length() > 1 && t.endsWithChar ('m')))
                return "minor";

            if (next == "major" || next == "maj")
                return "major";

            return {};
        };

        static const char* const names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

        for (int i = 0; i + 1 < tokens.size(); ++i)
        {
            const auto& t = tokens[i];
            const auto next = tokens[i + 1], after = tokens[i + 2], further = tokens[i + 3];

            if (t == "drop" || (t == "tuned" && next == "to"))
            {
                const auto& note = t == "drop" ? next : after;
                const auto& tail = t == "drop" ? after : further;

                if (const int pc = pitchClass (note, tail); pc >= 0)
                {
                    r.keyRoot = pc;
                    r.lowest = pc <= 4 ? 24 + pc : 12 + pc;
                    r.understood.add ((t == "drop" ? "drop " : "tuned to ") + juce::String (names[pc]));
                }

                continue;
            }

            if (t != "in" && t != "key")
                continue;

            const auto& note = t == "key" && next == "of" ? after : next;
            const auto& following = t == "key" && next == "of" ? further : after;
            const int pc = pitchClass (note, following);
            const auto q = quality (note, following);

            // "in a" is only a key when it says so ("in a minor", "in A#"), not "in a lazy way"
            if (pc < 0 || (note.length() == 1 && q.isEmpty() && following != "sharp" && following != "flat"
                           && ! (following.isEmpty() || following == "and" || following == "with" || following == "at")))
                continue;

            r.keyRoot = pc;

            if (q.isNotEmpty())
                r.mode = q;

            r.understood.add ("in " + juce::String (names[pc]) + (q.isNotEmpty() ? " " + q : juce::String()));
        }

        // A note name with its quality anywhere ("C# minor", "Bbm"; not "am", which is a word): the key, if none was named
        if (r.keyRoot < 0)
            for (int i = 0; i + 1 < tokens.size(); ++i)
                if (const auto q = quality (tokens[i], tokens[i + 1]); q.isNotEmpty() && (tokens[i].length() >= 3 || tokens[i + 1] == "minor" || tokens[i + 1] == "major"))
                    if (const int pc = pitchClass (tokens[i], tokens[i + 1]); pc >= 0)
                    {
                        r.keyRoot = pc;
                        r.mode = q;
                        r.understood.add (juce::String (names[pc]) + " " + q);
                        break;
                    }

        if (hasAny (text, { "standard tuning", "e standard" }))
        {
            r.keyRoot = r.keyRoot < 0 ? 4 : r.keyRoot;
            r.lowest = 28;
        }

        for (auto m : { "dorian", "phrygian", "mixolydian", "aeolian", "lydian" })
            if (has (text, m))
            {
                r.mode = juce::String (m) == "aeolian" ? juce::String ("minor") : juce::String (m);
                r.understood.add (r.mode);
            }
    }

    // How the bass is played
    if (hasAny (text, { "slide", "slides", "sliding", "slidey", "glide", "glides", "gliding" }))
    {
        r.slides = true;
        r.understood.add ("slides");
    }

    if (hasAny (text, { "walking", "walk", "walks", "walking bass" }))
    {
        r.walking = true;
        r.understood.add ("walking");
    }

    if (hasAny (text, { "octave", "octaves" }))
    {
        r.octaves = true;
        r.understood.add ("octaves");
    }

    if (hasAny (text, { "slap", "slapped", "slapping", "pop", "pops", "popping", "thumb" }))
    {
        r.slap = true;
        r.understood.add ("slap");
    }

    if (hasAny (text, { "pedal", "pedal tone", "drone", "one note", "just the root", "root notes", "roots only" })
        && ! hasAny (text, { "double pedal", "hat pedal", "hi hat pedal" }))
    {
        r.pedal = true;
        r.understood.add ("on the root");
    }

    if (hasAny (text, { "lock to the kick", "locked to the kick", "follow the kick", "follows the kick", "with the kick", "on the kick",
                        "lock with the kick", "tight with the kick", "lock it to the kick" }))
    {
        r.lockKick = true;
        r.understood.add ("locked to the kick");
    }

    if (hasAny (text, { "legato", "long notes", "sustained", "held", "let it ring", "ringing" }))
    {
        r.legato = juce::jmin (1.0f, r.legato + 0.6f);
        r.understood.add ("long notes");
    }
    else if (hasAny (text, { "staccato", "short notes", "choppy", "punchy", "palm muted", "muted", "short" }))
    {
        r.legato = juce::jmax (-1.0f, r.legato - 0.6f);
        r.understood.add ("short notes");
    }

    // The song's sections, in the order they're named ("add a breakdown" adds to what's there)
    // (StringArrays: an initializer_list in a static table would dangle once it's built)
    static const std::pair<const char*, juce::StringArray> sectionWords[] {
        { "intro",     { "intro", "introduction" } },
        { "verse",     { "verse", "verses" } },
        { "prechorus", { "pre chorus", "pre - chorus", "prechorus", "buildup", "build up", "build - up" } },
        { "chorus",    { "chorus", "choruses", "refrain" } },
        { "breakdown", { "breakdown", "breakdowns", "break down" } },
        { "bridge",    { "bridge", "middle eight" } },
        { "drop",      { "drop", "drops" } },
        { "hook",      { "hook", "hooks" } },
        { "outro",     { "outro", "ending" } },
    };

    std::vector<std::pair<int, juce::String>> found;

    // Where a kind is first named ("drop C", "drop D#" is a tuning, not a drop)
    auto firstNamed = [&text] (const juce::String& kind, const juce::StringArray& ws)
    {
        for (auto& w : ws)
            for (int at = text.indexOf (" " + w + " "); at >= 0; at = text.indexOf (at + 1, " " + w + " "))
            {
                if (kind == "drop")
                {
                    const auto next = text.substring (at + w.length() + 2).upToFirstOccurrenceOf (" ", false, false);

                    if (next.isNotEmpty() && next.length() <= 2 && juce::String ("abcdefg").containsChar (next[0])
                         && (next.length() == 1 || next[1] == '#' || next[1] == 'b'))
                        continue;
                }

                return at;
            }

        return -1;
    };

    for (auto& [kind, ws] : sectionWords)
        if (const int at = firstNamed (kind, ws); at >= 0)
            found.push_back ({ at, kind });

    std::sort (found.begin(), found.end());
    juce::StringArray asked;

    for (auto& [at, kind] : found)
        asked.addIfNotAlreadyThere (kind);

    if (! asked.isEmpty())
    {
        if (hasAny (text, { "add", "plus", "with", "and" }) && previous != nullptr && ! r.sections.isEmpty() && named == nullptr)
        {
            for (auto& k : asked)
                r.sections.addIfNotAlreadyThere (k);
        }
        else if (named != nullptr || r.sections.isEmpty())
        {
            r.sections = asked;

            // "With a breakdown", "add a bridge": the usual song plus these. A list ("intro, verse
            // and chorus") is the song as asked.
            auto ledIn = [&text] (int at, bool first)
            {
                auto before = text.substring (0, at).trimEnd();

                for (auto article : { " a", " an", " some", " the", " two", " 2" })
                    if (before.endsWith (article))
                        before = before.dropLastCharacters ((int) std::strlen (article)).trimEnd();

                for (auto w : { " with", " add", " plus", " including", " and" })
                    if (before.endsWith (w) && (! first || juce::String (w) != " and"))
                        return true;

                return false;
            };

            r.addToForm = ! found.empty();

            for (size_t i = 0; i < found.size(); ++i)
                r.addToForm = r.addToForm && ledIn (found[i].first, i == 0);
        }

        r.understood.add (asked.joinIntoString (", "));
    }

    return r;
}

} // namespace nightshift::agents
