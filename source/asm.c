// asm.c — Helio 213 Assembler
// Build: gcc -O2 -o asm asm.c
// Usage: ./asm program.asm memory/rom.bin
//
// .ORG <addr>   -- must appear before any labels, code, or data.
//     ROM    (0x00000-0x007FF): byte-addressed
//     RAM8   (0x01020-0x0501F): byte-addressed
//     RAM16  (0x05020-0x0511F): word-addressed
//     PMEM1  (0x06AC0-0x0EABF): byte-addressed
//     PMEM2  (0x0EAC0-0x16ABF): byte-addressed
//     EXTPMEM(0x16AC0-0x1EABF): byte-addressed
// Output file always starts at byte 0. Place with monitor's LOAD.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#define MAX_LABELS 256
#define MAX_FIXUPS 512
#define ROM_SIZE   0x8000   /* 32 KiB — fits any single region */
#define MAX_LINE   256

/* ---- Memory map (new) ---- */
#define ROM_BASE     0x00000
#define ROM_END      0x00FFF
#define RAM8_BASE    0x01820
#define RAM8_END     0x0581F
#define RAM16_BASE   0x05820
#define RAM16_END    0x0591F
#define PMEM1_BASE   0x072C0
#define PMEM1_END    0x0F2BF
#define PMEM2_BASE   0x0F2C0
#define PMEM2_END    0x172BF
#define EXTPMEM_BASE 0x172C0
#define EXTPMEM_END  0x1F2BF

static uint8_t rom[ROM_SIZE];
static int rompos = 0;

static uint32_t current_org = 0;
static uint32_t region_end  = ROM_END;
static int addr_scale = 1;   /* 1 = byte-addressed, 2 = word-addressed */

static struct { char name[32]; int addr; } labels[MAX_LABELS];
static int n_labels = 0;

static struct { int patch_pos; char label[32]; int line_num; } fixups[MAX_FIXUPS];
static int n_fixups = 0;

static int line_num = 0;
static int error = 0;

static void err(const char *msg) {
    fprintf(stderr, "Error on line %d: %s\n", line_num, msg);
    error = 1;
}

static void emit16(uint16_t w) {
    if (rompos >= ROM_SIZE - 1) { err("output buffer overflow"); return; }
    rom[rompos++] = w & 0xFF;
    rom[rompos++] = (w >> 8) & 0xFF;
}

static void emit8(uint8_t b) {
    if (rompos >= ROM_SIZE) { err("output buffer overflow"); return; }
    rom[rompos++] = b;
}

static void op(uint8_t o, uint8_t a, uint8_t b) {
    emit16(((uint16_t)o << 12) | (a << 8) | (b << 4));
}

static void op_sub(uint8_t o, uint8_t a, uint8_t b, uint8_t sub) {
    emit16(((uint16_t)o << 12) | (a << 8) | (b << 4) | sub);
}

static uint32_t label_addr(void) {
    return current_org + (uint32_t)rompos / (uint32_t)addr_scale;
}

static int find_label(const char *name) {
    for (int i = 0; i < n_labels; i++)
        if (strcmp(labels[i].name, name) == 0) return labels[i].addr;
    return -1;
}

static int parse_reg(const char *s) {
    if (!s || (toupper(s[0]) != 'R')) return -1;
    char *end; long v = strtol(s + 1, &end, 10);
    if (*end != '\0' || v < 0 || v > 15) return -1;
    return (int)v;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static int handle_org(const char *arg) {
    if (rompos != 0 || n_labels != 0) {
        err(".ORG must appear before any labels, code, or data");
        return -1;
    }
    if (!arg || arg[0] == '\0') { err(".ORG requires an address argument"); return -1; }
    char *end;
    uint32_t org = (uint32_t)strtoul(arg, &end, 0);
    if (*end != '\0') { err("Invalid .ORG address"); return -1; }

    if      (org <= ROM_END)                            { addr_scale = 1; region_end = ROM_END; }
    else if (org >= RAM8_BASE     && org <= RAM8_END)    { addr_scale = 1; region_end = RAM8_END; }
    else if (org >= PMEM1_BASE    && org <= PMEM1_END)   { addr_scale = 1; region_end = PMEM1_END; }
    else if (org >= PMEM2_BASE    && org <= PMEM2_END)   { addr_scale = 1; region_end = PMEM2_END; }
    else if (org >= EXTPMEM_BASE  && org <= EXTPMEM_END) { addr_scale = 1; region_end = EXTPMEM_END; }
    else if (org >= RAM16_BASE    && org <= RAM16_END)   { addr_scale = 2; region_end = RAM16_END; }
    else {
        err(".ORG address not in an executable region");
        return -1;
    }
    current_org = org;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.asm output.bin\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("fopen input"); return 1; }

    char line[MAX_LINE];

    /* ---- Pass 1: labels + size ---- */
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        char *s = trim(line);

        char *cmt = strchr(s, ';');   /* strip comments so .ORG args parse cleanly */
        if (cmt) *cmt = '\0';
        if (s[0] == '\0') continue;

        char *colon = strchr(s, ':');
        if (colon) {
            *colon = '\0';
            char *lbl = trim(s);
            if (n_labels >= MAX_LABELS) { err("Too many labels"); break; }
            strncpy(labels[n_labels].name, lbl, 31);
            labels[n_labels].addr = (int)label_addr();
            n_labels++;
            s = trim(colon + 1);
            if (s[0] == '\0') continue;
        }

        char mnem[16] = {0};
        sscanf(s, "%15s", mnem);
        for (int i = 0; mnem[i]; i++) mnem[i] = toupper(mnem[i]);

        if (strcmp(mnem, ".ORG") == 0) {
            char orgarg[32] = {0};
            sscanf(s, "%*s %31s", orgarg);
            handle_org(orgarg);
            continue;
        }

        if (strcmp(mnem, "NOP")  == 0 || strcmp(mnem, "HALT")  == 0 ||
            strcmp(mnem, "ADD")  == 0 || strcmp(mnem, "SUB")   == 0 ||
            strcmp(mnem, "LOAD") == 0 || strcmp(mnem, "STORE") == 0 ||
            strcmp(mnem, "PUSH") == 0 || strcmp(mnem, "POP")   == 0 ||
            strcmp(mnem, "MUL")  == 0 || strcmp(mnem, "DIV")   == 0 ||
            strcmp(mnem, "MOD")  == 0 || strcmp(mnem, "AND")   == 0 ||
            strcmp(mnem, "OR")   == 0 || strcmp(mnem, "XOR")   == 0 ||
            strcmp(mnem, "SHL")  == 0 || strcmp(mnem, "SHR")   == 0 ||
            strcmp(mnem, "NOT")  == 0 || strcmp(mnem, "MOV")   == 0 ||
            strcmp(mnem, "MOVPC") == 0 || strcmp(mnem, "JMPR") == 0 ||
            strcmp(mnem, "CALLR") == 0) {
            rompos += 2;
        } else if (strcmp(mnem, "LOADI") == 0) {
            rompos += 6;
        } else if (strcmp(mnem, "JMP")  == 0 || strcmp(mnem, "JZ")   == 0 ||
                   strcmp(mnem, "JNZ")  == 0 || strcmp(mnem, "CALL") == 0 ||
                   strcmp(mnem, "WCHR") == 0 || strcmp(mnem, "RCHR") == 0 ||
                   strcmp(mnem, "JLT")  == 0 || strcmp(mnem, "JGT")  == 0 ||
                   strcmp(mnem, "RND")  == 0) {
            rompos += 4;
        } else if (strcmp(mnem, "RET") == 0) {
            rompos += 2;
        } else if (strcmp(mnem, ".BYTE") == 0) {
            rompos += 1;
        } else if (strcmp(mnem, ".WORD") == 0) {
            rompos += 2;
        } else {
            fprintf(stderr, "Error on line %d: Unknown mnemonic '%s'\n", line_num, mnem);
            error = 1;
        }
    }

    /* ---- Pass 2: emit ---- */
    fseek(f, 0, SEEK_SET);
    line_num = 0;
    rompos = 0;
    memset(rom, 0, sizeof(rom));
    /* Keep current_org / addr_scale / region_end from pass 1. */

    while (fgets(line, sizeof(line), f)) {
        line_num++;
        char *s = trim(line);
        if (s[0] == ';' || s[0] == '\0') continue;

        char *colon = strchr(s, ':');
        if (colon) {
            s = trim(colon + 1);
            if (s[0] == '\0') continue;
        }

        char mnem[16] = {0};
        char arg1[32] = {0}, arg2[32] = {0}, arg3[32] = {0};

        char *comment = strchr(s, ';');
        if (comment) *comment = '\0';
        for (int i = 0; s[i]; i++) if (s[i] == ',') s[i] = ' ';

        int nargs = sscanf(s, "%15s %31s %31s %31s", mnem, arg1, arg2, arg3);
        for (int i = 0; mnem[i]; i++) mnem[i] = toupper(mnem[i]);
        if (nargs == 0) continue;

        if (strcmp(mnem, ".ORG") == 0) continue;   /* handled in pass 1 */

        if (strcmp(mnem, "NOP") == 0) {
            emit16(0x0000);
        } else if (strcmp(mnem, "HALT") == 0) {
            emit16(0x1000);
        } else if (strcmp(mnem, "LOADI") == 0) {
            int rd = parse_reg(arg1);
            if (rd < 0) { err("Invalid register for LOADI"); continue; }
            int addr = find_label(arg2);
            uint32_t val = 0;
            if (addr >= 0) {
                val = (uint32_t)addr;
            } else if (arg2[0] == '\'') {
                if (arg2[2] == '\'' && arg2[3] == '\0') {
                    char c = toupper(arg2[1]);
                    if (c >= 'A' && c <= 'Z') val = c - 'A' + 1;
                    else if (c >= '0' && c <= '9') {
                        if (c == '0') val = 36;
                        else val = (c - '1') + 27;
                    } else val = 0;
                } else if (arg2[3] == '\'' && arg2[4] == '\0') {
                    switch (arg2[2]) {
                    case 'n': val = '\n'; break;
                    case 't': val = '\t'; break;
                    case '0': val = 0;    break;
                    case '\'': val = '\''; break;
                    case '\\': val = '\\'; break;
                    default: err("bad escape"); val = 0;
                    }
                } else { err("malformed char literal"); val = 0; }
            } else {
                val = strtoul(arg2, NULL, 0);
            }
            op(0x2, rd, 0);
            emit16((val >> 16) & 0xF);
            emit16(val & 0xFFFF);
        } else if (strcmp(mnem, "MUL") == 0 || strcmp(mnem, "DIV") == 0 ||
                   strcmp(mnem, "MOD") == 0 || strcmp(mnem, "AND") == 0 ||
                   strcmp(mnem, "OR")  == 0 || strcmp(mnem, "XOR") == 0 ||
                   strcmp(mnem, "SHL") == 0 || strcmp(mnem, "SHR") == 0 ||
                   strcmp(mnem, "NOT") == 0 || strcmp(mnem, "MOV") == 0) {
            int ra = parse_reg(arg1);
            int rb = parse_reg(arg2);
            if (strcmp(mnem, "NOT") == 0 && rb < 0) rb = ra;
            if (ra < 0 || rb < 0) { err("Invalid registers for ALU"); continue; }
            uint8_t sub = 0;
            if (strcmp(mnem, "MUL") == 0) sub = 0x1;
            if (strcmp(mnem, "DIV") == 0) sub = 0x2;
            if (strcmp(mnem, "MOD") == 0) sub = 0x3;
            if (strcmp(mnem, "AND") == 0) sub = 0x4;
            if (strcmp(mnem, "OR")  == 0) sub = 0x5;
            if (strcmp(mnem, "XOR") == 0) sub = 0x6;
            if (strcmp(mnem, "SHL") == 0) sub = 0x7;
            if (strcmp(mnem, "SHR") == 0) sub = 0x8;
            if (strcmp(mnem, "NOT") == 0) sub = 0xB;
            if (strcmp(mnem, "MOV") == 0) sub = 0xC;
            op_sub(0x0, ra, rb, sub);
        } else if (strcmp(mnem, "ADD") == 0 || strcmp(mnem, "SUB") == 0 ||
                   strcmp(mnem, "LOAD") == 0 || strcmp(mnem, "STORE") == 0) {
            int rd = parse_reg(arg1);
            int rs = parse_reg(arg2);
            if (rd < 0 || rs < 0) { err("Invalid registers"); continue; }
            uint8_t opc = 0;
            if (strcmp(mnem, "ADD") == 0)   opc = 0x3;
            if (strcmp(mnem, "SUB") == 0)   opc = 0x4;
            if (strcmp(mnem, "LOAD") == 0)  opc = 0x8;
            if (strcmp(mnem, "STORE") == 0) opc = 0x9;
            op(opc, rd, rs);
        } else if (strcmp(mnem, "PUSH") == 0 || strcmp(mnem, "POP") == 0) {
            int rd = parse_reg(arg1);
            if (rd < 0) { err("Invalid register"); continue; }
            if (strcmp(mnem, "PUSH") == 0) op(0xA, rd, 0);
            if (strcmp(mnem, "POP") == 0)  op(0xB, rd, 0);
        } else if (strcmp(mnem, "JMP") == 0 || strcmp(mnem, "JZ") == 0 ||
                   strcmp(mnem, "JNZ") == 0 || strcmp(mnem, "CALL") == 0) {
            uint8_t opc = 0;
            int reg = 0;
            char *targ = arg1;
            if (strcmp(mnem, "JMP") == 0)  { opc = 0x5; targ = arg1; }
            if (strcmp(mnem, "CALL") == 0) { opc = 0xC; targ = arg1; }
            if (strcmp(mnem, "JZ") == 0 || strcmp(mnem, "JNZ") == 0) {
                reg = parse_reg(arg1);
                if (reg < 0) { err("Invalid register for branch"); continue; }
                opc = (strcmp(mnem, "JZ") == 0) ? 0x6 : 0x7;
                targ = arg2;
            }
            int addr = find_label(targ);
            if (addr >= 0) {
                if (addr > 0xFFFF) {
                    err("Branch target exceeds 16-bit immediate");
                    continue;
                }
                op(opc, reg, 0);
                emit16((uint16_t)addr);
            } else {
                op(opc, reg, 0);
                if (n_fixups >= MAX_FIXUPS) { err("Too many fixups"); continue; }
                fixups[n_fixups].patch_pos = rompos;
                strncpy(fixups[n_fixups].label, targ, 31);
                fixups[n_fixups].line_num = line_num;
                n_fixups++;
                emit16(0);
            }
        } else if (strcmp(mnem, "JLT") == 0 || strcmp(mnem, "JGT") == 0) {
            int ra = parse_reg(arg1);
            int rb = parse_reg(arg2);
            if (ra < 0 || rb < 0) { err("Invalid registers"); continue; }
            uint8_t sub = (strcmp(mnem, "JLT") == 0) ? 0x9 : 0xA;
            char *targ = arg3;
            int addr = find_label(targ);
            if (addr >= 0) {
                if (addr > 0xFFFF) {
                    err("Branch target exceeds 16-bit immediate");
                    continue;
                }
                op_sub(0x0, ra, rb, sub);
                emit16((uint16_t)addr);
            } else {
                op_sub(0x0, ra, rb, sub);
                if (n_fixups >= MAX_FIXUPS) { err("Too many fixups"); continue; }
                fixups[n_fixups].patch_pos = rompos;
                strncpy(fixups[n_fixups].label, targ, 31);
                fixups[n_fixups].line_num = line_num;
                n_fixups++;
                emit16(0);
            }
        } else if (strcmp(mnem, "RET") == 0) {
            emit16(0xD000);
        } else if (strcmp(mnem, "WCHR") == 0) {
            int ra = parse_reg(arg1), rb = parse_reg(arg2), rc = parse_reg(arg3);
            if (ra < 0 || rb < 0 || rc < 0) { err("Invalid registers for WCHR"); continue; }
            op(0xE, ra, rb);
            emit16(rc);
        } else if (strcmp(mnem, "RCHR") == 0) {
            int ra = parse_reg(arg1), rb = parse_reg(arg2), rc = parse_reg(arg3);
            if (ra < 0 || rb < 0 || rc < 0) { err("Invalid registers for RCHR"); continue; }
            op(0xF, ra, rb);
            emit16(rc);
        } else if (strcmp(mnem, "RND") == 0) {
            int rd = parse_reg(arg1);
            if (rd < 0) { err("Invalid register for RND"); continue; }
            int addr = find_label(arg2);
            uint32_t len = (addr >= 0) ? (uint32_t)addr : strtoul(arg2, NULL, 0);
            if (len > 6) { err("RND length must be 0-6"); continue; }
            op_sub(0x0, rd, 0, 0xD);
            emit16(len & 0xF);
        } else if (strcmp(mnem, "MOVPC") == 0) {
            int rd = parse_reg(arg1);
            if (rd < 0) { err("Invalid register for MOVPC"); continue; }
            op_sub(0x0, rd, 0, 0xE);
        } else if (strcmp(mnem, "JMPR") == 0) {
            int ra = parse_reg(arg1);
            if (ra < 0) { err("Invalid register for JMPR"); continue; }
            op_sub(0x0, ra, 0, 0xF);            /* rb = 0: JMPR */
        } else if (strcmp(mnem, "CALLR") == 0) {
            int ra = parse_reg(arg1);
            if (ra < 0) { err("Invalid register for CALLR"); continue; }
            op_sub(0x0, ra, 1, 0xF);            /* rb = 1: CALLR */
        } else if (strcmp(mnem, ".BYTE") == 0) {
            emit8(strtoul(arg1, NULL, 0) & 0xFF);
        } else if (strcmp(mnem, ".WORD") == 0) {
            emit16((uint16_t)strtoul(arg1, NULL, 0));
        } else {
            fprintf(stderr, "Error on line %d: Unknown instruction '%s'\n", line_num, mnem);
            error = 1;
        }
    }

    /* ---- Apply fixups ---- */
    for (int i = 0; i < n_fixups; i++) {
        int addr = find_label(fixups[i].label);
        if (addr < 0) {
            fprintf(stderr, "Error on line %d: Undefined label '%s'\n",
                    fixups[i].line_num, fixups[i].label);
            error = 1;
            continue;
        }
        if (addr > 0xFFFF) {
            fprintf(stderr, "Error on line %d: Branch target '0x%X' exceeds 16-bit immediate\n",
                    fixups[i].line_num, addr);
            error = 1;
            continue;
        }
        rom[fixups[i].patch_pos]     = addr & 0xFF;
        rom[fixups[i].patch_pos + 1] = (addr >> 8) & 0xFF;
    }

    fclose(f);

    if (error) { fprintf(stderr, "Assembly failed.\n"); return 1; }

    /* Region overflow warning (applies to any region, not just ROM) */
    uint32_t end_addr = current_org + (uint32_t)rompos / (uint32_t)addr_scale;
    if (end_addr > region_end + 1) {
        fprintf(stderr, "Warning: program ends at 0x%X, past region end 0x%X\n",
                end_addr - 1, region_end);
    }

    /* Write only the bytes we produced; emulator's load_file handles short files. */
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror("fopen output"); return 1; }
    fwrite(rom, 1, rompos, out);
    fclose(out);

    printf("Assembled %d bytes to %s\n", rompos, argv[2]);
    return 0;
}