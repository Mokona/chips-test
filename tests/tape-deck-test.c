//------------------------------------------------------------------------------
//  tape-deck-test.c
//------------------------------------------------------------------------------
#define CHIPS_IMPL
// small capacity to test full tapes
#define TAPE_DECK_MAX_PULSES (64)
#include "chips/tape_deck.h"
#include "utest.h"

#define T(b) ASSERT_TRUE(b)

static tape_deck_t deck;

static void init(bool auto_rewind) {
    tape_deck_init(&deck, &(tape_deck_desc_t){ .tick_hz = 1000, .auto_rewind = auto_rewind });
}

// play num ticks with the motor on, return the levels read as a string of 'L' and 'H'
static const char* play(int num) {
    static char levels[256];
    for (int i = 0; i < num; i += 1) {
        const uint64_t pins = tape_deck_tick(&deck, TAPE_DECK_MOTOR);
        levels[i] = (pins & TAPE_DECK_READ) ? 'H' : 'L';
    }
    levels[num] = 0;
    return levels;
}

// play as many ticks as expected levels
// Warning: ASSERT_STREQ evaluates its arguments several times
#define ASSERT_PLAYS(expected) { const char* levels = play((int)strlen(expected)); ASSERT_STREQ(expected, levels); }

// record a string of 'L' and 'H', one tick per level
static void record(const char* levels) {
    for (; *levels; levels += 1) {
        const uint64_t write = (*levels == 'H') ? TAPE_DECK_WRITE : 0;
        tape_deck_tick(&deck, TAPE_DECK_MOTOR | TAPE_DECK_REC | write);
    }
}

static void stop_motor(void) {
    tape_deck_tick(&deck, 0);
}

// return the pulses on tape as a string of level and duration, for instance "L3 H2"
static const char* pulses(void) {
    static char str[1024];
    size_t len = 0;
    str[0] = 0;
    for (uint32_t i = 0; i < deck.num_pulses; i += 1) {
        len += snprintf(&str[len], sizeof(str) - len, "%s%c%u", (i > 0) ? " " : "",
            tape_deck_pulse_level(&deck, i) ? 'H' : 'L', tape_deck_pulse_ticks(&deck, i));
    }
    return str;
}

static uint64_t total_ticks(void) {
    uint64_t ticks = 0;
    for (uint32_t i = 0; i < deck.num_pulses; i += 1) {
        ticks += tape_deck_pulse_ticks(&deck, i);
    }
    return ticks;
}

UTEST(tape_deck, blank_tape_is_silent) {
    init(false);
    ASSERT_PLAYS("LLLLLLLL");
    T(deck.pos == 0);
    T(!deck.moved);
}

UTEST(tape_deck, play) {
    init(false);
    T(tape_deck_add_pulse(&deck, false, 3));
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 4));
    ASSERT_PLAYS("LLLHHLLLL");
    T(deck.pos == 3);
    // after the end of the content
    ASSERT_PLAYS("LLL");
}

UTEST(tape_deck, motor_off_stops_the_tape) {
    init(false);
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 2));
    ASSERT_PLAYS("H");
    for (int i = 0; i < 10; i += 1) {
        T(0 == (tape_deck_tick(&deck, 0) & TAPE_DECK_READ));
    }
    ASSERT_PLAYS("HLL");
}

UTEST(tape_deck, auto_rewind) {
    init(true);
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 2));
    ASSERT_PLAYS("HHL");
    stop_motor();
    T(deck.pos == 0 && deck.pulse_ticks == 0);
    ASSERT_PLAYS("HHL");
}

UTEST(tape_deck, no_auto_rewind) {
    init(false);
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 2));
    ASSERT_PLAYS("HHL");
    stop_motor();
    ASSERT_PLAYS("L");
    T(deck.pos == 2);
}

UTEST(tape_deck, long_pulses_are_split) {
    init(false);
    T(tape_deck_add_pulse(&deck, false, (uint64_t)TAPE_DECK_PULSE_MAX_TICKS + 6));
    T(deck.num_pulses == 2);
    T(tape_deck_pulse_ticks(&deck, 0) == TAPE_DECK_PULSE_MAX_TICKS);
    T(tape_deck_pulse_ticks(&deck, 1) == 6);
    T(!tape_deck_pulse_level(&deck, 0) && !tape_deck_pulse_level(&deck, 1));
}

UTEST(tape_deck, full_tape) {
    init(false);
    for (int i = 0; i < TAPE_DECK_MAX_PULSES; i += 1) {
        T(tape_deck_add_pulse(&deck, i & 1, 1));
    }
    T(!tape_deck_add_pulse(&deck, false, 1));
    T(deck.num_pulses == TAPE_DECK_MAX_PULSES);
}

UTEST(tape_deck, eject) {
    init(false);
    T(tape_deck_add_pulse(&deck, true, 5));
    ASSERT_PLAYS("HH");
    tape_deck_eject(&deck);
    T(deck.num_pulses == 0 && deck.pos == 0);
    ASSERT_PLAYS("LL");
}

UTEST(tape_deck, record_on_blank_tape) {
    init(false);
    record("LLLHHLLLLH");
    T(tape_deck_is_recording(&deck));
    stop_motor();
    T(!tape_deck_is_recording(&deck));
    ASSERT_STREQ("L3 H2 L4 H1", pulses());
    T(deck.pos == 4);
    tape_deck_rewind(&deck);
    ASSERT_PLAYS("LLLHHLLLLH");
}

UTEST(tape_deck, record_appends_after_the_end) {
    init(false);
    T(tape_deck_add_pulse(&deck, true, 2));
    ASSERT_PLAYS("HHL");
    record("LLH");
    stop_motor();
    ASSERT_STREQ("H2 L2 H1", pulses());
}

UTEST(tape_deck, record_overwrites_in_the_middle) {
    init(false);
    T(tape_deck_add_pulse(&deck, false, 10));
    T(tape_deck_add_pulse(&deck, true, 10));
    T(tape_deck_add_pulse(&deck, false, 10));
    T(tape_deck_add_pulse(&deck, true, 10));
    // head in the middle of the second pulse
    play(14);
    T(deck.pos == 1 && deck.pulse_ticks == 4);
    // overwrite ticks 14 to 21
    record("HHHLLLLL");
    stop_motor();
    ASSERT_STREQ("L10 H4 H3 L5 L8 H10", pulses());
    T(total_ticks() == 40);
    // the head is after the recorded part
    T(deck.pos == 4 && deck.pulse_ticks == 0);
    ASSERT_PLAYS("LLLLLLLLHH");
}

UTEST(tape_deck, record_overwrites_several_pulses) {
    init(false);
    for (int i = 0; i < 6; i += 1) {
        T(tape_deck_add_pulse(&deck, i & 1, 2));
    }
    // overwrite from the start of the second pulse to the middle of the fifth
    play(2);
    record("HHHHHHH");
    stop_motor();
    ASSERT_STREQ("L2 H7 L1 H2", pulses());
    T(total_ticks() == 12);
}

UTEST(tape_deck, record_past_the_end_of_the_content) {
    init(false);
    T(tape_deck_add_pulse(&deck, false, 2));
    T(tape_deck_add_pulse(&deck, true, 2));
    play(1);
    record("HHHHHHH");
    stop_motor();
    ASSERT_STREQ("L1 H7", pulses());
}

UTEST(tape_deck, recording_stops_with_rec_pin) {
    init(false);
    record("HHLL");
    T(tape_deck_is_recording(&deck));
    // motor still running, no more record mode: plays the rest of the tape
    T(0 == (tape_deck_tick(&deck, TAPE_DECK_MOTOR) & TAPE_DECK_READ));
    T(!tape_deck_is_recording(&deck));
    ASSERT_STREQ("H2 L2", pulses());
}

UTEST(tape_deck, recording_on_full_tape_overflows) {
    init(false);
    // a long pulse followed by short ones, leaving room for a single new pulse
    T(tape_deck_add_pulse(&deck, false, 100));
    for (int i = 1; i < TAPE_DECK_MAX_PULSES - 1; i += 1) {
        T(tape_deck_add_pulse(&deck, i & 1, 1));
    }
    // the recorded signal is denser than the long pulse it overwrites
    record("HLHLHLHLHL");
    stop_motor();
    T(deck.rec.overflow);
    T(deck.num_pulses == TAPE_DECK_MAX_PULSES);
    // the content after the recording is kept
    T(!tape_deck_pulse_level(&deck, 1) && (tape_deck_pulse_ticks(&deck, 1) == 90));
}

UTEST(tape_deck, seek_and_find_silence) {
    init(false);
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 2));
    T(tape_deck_add_pulse(&deck, false, 100));
    T(tape_deck_add_pulse(&deck, true, 2));
    T(tape_deck_add_pulse(&deck, false, 100));
    T(tape_deck_find_silence(&deck, 0, 50) == 2);
    T(tape_deck_find_silence(&deck, 3, 50) == 4);
    T(tape_deck_find_silence(&deck, 5, 50) == 5);
    T(tape_deck_find_silence(&deck, 0, 1000) == deck.num_pulses);
    tape_deck_seek(&deck, 3);
    ASSERT_PLAYS("HHL");
    tape_deck_seek(&deck, 1000);
    T(deck.pos == deck.num_pulses);
}
