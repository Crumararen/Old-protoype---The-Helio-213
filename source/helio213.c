// helio213.c — Extremely basic Helio 213 emulator + SDL window
// Build: gcc -O2 -o helio213 helio213.c $(sdl2-config --cflags --libs)
// Usage: ./helio213 --- Currently loads from ROM and runs until HALT
//                       or ERROR. Exit window with ESCAPE key to end.

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

/* ---- PMEM Definitions ---- */
#define ROM_FILE     "memory/rom.bin"
#define PMEM1_FILE   "memory/pmem1.bin"
#define PMEM2_FILE   "memory/pmem2.bin"
#define EXTPMEM_FILE "memory/extpmem.bin"
#define SAVE_INTERVAL_MS 1000

/* ---- Memory map ---- */
#define ROM_BASE     0x00000
#define ROM_END      0x00FFF
#define STRAM_BASE   0x01000
#define STRAM_END    0x017FE
#define SP_ADDR      0x017FF
#define RNG_ADDR     0x01800
#define KEYMAP_BASE  0x01804
#define KEYMAP_END   0x01813
#define CLOCK_ADDR   0x01814
#define SYSCTRL_ADDR 0x01819
#define AUDIO_BASE   0x0181A
#define AUDIO_END    0x0181F
#define RAM8_BASE    0x01820
#define RAM8_END     0x0581F
#define RAM16_BASE   0x05820
#define RAM16_END    0x0591F
#define CHRBUF_BASE  0x05920
#define CHRBUF_END   0x0627F
#define PAL_BASE     0x06280
#define PAL_END      0x062BF
#define FONT_BASE    0x062C0
#define FONT_END     0x072BF
#define PMEM1_BASE   0x072C0
#define PMEM1_END    0x0F2BF
#define PMEM2_BASE   0x0F2C0
#define PMEM2_END    0x172BF
#define EXTPMEM_BASE 0x172C0
#define EXTPMEM_END  0x1F2BF

/* ---- Screen ---- */
#define SCR_COLS 60
#define SCR_ROWS 40
#define CHAR_W   8
#define CHAR_H   8
#define UPSCALE  2
#define WIN_W    (SCR_COLS * CHAR_W * UPSCALE)
#define WIN_H    (SCR_ROWS * CHAR_H * UPSCALE)

/* ---- Storage ---- */
static uint8_t  rom[ROM_END - ROM_BASE + 1];
static uint16_t stram[STRAM_END - STRAM_BASE + 1];
static uint8_t  ram8[RAM8_END - RAM8_BASE + 1];
static uint16_t ram16[RAM16_END - RAM16_BASE + 1];
static uint16_t charbuf[CHRBUF_END - CHRBUF_BASE + 1];
static uint8_t  palbuf[PAL_END - PAL_BASE + 1];
static uint8_t  fontbuf[FONT_END - FONT_BASE + 1];
static uint8_t  pmem1[PMEM1_END - PMEM1_BASE + 1];
static uint8_t  pmem2[PMEM2_END - PMEM2_BASE + 1];
static uint8_t  extpmem[EXTPMEM_END - EXTPMEM_BASE + 1];

/* ---- Hardware ---- */
static uint8_t  keymap[16] = {0};      // 128 bits for keyboard matrix
static uint32_t random_cell = 0;       // 32-bit RNG cell
static uint32_t system_clock = 0;      // Centisecond counter
static uint32_t last_clock_tick = 0;   // For tracking elapsed time

static struct {
    uint32_t r[16];
    uint32_t pc;
    uint16_t sp;
    int halted;
    int error;
    const char *errmsg;
} cpu;

/* ---- Memory access ---- */
static uint32_t mem_read(uint32_t addr) {
    if (addr <= ROM_END)      return rom[addr];
    if (addr <= STRAM_END)    return stram[addr - STRAM_BASE];
    if (addr == SP_ADDR)      return cpu.sp;
    if (addr >= RNG_ADDR && addr <= RNG_ADDR + 3) {
        return (random_cell >> ((addr - RNG_ADDR) * 8)) & 0xFF;
    }
    if (addr >= CLOCK_ADDR && addr <= CLOCK_ADDR + 3) {
        return (system_clock >> ((addr - CLOCK_ADDR) * 8)) & 0xFF;
    }
    if (addr >= KEYMAP_BASE && addr <= KEYMAP_END) {
        return keymap[addr - KEYMAP_BASE];
    }
    // (We'll add System Control and Audio later)
    if (addr <= RAM8_END)     return ram8[addr - RAM8_BASE];
    if (addr <= RAM16_END)    return ram16[addr - RAM16_BASE];
    if (addr <= CHRBUF_END)   { cpu.error=1; cpu.errmsg="READ from char buffer"; return 0; }
    if (addr <= PAL_END)      return palbuf[addr - PAL_BASE];
    if (addr <= FONT_END)     return fontbuf[addr - FONT_BASE];
    if (addr <= PMEM1_END)    return pmem1[addr - PMEM1_BASE];
    if (addr <= PMEM2_END)    return pmem2[addr - PMEM2_BASE];
    if (addr <= EXTPMEM_END)  return extpmem[addr - EXTPMEM_BASE];
    cpu.error=1; cpu.errmsg="READ out of range"; return 0;
}

static void mem_write(uint32_t addr, uint32_t v) {
    if (addr <= ROM_END)      { cpu.error=1; cpu.errmsg="WRITE to ROM"; return; }
    if (addr <= STRAM_END)    { stram[addr - STRAM_BASE] = (uint16_t)v; return; }
        if (addr == SP_ADDR)      { cpu.sp = v & 0x7FF; return; }
    if (addr >= RNG_ADDR && addr <= RNG_ADDR + 3) {
        uint32_t shift = (addr - RNG_ADDR) * 8;
        random_cell = (random_cell & ~(0xFF << shift)) | ((v & 0xFF) << shift);
        return;
    } if (addr >= CLOCK_ADDR && addr <= CLOCK_ADDR + 3) {
        uint32_t shift = (addr - CLOCK_ADDR) * 8;
        system_clock = (system_clock & ~(0xFF << shift)) | ((v & 0xFF) << shift);
        return;
    } if (addr >= KEYMAP_BASE && addr <= KEYMAP_END) {
        keymap[addr - KEYMAP_BASE] = v & 0xFF;
        return;
    } if (addr <= RAM8_END)   { ram8[addr - RAM8_BASE] = (uint8_t)v; return; }
    if (addr <= RAM16_END)    { ram16[addr - RAM16_BASE] = v; return; }
    if (addr <= CHRBUF_END)   { cpu.error=1; cpu.errmsg="WRITE to char buffer"; return; }
    if (addr <= PAL_END)      { palbuf[addr - PAL_BASE] = (uint8_t)v; return; }
    if (addr <= FONT_END)     { fontbuf[addr - FONT_BASE] = (uint8_t)v; return; }
    if (addr <= PMEM1_END)    { pmem1[addr - PMEM1_BASE] = (uint8_t)v; return; }
    if (addr <= PMEM2_END)    { pmem2[addr - PMEM2_BASE] = (uint8_t)v; return; }
    if (addr <= EXTPMEM_END)  { extpmem[addr - EXTPMEM_BASE] = (uint8_t)v; return; }
    cpu.error=1; cpu.errmsg="WRITE out of range";
}

/* ---- Stack ---- */
static void push16(uint16_t v) {
    if (cpu.sp == 0) { cpu.error=1; cpu.errmsg="STACK OVERFLOW"; return; }
    stram[cpu.sp] = v;
    cpu.sp--;
}
static uint16_t pop16(void) {
    if (cpu.sp >= 0x7FE) { cpu.error=1; cpu.errmsg="STACK UNDERFLOW"; return 0; }
    cpu.sp++;
    return stram[cpu.sp];
}

/* ---- CPU ---- */
static uint16_t fetch16(void) {
    uint32_t a = cpu.pc;
    uint16_t w;

    /* 8-bit-cell regions: instruction = 2 cells, PC += 2 */
    if (a <= ROM_END) {
        if (a + 1 > ROM_END) { cpu.error=1; cpu.errmsg="FETCH past ROM"; return 0; }
        w = rom[a] | (rom[a+1] << 8);
        cpu.pc += 2;
        return w;
    }
    if (a >= RAM8_BASE && a <= RAM8_END) {
        if (a + 1 > RAM8_END) { cpu.error=1; cpu.errmsg="FETCH past RAM8"; return 0; }
        w = ram8[a - RAM8_BASE] | (ram8[a+1 - RAM8_BASE] << 8);
        cpu.pc += 2;
        return w;
    }
    if (a >= PMEM1_BASE && a <= PMEM1_END) {
        if (a + 1 > PMEM1_END) { cpu.error=1; cpu.errmsg="FETCH past PMEM1"; return 0; }
        w = pmem1[a - PMEM1_BASE] | (pmem1[a+1 - PMEM1_BASE] << 8);
        cpu.pc += 2;
        return w;
    }
    if (a >= PMEM2_BASE && a <= PMEM2_END) {
        if (a + 1 > PMEM2_END) { cpu.error=1; cpu.errmsg="FETCH past PMEM2"; return 0; }
        w = pmem2[a - PMEM2_BASE] | (pmem2[a+1 - PMEM2_BASE] << 8);
        cpu.pc += 2;
        return w;
    }
    if (a >= EXTPMEM_BASE && a <= EXTPMEM_END) {
        if (a + 1 > EXTPMEM_END) { cpu.error=1; cpu.errmsg="FETCH past EXTPMEM"; return 0; }
        w = extpmem[a - EXTPMEM_BASE] | (extpmem[a+1 - EXTPMEM_BASE] << 8);
        cpu.pc += 2;
        return w;
    }

    /* 16-bit-cell regions: instruction = 1 cell, PC += 1 */
    if (a >= STRAM_BASE && a <= STRAM_END) {
        w = stram[a - STRAM_BASE];
        cpu.pc += 1;
        return w;
    }
    if (a >= RAM16_BASE && a <= RAM16_END) {
        w = ram16[a - RAM16_BASE];
        cpu.pc += 1;
        return w;
    }

    cpu.error = 1;
    cpu.errmsg = "FETCH out of range";
    return 0;
}

/* ---- Keyboard map ---- */
static const struct { 
    SDL_Keycode sym; 
    int byte_idx; 
    int bit_idx; 
} key_mappings[] = {
    // ---- IDs 0-67: unchanged from your current map ----
    // Byte 0: A-H
    { SDLK_a, 0, 0 }, { SDLK_b, 0, 1 }, { SDLK_c, 0, 2 }, { SDLK_d, 0, 3 },
    { SDLK_e, 0, 4 }, { SDLK_f, 0, 5 }, { SDLK_g, 0, 6 }, { SDLK_h, 0, 7 },
    // Byte 1: I-P
    { SDLK_i, 1, 0 }, { SDLK_j, 1, 1 }, { SDLK_k, 1, 2 }, { SDLK_l, 1, 3 },
    { SDLK_m, 1, 4 }, { SDLK_n, 1, 5 }, { SDLK_o, 1, 6 }, { SDLK_p, 1, 7 },
    // Byte 2: Q-X
    { SDLK_q, 2, 0 }, { SDLK_r, 2, 1 }, { SDLK_s, 2, 2 }, { SDLK_t, 2, 3 },
    { SDLK_u, 2, 4 }, { SDLK_v, 2, 5 }, { SDLK_w, 2, 6 }, { SDLK_x, 2, 7 },
    // Byte 3: Y, Z, 1-6
    { SDLK_y, 3, 0 }, { SDLK_z, 3, 1 }, 
    { SDLK_1, 3, 2 }, { SDLK_2, 3, 3 }, { SDLK_3, 3, 4 }, { SDLK_4, 3, 5 }, 
    { SDLK_5, 3, 6 }, { SDLK_6, 3, 7 },
    // Byte 4: 7-0, Space, Enter, F1, F2
    { SDLK_7, 4, 0 }, { SDLK_8, 4, 1 }, { SDLK_9, 4, 2 }, { SDLK_0, 4, 3 },
    { SDLK_SPACE, 4, 4 }, { SDLK_RETURN, 4, 5 }, { SDLK_F1, 4, 6 }, { SDLK_F2, 4, 7 },
    // Byte 5: F3-F10
    { SDLK_F3, 5, 0 }, { SDLK_F4, 5, 1 }, { SDLK_F5, 5, 2 }, { SDLK_F6, 5, 3 },
    { SDLK_F7, 5, 4 }, { SDLK_F8, 5, 5 }, { SDLK_F9, 5, 6 }, { SDLK_F10, 5, 7 },
    // Byte 6: F11, F12, Esc, Tab, Caps, Backspace, Home, End
    { SDLK_F11, 6, 0 }, { SDLK_F12, 6, 1 }, { SDLK_ESCAPE, 6, 2 }, { SDLK_TAB, 6, 3 },
    { SDLK_CAPSLOCK, 6, 4 }, { SDLK_BACKSPACE, 6, 5 }, { SDLK_HOME, 6, 6 }, { SDLK_END, 6, 7 },
    // Byte 7: PgUp, PgDn, Arrows, Shifts
    { SDLK_PAGEUP, 7, 0 }, { SDLK_PAGEDOWN, 7, 1 }, { SDLK_UP, 7, 2 }, { SDLK_DOWN, 7, 3 },
    { SDLK_LEFT, 7, 4 }, { SDLK_RIGHT, 7, 5 }, { SDLK_LSHIFT, 7, 6 }, { SDLK_RSHIFT, 7, 7 },

    // ---- IDs 68-104: new keys ----
    // Byte 8: finish modifiers + GUI/Menu
    { SDLK_LCTRL, 8, 0 }, { SDLK_RCTRL, 8, 1 }, { SDLK_LALT, 8, 2 }, { SDLK_RALT, 8, 3 },
    { SDLK_LGUI, 8, 4 }, { SDLK_RGUI, 8, 5 }, { SDLK_APPLICATION, 8, 6 },
    // (byte 8 bit 7 = ID 71 reserved)

    // Byte 9: top-row punctuation (unshifted / shifted handled by monitor)
    { SDLK_BACKQUOTE, 9, 0 },      // ` ~
    { SDLK_MINUS, 9, 1 },          // - _
    { SDLK_EQUALS, 9, 2 },         // = +
    { SDLK_LEFTBRACKET, 9, 3 },    // [ {
    { SDLK_RIGHTBRACKET, 9, 4 },   // ] }
    { SDLK_BACKSLASH, 9, 5 },      // \ |
    { SDLK_SEMICOLON, 9, 6 },      // ; :
    { SDLK_QUOTE, 9, 7 },          // ' "

    // Byte 10: bottom-row punctuation + misc nav
    { SDLK_COMMA, 10, 0 },         // , <
    { SDLK_PERIOD, 10, 1 },        // . >
    { SDLK_SLASH, 10, 2 },         // / ?
    { SDLK_INSERT, 10, 3 },
    { SDLK_DELETE, 10, 4 },
    { SDLK_PRINTSCREEN, 10, 5 },
    { SDLK_SCROLLLOCK, 10, 6 },
    { SDLK_PAUSE, 10, 7 },

    // Byte 11: numpad left column + top
    { SDLK_NUMLOCKCLEAR, 11, 0 },
    { SDLK_KP_DIVIDE, 11, 1 },
    { SDLK_KP_MULTIPLY, 11, 2 },
    { SDLK_KP_MINUS, 11, 3 },
    { SDLK_KP_7, 11, 4 }, { SDLK_KP_8, 11, 5 }, { SDLK_KP_9, 11, 6 },
    { SDLK_KP_PLUS, 11, 7 },

    // Byte 12: numpad middle/bottom
    { SDLK_KP_4, 12, 0 }, { SDLK_KP_5, 12, 1 }, { SDLK_KP_6, 12, 2 },
    { SDLK_KP_1, 12, 3 }, { SDLK_KP_2, 12, 4 }, { SDLK_KP_3, 12, 5 },
    { SDLK_KP_0, 12, 6 }, { SDLK_KP_PERIOD, 12, 7 },

    // Byte 13: numpad Enter + reserved
    { SDLK_KP_ENTER, 13, 0 },
    // (IDs 105-127 free for media keys, international keys, etc.)

    // Terminator
    { 0, 0, 0 } 
};

static void step(void) {
    if (cpu.halted || cpu.error) return;
    uint16_t ir = fetch16();
    uint8_t o   = (ir >> 12) & 0xF;
    uint8_t ra  = (ir >> 8)  & 0xF;
    uint8_t rb  = (ir >> 4)  & 0xF;
    uint8_t sub = ir & 0xF;

    switch (o) {
    case 0x0:  /* SUBCODE */
        switch (sub) {
        case 0x0: break;                                  /* NOP */
        case 0x1: cpu.r[ra] *= cpu.r[rb]; break;          /* MUL */
        case 0x2:                                         /* DIV */
            if (cpu.r[rb] == 0) { cpu.error = 1; cpu.errmsg = "DIV BY ZERO"; }
            else cpu.r[ra] /= cpu.r[rb];
            break;
        case 0x3:                                         /* MOD */
            if (cpu.r[rb] == 0) { cpu.error = 1; cpu.errmsg = "MOD BY ZERO"; }
            else cpu.r[ra] %= cpu.r[rb];
            break;
        case 0x4: cpu.r[ra] &= cpu.r[rb]; break;          /* AND */
        case 0x5: cpu.r[ra] |= cpu.r[rb]; break;          /* OR  */
        case 0x6: cpu.r[ra] ^= cpu.r[rb]; break;          /* XOR */
        case 0x7: cpu.r[ra] <<= (cpu.r[rb] & 31); break;  /* SHL */
        case 0x8: cpu.r[ra] >>= (cpu.r[rb] & 31); break;  /* SHR (logical) */
        case 0x9: {                                       /* JLT Ra, Rb, addr16 */
            uint16_t t = fetch16();
            if ((int32_t)cpu.r[ra] < (int32_t)cpu.r[rb]) cpu.pc = t;
            break;
        }
        case 0xA: {                                       /* JGT Ra, Rb, addr16 */
            uint16_t t = fetch16();
            if ((int32_t)cpu.r[ra] > (int32_t)cpu.r[rb]) cpu.pc = t;
            break;
        }
        case 0xB: cpu.r[ra] = ~cpu.r[ra]; break;          /* NOT Rd (unary) */
        case 0xC: cpu.r[ra] = cpu.r[rb]; break;           /* MOV */
        case 0xD: { /* RND Rd, len */
            uint8_t len = fetch16() & 0xF;
            uint32_t result = random_cell; // Read the current hardware state
            int bits = 0;

            // 1. Advance the hardware state using Xorshift
            random_cell ^= random_cell << 13;
            random_cell ^= random_cell >> 17;
            random_cell ^= random_cell << 5;
            
            // The "Zero Trap": If Xorshift hits 0, it stays 0 forever.
            // this is just a tiny safeguard to reseed it if that happens.
            if (random_cell == 0) {
                random_cell = 0xDEADBEEF;
            }

            // 2. Calculate the bit mask based on the immediate
            if (len == 0) {            // special case; if 0, return full 32
                bits = 32;
            } else if (len <= 6) {
                bits = 1 << (len - 1); // len 1 = 1 bit, 2 = 2 bits, 3 = 4 bits, etc.
            } else {
                cpu.error = 1; cpu.errmsg = "INVALID RND LENGTH"; break;
            }

            // 3. Mask the result and store it in the register
            if (bits == 32) {
                cpu.r[ra] = result;
            } else {
                uint32_t mask = (1U << bits) - 1;
                cpu.r[ra] = result & mask;
            }
            break;
        }
        
        case 0xE: cpu.r[ra] = cpu.pc; break;             /* MOVPC Rd */
        case 0xF:
            if (rb == 0) {
                cpu.pc = cpu.r[ra];               /* JMPR Ra */
            } else if (rb == 1) {
                uint32_t t = cpu.r[ra];
                push16((cpu.pc >> 16) & 0xF);
                push16(cpu.pc & 0xFFFF);
                cpu.pc = t;                       /* CALLR Ra */
            } else {
                cpu.error = 1; cpu.errmsg = "INVALID SUB-OPCODE"; 
            }
            break;
        
        default:
            cpu.error = 1; cpu.errmsg = "INVALID SUB-OPCODE";
            break;
        }
        break;  /* exit opcode switch */

    case 0x1: cpu.halted = 1; break;                      /* HALT */
    case 0x2: {                                           /* LOADI Rd, imm20 */
        uint16_t hi = fetch16() & 0xF;
        uint16_t lo = fetch16();
        cpu.r[ra] = ((uint32_t)hi << 16) | lo;
        break;
    }
    case 0x3: cpu.r[ra] += cpu.r[rb]; break;              /* ADD */
    case 0x4: cpu.r[ra] -= cpu.r[rb]; break;              /* SUB */
    case 0x5: cpu.pc = fetch16(); break;                  /* JMP */
    case 0x6: { uint16_t t = fetch16(); if (cpu.r[ra] == 0) cpu.pc = t; break; } /* JZ */
    case 0x7: { uint16_t t = fetch16(); if (cpu.r[ra] != 0) cpu.pc = t; break; } /* JNZ */
    case 0x8: cpu.r[ra] = mem_read(cpu.r[rb]); break;     /* LOAD */
    case 0x9: mem_write(cpu.r[ra], cpu.r[rb]); break;     /* STORE */
    case 0xA: push16(cpu.r[ra]); break;                   /* PUSH */
    case 0xB: cpu.r[ra] = pop16(); break;                 /* POP */
    case 0xC: {                                           /* CALL */
        uint16_t t = fetch16();
        push16((cpu.pc >> 16) & 0xF);
        push16(cpu.pc & 0xFFFF);
        cpu.pc = t;
        break;
    }
    case 0xD: {                                           /* RET */
        uint16_t lo = pop16();
        uint16_t hi = pop16();
        cpu.pc = ((uint32_t)hi << 16) | lo;
        break;
    }
    case 0xE: {                                           /* WCHR */
        uint32_t addr = cpu.r[ra];
        uint8_t  pal  = cpu.r[rb] & 0x3;
        uint8_t  rc   = fetch16() & 0xF;
        uint8_t  chr  = cpu.r[rc] & 0x7F;
        if (addr < CHRBUF_BASE || addr > CHRBUF_END) {
            cpu.error = 1; cpu.errmsg = "WCHR address out of char buffer"; break;
        }
        charbuf[addr - CHRBUF_BASE] = (uint16_t)((pal << 7) | chr);
        break;
    }
    case 0xF: {                                           /* RCHR */
        uint32_t addr = cpu.r[ra];
        uint8_t  rd   = fetch16() & 0xF;
        if (addr < CHRBUF_BASE || addr > CHRBUF_END) {
            cpu.error = 1; cpu.errmsg = "RCHR address out of char buffer"; break;
        }
        uint16_t cell = charbuf[addr - CHRBUF_BASE];
        cpu.r[rb] = (cell >> 7) & 0x3;
        cpu.r[rd] = cell & 0x7F;
        break;
    }
    }
}

/* ---- Program emitter ---- */
static int rompos = 0;
static void emit16(uint16_t w) { rom[rompos++] = w & 0xFF; rom[rompos++] = (w >> 8) & 0xFF; }
static void op(uint8_t o, uint8_t a, uint8_t b) {
    emit16(((uint16_t)o << 12) | (a << 8) | (b << 4));
}
static void loadi(int rd, uint32_t v) {
    op(0x2, rd, 0);
    emit16((v >> 16) & 0xF);
    emit16(v & 0xFFFF);
}
static void wchr(int addr_reg, int pal_reg, int char_reg) {
    op(0xE, addr_reg, pal_reg);
    emit16((uint16_t)char_reg & 0xF);
}
static void add_(int rd, int rs) { op(0x3, rd, rs); }
static void push_(int rs) { op(0xA, rs, 0); }
static void pop_(int rd) { op(0xB, rd, 0); }
static void halt_(void) { op(0x1, 0, 0); }

/* ---- Minimal fixup support so hand-emitted loops can jump ---- */
#define MAX_FIXUPS 32
static struct { int patch_pos; int label_id; } fixups[MAX_FIXUPS];
static int n_fixups = 0;
static int label_pos[32];

static void mark_label(int id) { label_pos[id] = rompos; }

static void jmp_to(int id) {          /* JMP label */
    op(0x5, 0, 0);
    fixups[n_fixups].patch_pos = rompos;
    fixups[n_fixups].label_id  = id;
    n_fixups++;
    emit16(0);
}

static void jz_to(int ra, int id) {   /* JZ Ra, label */
    op(0x6, ra, 0);
    fixups[n_fixups].patch_pos = rompos;
    fixups[n_fixups].label_id  = id;
    n_fixups++;
    emit16(0);
}

static void jnz_to(int ra, int id) {  /* JNZ Ra, label */
    op(0x7, ra, 0);
    fixups[n_fixups].patch_pos = rompos;
    fixups[n_fixups].label_id  = id;
    n_fixups++;
    emit16(0);
}

static void apply_fixups(void) {
    for (int i = 0; i < n_fixups; i++) {
        uint16_t t = (uint16_t)label_pos[fixups[i].label_id];
        rom[fixups[i].patch_pos]     = t & 0xFF;
        rom[fixups[i].patch_pos + 1] = (t >> 8) & 0xFF;
    }
    n_fixups = 0;
}

static void store8(int addr_reg, int val_reg) { /* STORE Ra, Rs */ op(0x9, addr_reg, val_reg); }
static void sub_(int rd, int rs) { op(0x4, rd, rs); }
static void jmp_(uint16_t t)     { op(0x5, 0, 0); emit16(t); }

/* ---- Font & palette ---- */
static void put_glyph(int char_id, const uint8_t rows[8]) {
    for (int py = 0; py < 8; py++) {
        uint8_t bits = rows[py];
        for (int px = 0; px < 8; px++) {
            uint8_t nib = (bits & (0x80 >> px)) ? 0xF : 0x0;
            int idx = char_id * 32 + py * 4 + px / 2;
            if (px % 2 == 0) fontbuf[idx] = (fontbuf[idx] & 0x0F) | (nib << 4);
            else             fontbuf[idx] = (fontbuf[idx] & 0xF0) | nib;
        }
    }
}

static void load_file(const char *path, void *buf, size_t sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return;                    // missing file -> leave zeroed 
    fread(buf, 1, sz, f);
    fclose(f);
}

static void save_file(const char *path, const void *buf, size_t sz) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, sz, f);
    fclose(f);
}

static void load_persistent(void) {
    load_file(ROM_FILE,     rom,     sizeof(rom));
    load_file(PMEM1_FILE,   pmem1,   sizeof(pmem1));
    load_file(PMEM2_FILE,   pmem2,   sizeof(pmem2));
    load_file(EXTPMEM_FILE, extpmem, sizeof(extpmem));
}

static void save_persistent(void) {
    save_file(PMEM1_FILE,   pmem1,   sizeof(pmem1));
    save_file(PMEM2_FILE,   pmem2,   sizeof(pmem2));
    save_file(EXTPMEM_FILE, extpmem, sizeof(extpmem));
}

static void reset_cpu(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.sp = 0x7FE;
    cpu.pc = 0;
    random_cell = (uint32_t)SDL_GetTicks() | 1; 
}

/* ---- Render ---- */
static void render(SDL_Renderer *ren, SDL_Texture *tex) {
    void *pix; int pitch;
    SDL_LockTexture(tex, NULL, &pix, &pitch);
    uint32_t *pixels = (uint32_t*)pix;
    int stride = pitch / 4;

    for (int row = 0; row < SCR_ROWS; row++) {
        for (int col = 0; col < SCR_COLS; col++) {
            uint16_t cell = charbuf[row * SCR_COLS + col];
            int pal_id = (cell >> 7) & 0x3;
            int ch_id  = cell & 0x7F;
            const uint8_t *glyph = &fontbuf[ch_id * 32];

            for (int py = 0; py < CHAR_H; py++) {
                for (int px = 0; px < CHAR_W; px++) {
                    int byte_idx = py * 4 + px / 2;
                    uint8_t byte = glyph[byte_idx];
                    uint8_t cidx = ((px % 2) == 0) ? (byte >> 4) : (byte & 0xF);
                    uint8_t color = palbuf[pal_id * 16 + cidx];
                    int r3 = (color >> 5) & 0x7;
                    int g3 = (color >> 2) & 0x7;
                    int b2 = color & 0x3;
                    int r8 = r3 * 255 / 7;
                    int g8 = g3 * 255 / 7;
                    int b8 = b2 * 255 / 3;
                    uint32_t c = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
                    int sx = (col * CHAR_W + px) * UPSCALE;
                    int sy = (row * CHAR_H + py) * UPSCALE;
                    for (int oy = 0; oy < UPSCALE; oy++)
                        for (int ox = 0; ox < UPSCALE; ox++)
                            pixels[(sy + oy) * stride + (sx + ox)] = c;
                }
            }
        }
    }

    SDL_UnlockTexture(tex);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

/* ---- Main ---- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Helio 213",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }

    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, WIN_W, WIN_H);
    if (!tex) { fprintf(stderr, "CreateTexture: %s\n", SDL_GetError()); return 1; }

    load_persistent();
    load_file("memory/font.bin", fontbuf, sizeof(fontbuf));
    load_file("memory/palette.bin", palbuf, sizeof(palbuf));
    
    reset_cpu();

    /* --- NEW: 13 MHz Clock Emulation --- */
    int running = 1;
    const double TARGET_HZ = 13000000.0; // 13 MHz
    uint64_t last_time = SDL_GetPerformanceCounter();
    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    double cycle_accumulator = 0.0;

    while (running) {
        // Manage keyboard input
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    // In the future, to trap ESC in emulator, comment this out
                    running = 0; 
                }
                
                // Find the key in our 68-key mapping and set the bit to 1
                for (int i = 0; key_mappings[i].sym != 0; i++) {
                    if (key_mappings[i].sym == e.key.keysym.sym) {
                        keymap[key_mappings[i].byte_idx] |= (1 << key_mappings[i].bit_idx);
                        break;
                    }
                }
            }
            
            if (e.type == SDL_KEYUP) {
                // Find the key in our 68-key mapping and clear the bit to 0
                for (int i = 0; key_mappings[i].sym != 0; i++) {
                    if (key_mappings[i].sym == e.key.keysym.sym) {
                        keymap[key_mappings[i].byte_idx] &= ~(1 << key_mappings[i].bit_idx);
                        break;
                    }
                }
            }
        }

        // 1. Calculate how much time has passed and how many CPU cycles to run
        uint64_t current_time = SDL_GetPerformanceCounter();
        double elapsed = (double)(current_time - last_time) / (double)perf_freq;
        last_time = current_time;

        cycle_accumulator += elapsed * TARGET_HZ;
        int cycles_to_run = (int)cycle_accumulator;
        cycle_accumulator -= cycles_to_run;

        // 2. Run the CPU
        for (int i = 0; i < cycles_to_run; i++) {
            if (!cpu.halted && !cpu.error) {
                step();
            }
        }

        // If the CPU throws an error, print it once and halt so it doesn't spam.
        if (cpu.error) {
            printf("CPU ERROR: %s\n", cpu.errmsg);
            cpu.error = 0; 
            cpu.halted = 1; 
        }

        // 3. Increment clock
        uint32_t now_ms = SDL_GetTicks();
        uint32_t elapsed_ms = now_ms - last_clock_tick;
        if (elapsed_ms >= 10) {
            system_clock += (elapsed_ms / 10); 
            last_clock_tick += ((elapsed_ms / 10) * 10);
        }

        // 4. Draw the screen
        render(ren, tex);

        // 5. Save persistent memory every second
        static uint32_t last_save = 0;
        if (now_ms - last_save >= SAVE_INTERVAL_MS) {
            save_persistent();
            last_save = now_ms;
        }
    }

    save_persistent();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}