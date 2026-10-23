//------------------------------------------------------------------------------
//  vg5000-tape-test.c
//------------------------------------------------------------------------------

#define CHIPS_IMPL
#include "chips/chips_common.h"
#include "chips/z80.h"
#include "chips/mem.h"
#include "chips/ef9345.h"
#include "chips/beeper.h"
#include "chips/clk.h"
#include "chips/kbd.h"
#include "chips/tape_deck.h"
#include "systems/vg5000.h"
#include "vg5000-roms.h"
#include "keybuf.h"
#include "utest.h"

#define T(b) ASSERT_TRUE(b)

// BASIC program first instruction at boot
#define TXTTAB (0x49FC)
// loaded machine code binary location
#define BINARY_ADDR (TXTTAB + 16)

// k7 file layout
#define K7_HEADER_SIZE (32)
#define K7_LENGTH_OFFSET (28)
#define K7_DATA_OFFSET (K7_HEADER_SIZE + 10)
#define K7_MAX_SIZE (4096)

// tape signal half-periods in CPU ticks
#define TAPE_SHORT (833)
#define TAPE_LONG (1666)
#define TAPE_MAX_PULSES (512 * 1024)

#define FRAME_US (16667)

// frames between two keys fed from the key buffer, as in the emulator app
#define KEY_DELAY_FRAMES (5)

static vg5000_t sys;
static uint8_t k7[K7_MAX_SIZE];
static uint16_t pulses[TAPE_MAX_PULSES];

// run frames, feeding keys from the key buffer like the emulator app does
static void advance_frames(int num) {
    for (int i = 0; i < num; i++) {
        vg5000_exec(&sys, FRAME_US);
        const uint8_t key_code = keybuf_get(FRAME_US);
        if (key_code != 0) {
            vg5000_key_down(&sys, key_code);
            vg5000_key_up(&sys, key_code);
        }
    }
}

// returns once all keys have been fed
// WARNING: replaces any pending input
static void type_on_keyboard(const char* str) {
    keybuf_put(str);
    advance_frames((int)(strlen(str) + 1) * (KEY_DELAY_FRAMES + 1));
}

static bool tape_motor_on(void) {
    return tape_deck_is_motor_on(&sys.tape_deck);
}

// run until the tape motor has been switched on then off again,
static int wait_tape_operation(int max_seconds) {
    int motor_frames = 0;
    for (int i = 0; i < max_seconds * 60; i++) {
        advance_frames(1);
        if (tape_motor_on()) {
            motor_frames++;
        }
        else if (motor_frames > 0) {
            break;
        }
    }
    // return number of frames with motor on
    return motor_frames;
}

static void boot(void) {
    keybuf_init(&(keybuf_desc_t){ .key_delay_frames = KEY_DELAY_FRAMES });
    vg5000_init(&sys, &(vg5000_desc_t){
        .type = VG5000_TYPE_11,
        .audio = { .sample_rate = 44100 },
        .roms = {
            .vg5000_10 = { .ptr = dump_vg5000_rom_10, .size = sizeof(dump_vg5000_rom_10) },
            .vg5000_11 = { .ptr = dump_vg5000_rom_11, .size = sizeof(dump_vg5000_rom_11) },
            .ef9345_charset = { .ptr = dump_vg5000_charset_rom, .size = sizeof(dump_vg5000_charset_rom) },
        },
    });
    advance_frames(120);
}

// build a machine code k7
// '10 CALL 18956' BASIC
// then trampoline, padding, and a binary with all byte values
static size_t make_k7(void) {
    memset(k7, 0, sizeof(k7));

    // header sync
    size_t pos = 0;
    for (int i = 0; i < 10; i++) {
        k7[pos++] = 0xD3;
    }

    k7[pos++] = 'M';
    memcpy(&k7[pos], "TEST  ", 6); pos += 6;
    k7[pos++] = 0;              // version
    memcpy(&k7[pos], "10", 2); pos += 5;
    k7[pos++] = 0;              // protection
    pos += 2;                   // checksum workspace

    k7[pos++] = TXTTAB & 0xFF;
    k7[pos++] = TXTTAB >> 8;

    const size_t length_pos = pos; pos += 2;
    const size_t checksum_pos = pos; pos += 2;

    // payload sync
    for (int i = 0; i < 10; i++) {
        k7[pos++] = 0xD6;
    }

    const size_t data_start = pos;

    // trampoline: next line pointer, line number, CALL token, parameter
    char call_param[8];
    snprintf(call_param, sizeof(call_param), " %d", BINARY_ADDR);
    const size_t line_size = 5 + strlen(call_param) + 1;
    k7[pos++] = (TXTTAB + line_size) & 0xFF;
    k7[pos++] = (TXTTAB + line_size) >> 8;
    k7[pos++] = 10;
    k7[pos++] = 0;
    k7[pos++] = 0x9F;
    memcpy(&k7[pos], call_param, strlen(call_param) + 1); pos += strlen(call_param) + 1;

    pos += 2;   // end of program
    pos = data_start + (BINARY_ADDR - TXTTAB);

    // all byte values in scrambled order
    // second half of bytes shifted by one
    for (int i = 0; i < 512; i++) {
        k7[pos++] = (uint8_t)(i * 7 + (i >> 8));
    }

    const size_t length = pos - data_start;
    uint16_t checksum = 0;
    for (size_t i = data_start; i < pos; i++) {
        checksum += k7[i];
    }
    k7[length_pos] = length & 0xFF;
    k7[length_pos + 1] = length >> 8;
    k7[checksum_pos] = checksum & 0xFF;
    k7[checksum_pos + 1] = checksum >> 8;

    pos += 10;  // trailing zeros

    // return the k7 size
    return pos;
}

static size_t put_impulse(size_t pos, uint16_t half_period) {
    pulses[pos++] = half_period;
    pulses[pos++] = half_period;
    return pos;
}

static size_t put_end_of_byte(size_t pos) {
    for (int i = 0; i < 4; i++) {
        pos = put_impulse(pos, TAPE_SHORT);
    }
    return put_impulse(pos, TAPE_LONG);
}

static size_t put_byte(size_t pos, uint8_t byte) {
    for (int bit = 0; bit < 8; bit++, byte >>= 1) {
        if (byte & 1) {
            pos = put_impulse(pos, TAPE_SHORT);
            pos = put_impulse(pos, TAPE_SHORT);
        }
        else {
            pos = put_impulse(pos, TAPE_LONG);
        }
    }
    return put_end_of_byte(pos);
}

static size_t put_synchro(size_t pos, int num_impulses) {
    for (int i = 0; i < num_impulses; i++) {
        pos = put_impulse(pos, TAPE_SHORT);
    }
    return put_end_of_byte(pos);
}

// reference k7 to tape signal encoder
// duplicated code at the moment (with different naming)
// TODO: merge when k7 handling moves to chips
static size_t k7_to_pulses(size_t k7_size) {
    size_t pos = 0;
    pulses[pos++] = 17400;  // starts with a silence

    pos = put_synchro(pos, 30000);
    for (size_t i = 0; i < K7_HEADER_SIZE; i++) {
        pos = put_byte(pos, k7[i]);
    }

    pos = put_synchro(pos, 7200);
    for (size_t i = K7_HEADER_SIZE; i < k7_size; i++) {
        pos = put_byte(pos, k7[i]);
    }

    // return number of pulses
    return pos;
}

UTEST(vg5000_tape, cload_k7) {
    boot();

    const size_t k7_size = make_k7();
    const size_t num_pulses = k7_to_pulses(k7_size);

    T(num_pulses < TAPE_MAX_PULSES);
    T(vg5000_insert_tape(&sys, (chips_range_t){ .ptr = pulses, .size = num_pulses * sizeof(uint16_t) }));

    type_on_keyboard("CLOAD\n");

    T(wait_tape_operation(120) > 0);
    T(!tape_motor_on());
    const uint16_t length = k7[K7_LENGTH_OFFSET] | (k7[K7_LENGTH_OFFSET + 1] << 8);
    for (uint16_t i = 0; i < length; i++) {
        ASSERT_EQ(k7[K7_DATA_OFFSET + i], mem_rd(&sys.mem, TXTTAB + i));
    }
}

UTEST(vg5000_tape, csave_cload_roundtrip) {
    boot();

    type_on_keyboard("10 PRINT \"HELLO\"\n20 GOTO 10\n");

    uint8_t program[64];
    for (int i = 0; i < 64; i++) {
        program[i] = mem_rd(&sys.mem, TXTTAB + i);
    }

    vg5000_tape_rec(&sys, true);
    type_on_keyboard("CSAVE\"RT\"\n");
    T(wait_tape_operation(120) > 0);
    vg5000_tape_rec(&sys, false);

    type_on_keyboard("NEW\n");
    T(mem_rd(&sys.mem, TXTTAB) == 0 && mem_rd(&sys.mem, TXTTAB + 1) == 0);

    type_on_keyboard("CLOAD\n");
    T(wait_tape_operation(120) > 0);

    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(program[i], mem_rd(&sys.mem, TXTTAB + i));
    }
}

UTEST(vg5000_tape, csave_without_rec_does_not_record) {
    boot();

    type_on_keyboard("10 PRINT \"HELLO\"\n");
    type_on_keyboard("CSAVE\"RT\"\n");
    T(wait_tape_operation(120) > 0);

    T(sys.tape_deck.num_pulses == 0);
}

UTEST_MAIN()
