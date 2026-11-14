; monitor.asm - Edge-detecting Monitor OS
;
; Primitive operating system and shell for the HELIO213
; Currently implemented commands = ECHO, RUN, PAL, LOAD
; To do - Implement filesystem management and necessary
; commands - FILE, FORMAT - and subcommands for filesys
; navigation, and find some solution to XIP requirement
; for RUN programs. Perhaps all programs need to LOAD
; to a dedicated RAM address before executing?
; But PMEM is perfectly usable as directly-addressable
; memory, and programs being made to use that could be
; up to 32K while still having the whole 12k of 8-bit
; RAM and 1K addresses of 16-bit RAM if they are built
; properly to run in PMEM. If we force a copy to RAM,
; then RAM becomes suddenly very limited; the bigger
; a program gets, the less RAM it would be able to use
; and that would go entirely against the goals of the
; program itself more than likely. Not sure how to
; solve that problem at the moment...

START:
    ; Setup
    LOADI R0 0x05920       ; R0 = Cursor (start at top left of char buffer)
    LOADI R9 3             ; R9 = Palette 3 (cool colors)
    LOADI R10 1            ; R10 = General increment (1)
    LOADI R11 0x01840      ; R11 = Start of 8-bit RAM (input buffer)
    LOADI R12 0            ; R12 = Buffer length counter
    MOV   R14 R0           ; Copy value of cursor to R14

    ; Clear the previous keymap state in RAM (0x01820 to 0x01828)
    LOADI R6 0x01820
    LOADI R7 0
    LOADI R8 16

CLEAR_PREV:
    STORE R6 R7            ; Store the value of R6 at address in R7
    ADD R6 R10             ; Increment R6 by  1
    SUB R8 R10             ; Increment R8 by -1
    JNZ R8 CLEAR_PREV      ; Loop if R8 != 0

    ; Print the prompt ">"
    LOADI R1 62            ; Font index 62 (our > symbol)
    WCHR R0 R9 R1          ; Write '>' at cursor
    ADD R0 R10             ; Increment cursor by 1
    LOADI R1 0             ; Set new value of R1 to null char ID
    WCHR R0 R9 R1          ; Print null char to cursor
    ADD R0 R10             ; Increment cursor

POLL_LOOP:
    LOADI R1 0x01804       ; Base of keymap hardware
    LOADI R6 0x01820       ; Base of previous keymap in RAM
    LOADI R8 16            ; 9 bytes to check

CHECK_BYTES:
    ; Checks if keyboard map has changed since last keypress
    LOAD R2 R1             ; Read hardware keymap byte
    LOAD R3 R6             ; Read previous state byte

    ; Check for difference (XOR)
    MOV R5 R2              ; Copy value of R2 into R5
    XOR R5 R3
    JNZ R5 KEY_CHANGED     ; If they differ something changed!

    ; If they are the same move to next byte
    ADD R1 R10             ; Increment R1 by  1
    ADD R6 R10             ; Increment R6 by  1
    SUB R8 R10             ; Increment R8 by -1
    JNZ R8 CHECK_BYTES     ; Loop if R8 != 0

    JMP POLL_LOOP          ; No changes loop forever

KEY_CHANGED:
    ; R1 = Hardware address
    ; R2 = Current hardware state
    ; R3 = Previous state
    ; R6 = RAM address of previous state

    ; Determine if it was a press (bit is 1 now 0 before) or release.
    ; A keypress means R2 has a bit that R3 does not.
    ; Logic: If (R2 & ~R3) != 0 it's a press.

    MOV R5 R3
    NOT R5                 ; R5 = ~R3
    AND R5 R2              ; R5 = R2 & ~R3 (bits that just went from 0 to 1)
    JZ R5 UPDATE_STATE     ; If no new bits it was just a release. Ignore.

    ; If we get to this point, it has to be a keypress.
    ; R5 will thus contain the bitmask of the new key,
    ; so let's find the specific bit now set in R5.
    LOADI R3 1             ; R3 = Bit mask
    LOADI R4 0             ; R4 = Bit index

FIND_BIT:
    MOV R7 R5              ; Copy keymap to R7
    AND R7 R3              ; If current & old keymap both 1, bit is 1
    JNZ R7 BIT_SET         ; Jump if result is not 0 to BIT_SET
    SHL R3 R10             ; Shift mask
    ADD R4 R10             ; Increment bit
    JMP FIND_BIT           ; Loop

BIT_SET:
    ; Calculate the linear key ID (0 to 82)
    LOADI R7 0x01804       ; Load first byte of keymap into R7
    SUB R1 R7              ; R1 = Byte offset
    LOADI R7 8             ; Set R7 to a 1 byte offset
    MUL R1 R7              ; Multiply R1 by R7
    ADD R1 R4              ; R1 = Physical key ID (0 to 82)

    ; Intercept special non-printing keys first
    LOADI R7 37            ; Check Enter (ID 37)
    SUB R7 R1
    JZ R7 HANDLE_ENTER

    LOADI R7 53            ; Check Backspace (ID 53)
    SUB R7 R1
    JZ R7 HANDLE_BACKSPACE

    ; Check Shift Status
    LOADI R6 0x0180B       ; Byte 7 of hardware keymap
    LOAD R7 R6
    LOADI R8 0xC0          ; LShift | RShift
    AND R7 R8
    JNZ R7 LOOKUP_SHIFTED

LOOKUP_UNSHIFTED:
    LOADI R7 TABLE_U       ; R7 = Base address of unshifted table
    ADD R1 R7              ; R1 = Base + KeyID
    LOAD R1 R1             ; R1 = The ASCII value loaded from ROM
    JZ R1 UPDATE_STATE     ; If the value is 0 (like for F1 or Shift), ignore it
    JMP PRINT_CHAR

LOOKUP_SHIFTED:
    LOADI R7 TABLE_S       ; R7 = Base address of shifted table
    ADD R1 R7              ; R1 = Base + KeyID
    LOAD R1 R1             ; R1 = The ASCII value loaded from ROM
    JZ R1 UPDATE_STATE     ; If 0, ignore
    JMP PRINT_CHAR

PRINT_ZERO:
    LOADI R1 48            ; ASCII '0'
    JMP PRINT_CHAR

PRINT_CHAR:
    WCHR R0 R9 R1          ; Write to screen
    STORE R11 R1           ; Save to RAM buffer
    ADD R11 R10            ; Increment buffer pointer
    ADD R12 R10            ; Increment buffer length
    ADD R0 R10             ; Move cursor forward
    JMP UPDATE_STATE

UPDATE_STATE:
    ; Copy the entire current hardware keymap to RAM so we don't repeat this key
    LOADI R1 0x01804       ; Point to hardware keymap
    LOADI R6 0x01820       ; Point to RAM keymap
    LOADI R8 16            ; 9 bytes to copy

UPDATE_LOOP:
    LOAD R2 R1             ; Read hardware state
    STORE R6 R2            ; Save to RAM
    ADD R1 R10             ; Increment hardware pointer
    ADD R6 R10             ; Increment RAM pointer
    SUB R8 R10             ; Decrement counter
    JNZ R8 UPDATE_LOOP     ; Loop until all 9 bytes are copied

    JMP POLL_LOOP          ; Go back to waiting for input

HANDLE_BACKSPACE:
    JZ R12 UPDATE_STATE    ; If buffer empty do nothing
    SUB R12 R10            ; Decrement buffer length
    SUB R11 R10            ; Decrement buffer pointer
    SUB R0 R10             ; Move cursor back one cell
    LOADI R1 0             ; Blank character
    WCHR R0 R9 R1          ; Erase character on screen
    JMP UPDATE_STATE       ; Sync keymap state

HANDLE_ENTER:
    ; 1. Erase prompt erase cursor and terminate string in RAM
    LOADI R1 0             ; Null Character (ASCII 0)

    WCHR R14 R9 R1         ; Erase the '>' prompt on screen
    WCHR R0  R9 R1         ; Erase the cursor position on screen
    STORE R11 R1           ; NULL terminate string in RAM

    ; 2. Calculate the address of the next line
    LOADI R6 0x05920       ; R6 = Base address
    MOV R7 R0              ; Copy current cursor address to R7
    SUB R7 R6              ; R7 = Offset (how far into the buffer are we?)

    LOADI R8 60            ; R8 = Screen width
    DIV R7 R8              ; R7 = Current Row (e.g. 0 1 2...)
    ADD R7 R10             ; R7 = Next Row (Current + 1)

    MUL R7 R8              ; R7 = Next Row Offset
    MOV R0 R7              ; Copy new offset to R0
    ADD R0 R6              ; R0 = Absolute address of the start of the next row

    ; 3. Check if we ran off the bottom of the screen
    LOADI R7 0x0627F       ; R7 = Last address of char buffer
    JGT R0 R7 WRAP_TO_TOP  ; If next row past the buffer wrap to top
    JMP NORMAL_ENTER       ; Else print new prompt

NORMAL_ENTER:
    ; 4. Save the new prompt location and print "> "
    MOV R14 R0             ; R14 now points to the NEW '>' prompt
    JMP HANDLE_COMMAND     ; Check if a command was entered

HANDLE_COMMAND:
    LOADI R15 0x01840
    CALL  SKIP_SPACE       ; skip leading whitespace
    LOAD  R1 R15
    JZ    R1 PRINT_PROMPT  ; this was a blank line

    ; Check for ECHO
    LOADI R13 CMD_ECHO
    CALL  STRCMP
    JNZ   R1 DO_ECHO

    ; Check for FILE
    LOADI R15 0x01840      ; Reset pointer before scanning for command
    LOADI R13 CMD_FILE
    CALL STRCMP
    ; JNZ R1 DO_FILE

    ; Check for FORMAT
    LOADI R15 0x01840      ; Reset pointer before scanning for command
    LOADI R13 CMD_FORMAT
    CALL STRCMP
    ; JNZ R1 DO_FORMAT

    ; Check for RUN
    LOADI R15 0x01840
    LOADI R13 CMD_RUN
    CALL  STRCMP
    JNZ   R1 DO_RUN

    ; Check for LOAD
    LOADI R15 0x01840
    LOADI R13 CMD_LOAD
    CALL  STRCMP
    JNZ R1 DO_LOAD

    ; Check for PAL
    LOADI R15 0x01840
    LOADI R13 CMD_PAL
    CALL  STRCMP
    JNZ   R1 DO_PAL

    ; Unknown Command
    JMP UNKNOWN_CMD

STRCMP:
    LOAD R2 R15            ; Load char from user input
    LOAD R3 R13            ; Load char from ROM string

    ; If the ROM string is 0, we hit the end of the command name.
    JZ R3 ROM_END

    ; Compare the characters
    SUB R2 R3
    JNZ R2 NO_MATCH        ; If they differ, it's not a match

    ; Move to next char
    ADD R15 R10
    ADD R13 R10
    JMP STRCMP

ROM_END:
    ; The ROM string is done. What about the user string?
    JZ R2 MATCH            ; If user string is 0, it's a perfect match

    LOADI R3 32            ; ASCII Space
    SUB R2 R3
    JZ R2 MATCH            ; If user string is a space, it's a command with arguments

    JMP NO_MATCH           ; Otherwise, it's a mismatch (e.g., "ECHOS")

MATCH:
    LOADI R1 1             ; Return 1
    RET

NO_MATCH:
    LOADI R1 0             ; Return 0
    RET

WRAP_TO_TOP:
    LOADI R0 0x05920       ; Start back at the top-left of the screen
    JMP NORMAL_ENTER       ; Go print the prompt at the new position

; Commands for the shell are implemented below.

; ECHO command: Repeat user-supplied string
DO_ECHO:
    CALL SKIP_SPACE        ; Skip the space.

ECHO_LOOP:
    LOAD R1 R15            ; Read current character from input buffer
    JZ R1 ECHO_DONE        ; if character is 0 (null char) command end
    WCHR R0 R9 R1          ; else write curr char to the character buffer
    ADD R0 R10             ; Increment print cursor by 1
    ADD R15 R10            ; Increment scan cursor by 1
    JMP ECHO_LOOP          ; Loop until null char

ECHO_DONE:
    CALL NEWLINE           ; Move to new line before printing prompt
    JMP PRINT_PROMPT

SKIP_SPACE:
    LOAD  R1 R15           ; Read current character from input buffer
    LOADI R2 32            ; ASCII id of space character
    SUB R1 R2              ; Subtract value of space from curr char
    JNZ R1 SKIP_SPACE_DONE ; if result not 0, then not a space
    ADD R15 R10            ; increment cursor by 1
    JMP SKIP_SPACE         ; loop until not a space
SKIP_SPACE_DONE:
    RET

UNKNOWN_CMD:
    LOADI R15 UNKNOWN_STR  ; Unknown error string
    CALL  ERR_PRINT        ; Print ERR
    JMP   PRINT_PROMPT     ; New prompt

ERR_PRINT:
    PUSH  R15              ; save the message pointer
    LOADI R15 ERR_STR      ; Load error string from eof
    CALL  PRINT_STR        ; print string
    POP   R15              ; restore message pointer
    CALL  PRINT_STR        ; print main error message
    CALL  NEWLINE          ; goto newline
    RET

DO_RUN:
    CALL  SKIP_SPACE
    CALL  TOKEN_END
    CALL  PARSE_HEX
    JNZ   R2 RUN_ERR

    ; --- Allowed if in ROM ---
    LOADI R4 0x00000
    JLT   R1 R4 RUN_CHECK_RAM8
    LOADI R4 0x00FFF
    JGT   R1 R4 RUN_CHECK_RAM8
    JMP   RUN_GO

RUN_CHECK_RAM8:
    ; --- Allowed if in RAM8 ---
    LOADI R4 0x01820
    JLT   R1 R4 RUN_CHECK_PMEM
    LOADI R4 0x0581F
    JGT   R1 R4 RUN_CHECK_PMEM
    JMP   RUN_GO

RUN_CHECK_PMEM:
    ; --- Allowed if in PMEM1/PMEM2/EXTPMEM ---
    LOADI R4 0x072C0
    JLT   R1 R4 RUN_ERR
    LOADI R4 0x1F2BF
    JGT   R1 R4 RUN_ERR
    ; fall through to RUN_GO

RUN_GO:
    ; --- Split R1 into high 4 / low 16 ---
    MOV   R6 R1
    MOV   R7 R1
    LOADI R4 0x10000
    DIV   R7 R4              ; R7 = high 4 bits
    MOV   R5 R7
    LOADI R4 0x10000
    MUL   R5 R4              ; R5 = high << 16
    SUB   R6 R5              ; R6 = low 16 bits

    ; --- Push return address (high, then low) ---
    LOADI R5 0
    PUSH  R5                 ; high 4 bits of return address
    LOADI R5 PRINT_PROMPT
    PUSH  R5                 ; low 16 bits of return address

    ; --- Push target (high, then low), then RET into it ---
    PUSH  R7                 ; target high
    PUSH  R6                 ; target low
    RET                      ; → PC = target, stack = [RETURN_HIGH, RETURN_LOW]

RUN_ERR:
    LOADI R15 BADADDR_STR
    CALL  ERR_PRINT
    JMP   PRINT_PROMPT

DO_LOAD:
    ; --- Arg 1: source ---
    CALL  SKIP_SPACE
    CALL  TOKEN_END
    CALL  PARSE_HEX
    JNZ   R2 LOAD_ERR
    PUSH  R1                ; push source      [src]

    ; --- Arg 2: destination ---
    MOV   R15 R13
    CALL  SKIP_SPACE
    CALL  TOKEN_END
    CALL  PARSE_HEX
    JNZ   R2 LOAD_ERR_POP1  ; throw error 1
    PUSH  R1                ; push destination [src, dst]

    ; --- Arg 3: count ---
    MOV   R15 R13
    CALL  SKIP_SPACE
    CALL  TOKEN_END
    CALL  PARSE_HEX
    JNZ   R2 LOAD_ERR_POP2
    MOV   R3 R1             ; R3 = count

    ; --- Unwind: R1 = src, R2 = dst, R3 = count ---
    POP   R2                ; R2 = dst
    POP   R1                ; R1 = src

    ; --- Validate count ---
    JZ    R3 LOAD_ERR

    ; --- Validate destination region and pick mode ---
    ; RAM8?
    LOADI R4 0x01820
    MOV   R5 R2
    JLT   R5 R4 LOAD_CHK_PMEM
    LOADI R4 0x0581F
    MOV   R5 R2
    JGT   R5 R4 LOAD_CHK_PMEM
    ; dst in RAM8: byte copy. Also check dst+count-1 <= RAM8_END.
    LOADI R4 0x0581F
    JMP   LOAD_CHK_RANGE

LOAD_CHK_PMEM:
    ; PMEM1/PMEM2/EXTPMEM
    LOADI R4 0x072C0
    MOV   R5 R2
    JLT   R5 R4 LOAD_CHK_RAM16
    LOADI R4 0x1F2BF
    MOV   R5 R2
    JGT   R5 R4 LOAD_CHK_RAM16
    ; dst in PMEM: byte copy.
    LOADI R4 0x1F2BF
    JMP   LOAD_CHK_RANGE

LOAD_CHK_RAM16:
    ; RAM16?
    LOADI R4 0x05820
    MOV   R5 R2
    JLT   R5 R4 LOAD_ERR
    LOADI R4 0x0591F
    MOV   R5 R2
    JGT   R5 R4 LOAD_ERR
    ; dst in RAM16: count must be even
    MOV   R5 R3
    LOADI R4 1
    AND   R5 R4
    JNZ   R5 LOAD_ERR
    ; Range check: dst + (count/2) - 1 <= 0x0591F
    MOV   R5 R3
    LOADI R4 2
    DIV   R5 R4             ; R5 = count/2 (cells)
    MOV   R6 R2
    ADD   R6 R5
    SUB   R6 R10            ; R6 = dst + cells - 1
    LOADI R4 0x0591F
    JGT   R6 R4 LOAD_ERR
    ; Mode flag: R6 = 0 means byte copy, 1 means pack-into-16
    LOADI R6 1
    JMP   LOAD_EXEC

LOAD_CHK_RANGE:
    ; R4 = region end; check dst + count - 1 <= R4
    MOV   R6 R2
    ADD   R6 R3
    SUB   R6 R10            ; R6 = dst + count - 1
    JGT   R6 R4 LOAD_ERR
    LOADI R6 0              ; byte copy
    ; fall through

LOAD_EXEC:
    ; R1 = src, R2 = dst, R3 = count, R6 = mode (0=byte, 1=pack16)
    JZ    R6 LOAD_BYTES_RAW
    CALL  LOAD_BYTES_PACK16
    JMP   PRINT_PROMPT

LOAD_BYTES_RAW:
    CALL  LOAD_BYTES_RAW_COPY
    JMP   PRINT_PROMPT

LOAD_ERR_POP2:
    POP   R2                ; discard dst
LOAD_ERR_POP1:
    POP   R2                ; discard src
LOAD_ERR:
    LOADI R15 BADADDR_STR
    CALL  ERR_PRINT
    JMP   PRINT_PROMPT

LOAD_BYTES_RAW_COPY:
    JZ    R3 LOAD_BYTES_RAW_DONE
LOAD_BYTES_RAW_LOOP:
    LOAD  R4 R1
    STORE R2 R4
    ADD   R1 R10
    ADD   R2 R10
    SUB   R3 R10
    JNZ   R3 LOAD_BYTES_RAW_LOOP
LOAD_BYTES_RAW_DONE:
    RET

LOAD_BYTES_PACK16:
LOAD_BYTES_PACK16_LOOP:
    LOAD  R4 R1             ; low byte
    ADD   R1 R10
    LOAD  R5 R1             ; high byte
    ADD   R1 R10
    LOADI R6 8
LOAD_BYTES_PACK16_SHIFT:
    SHL   R5 R10
    SUB   R6 R10
    JNZ   R6 LOAD_BYTES_PACK16_SHIFT
    ADD   R4 R5             ; R4 = (high << 8) | low
    STORE R2 R4
    ADD   R2 R10            ; advance dest by ONE cell
    LOADI R6 2
    SUB   R3 R6
    JNZ   R3 LOAD_BYTES_PACK16_LOOP
    RET

; PAL command - change palette
DO_PAL:
    ADD R9 R10             ; Increment palette
    JMP PRINT_PROMPT       ; New prompt

PRINT_PROMPT:
    MOV R14 R0             ; Update R14 with new prompt position

    ; Print the prompt "> "
    LOADI R1 62            ; ASCII '>'
    WCHR R0 R9 R1          ; write '>'
    ADD R0 R10             ; Increment cursor

    LOADI R1 0             ; Null Char
    WCHR R0 R9 R1          ; Write a null char
    ADD R0 R10             ; Increment cursor

    ; Reset the input buffer
    LOADI R11 0x01840      ; Reset buffer pointer to start of 8-bit RAM
    LOADI R12 0            ; Reset buffer length
    JMP UPDATE_STATE       ; Sync state and wait for input

NEWLINE:
    LOADI R6 0x05920       ; Start of character buffer
    MOV R7 R0              ; Current cursor
    SUB R7 R6              ; Get byte offset into char buffer

    LOADI R8 60            ; Screen width
    DIV R7 R8              ; Offset by current row index
    ADD R7 R10             ; Next row index (curr + 1)

    MUL R7 R8              ; Next row index * screen width
    MOV R0 R7              ; Move cursor to resulting offset
    ADD R0 R6              ; Add base address of char buffer

    LOADI R7 0x0627F       ; Last address of character buffer
    JGT R0 R7 NEWLINE_WRAP ; Past end of buffer? Wrap to top
    RET

NEWLINE_WRAP:
    LOADI R0 0x05920       ; Start of character buffer
    RET

; PARSE_HEX: R15 = start, R13 = end (exclusive)
; Returns R1 = value, R2 = 0 (ok) / 1 (error)
; Advances R15. Clobbers R1-R6.
PARSE_HEX:
    MOV   R6 R15             ; R6 = initial start (for "did we consume anything?")
    LOADI R1 0               ; accumulator

    ; --- Optional "0x" / "0X" prefix ---
    MOV   R2 R13
    SUB   R2 R15             ; R2 = remaining length
    LOADI R4 3
    JLT   R2 R4 PARSE_HEX_LOOP   ; too short to have a prefix
    LOAD  R3 R15
    LOADI R4 48              ; '0'
    SUB   R3 R4
    JNZ   R3 PARSE_HEX_LOOP
    ADD   R15 R10            ; skip '0'
    LOAD  R3 R15
    LOADI R4 120             ; 'x'
    SUB   R3 R4
    JZ    R3 PARSE_HEX_SKIP_X
    LOAD  R3 R15
    LOADI R4 88              ; 'X'
    SUB   R3 R4
    JNZ   R3 PARSE_HEX_REWIND
PARSE_HEX_SKIP_X:
    ADD   R15 R10            ; skip 'x' or 'X'
    JMP   PARSE_HEX_LOOP
PARSE_HEX_REWIND:
    SUB   R15 R10            ; undo '0' skip (it was just a leading zero)

PARSE_HEX_LOOP:
    MOV   R2 R13
    SUB   R2 R15
    JZ    R2 PARSE_HEX_DONE     ; end of token
    LOAD  R3 R15             ; R3 = current char

    ; '0'..'9'?
    LOADI R4 48
    MOV   R5 R3
    SUB   R5 R4              ; R5 = R3 - 48
    LOADI R4 10
    JLT   R5 R4 PARSE_HEX_NIBBLE
    ; 'A'..'F'?
    LOADI R4 65
    MOV   R5 R3
    SUB   R5 R4              ; R5 = R3 - 65
    LOADI R4 6
    JLT   R5 R4 PARSE_HEX_UPPER
    ; 'a'..'f'?
    LOADI R4 97
    MOV   R5 R3
    SUB   R5 R4              ; R5 = R3 - 97
    LOADI R4 6
    JLT   R5 R4 PARSE_HEX_LOWER
    JMP   PARSE_HEX_ERR

PARSE_HEX_UPPER:
    LOADI R4 10              ; 'A'(65) - 10 = 55
    ADD   R5 R4              ; R5 = 10..15
    JMP   PARSE_HEX_ACCUM
PARSE_HEX_LOWER:
    LOADI R4 10              ; 'a'(97) - 10 = 87
    ADD   R5 R4              ; R5 = 10..15
    JMP   PARSE_HEX_ACCUM
PARSE_HEX_NIBBLE:
    ; R5 already holds 0..9 from the first SUB
PARSE_HEX_ACCUM:
    LOADI R4 16
    MUL   R1 R4              ; R1 *= 16
    ADD   R1 R5              ; R1 += nibble
    ADD   R15 R10
    JMP   PARSE_HEX_LOOP

PARSE_HEX_DONE:
    MOV   R2 R15
    SUB   R2 R6              ; R2 = chars consumed
    JZ    R2 PARSE_HEX_ERR      ; parsed nothing → error (e.g. bare "0x")
    LOADI R2 0
    RET
PARSE_HEX_ERR:
    LOADI R2 1
    RET

; R15 = pointer to null-terminated string (ROM or RAM).
; Prints each byte at cursor, advancing R0.
; Leaves R15 pointing at the null terminator.
; Clobbers R1. Preserves R0, R9, R10.
PRINT_STR:
    LOAD  R1 R15
    JZ    R1 PRINT_STR_DONE
    WCHR  R0 R9 R1
    ADD   R0 R10
    ADD   R15 R10
    JMP   PRINT_STR
PRINT_STR_DONE:
    RET

; LOAD_BYTES — copy R3 bytes from [R1] (8-bit) to [R2] (16-bit)
; R3 must be even. Advances R1 by R3, R2 by R3/2.
; Clobbers R4, R5, R6. Preserves R1, R2, R3 only on entry.
LOAD_BYTES:
LOAD_BYTES_LOOP:
    LOAD  R4 R1             ; low byte
    ADD   R1 R10
    LOAD  R5 R1             ; high byte
    ADD   R1 R10
    LOADI R6 8
LOAD_BYTES_SHIFT:
    SHL   R5 R10
    SUB   R6 R10
    JNZ   R6 LOAD_BYTES_SHIFT
    ADD   R4 R5             ; R4 = (high << 8) | low
    STORE R2 R4
    ADD   R2 R10            ; advance dest by ONE cell
    LOADI R6 2
    SUB   R3 R6
    JNZ   R3 LOAD_BYTES_LOOP
    RET

; R15 = start of token.
; On return: R15 = start of token (UNCHANGED)
;            R13 = byte AFTER the token (terminator position)
; Clobbers R1, R2. Preserves everything else.
TOKEN_END:
    PUSH  R6                ; save caller's R6 for scan pointer
    MOV   R6 R15            ; R6 = scan = token start
TOKEN_END_LOOP:
    LOAD  R1 R6
    JZ    R1 TOKEN_END_FOUND     ; hit '\0'  -> end of token
    LOADI R2 32
    SUB   R1 R2
    JZ    R1 TOKEN_END_FOUND     ; hit space -> end of token
    ADD   R6 R10                 ; R10 must be 1
    JMP   TOKEN_END_LOOP
TOKEN_END_FOUND:
    MOV   R13 R6                 ; publish result
    POP   R6                     ; restore caller's R6
    RET

; Exit program and rebalance
PROGRAM_EXIT:
    LOADI R1 0x017FF       ; SP dedicated register address
    LOADI R2 0x17FE
    STORE R1 R2            ; reset SP to 0x17FE
    JMP   PRINT_PROMPT     ; (or JMP 0 if you want full re-init)

; Keyboard Lookup Tables
; ID 0-25: A-Z
; ID 26-35: 1-9, 0
; ID 36: Space
; ID 37: Enter (handled above, but mapped to 10 here)
; ID 38-52: F1-F12, Esc, Tab, Caps
; ID 53: Backspace (handled above, but mapped to 8 here)
; ID 54-71: Nav keys, Shifts, Ctrls, Alts
; ID 72-82: Punctuation

TABLE_U:
    .byte 97  ; a
    .byte 98  ; b
    .byte 99  ; c
    .byte 100 ; d
    .byte 101 ; e
    .byte 102 ; f
    .byte 103 ; g
    .byte 104 ; h
    .byte 105 ; i
    .byte 106 ; j
    .byte 107 ; k
    .byte 108 ; l
    .byte 109 ; m
    .byte 110 ; n
    .byte 111 ; o
    .byte 112 ; p
    .byte 113 ; q
    .byte 114 ; r
    .byte 115 ; s
    .byte 116 ; t
    .byte 117 ; u
    .byte 118 ; v
    .byte 119 ; w
    .byte 120 ; x
    .byte 121 ; y
    .byte 122 ; z
    .byte 49  ; 1
    .byte 50  ; 2
    .byte 51  ; 3
    .byte 52  ; 4
    .byte 53  ; 5
    .byte 54  ; 6
    .byte 55  ; 7
    .byte 56  ; 8
    .byte 57  ; 9
    .byte 48  ; 0
    .byte 32  ; space
    .byte 10  ; enter
    .byte 0   ; F1
    .byte 0   ; F2
    .byte 0   ; F3
    .byte 0   ; F4
    .byte 0   ; F5
    .byte 0   ; F6
    .byte 0   ; F7
    .byte 0   ; F8
    .byte 0   ; F9
    .byte 0   ; F10
    .byte 0   ; F11
    .byte 0   ; F12
    .byte 0   ; Escape
    .byte 0   ; Tab
    .byte 0   ; Caps Lock
    .byte 8   ; Backspace
    .byte 0   ; Home
    .byte 0   ; End
    .byte 0   ; PgUp
    .byte 0   ; PgDn
    .byte 0   ; Up
    .byte 0   ; Down
    .byte 0   ; Left
    .byte 0   ; Right
    .byte 0   ; LShift
    .byte 0   ; RShift
    .byte 0   ; LCtrl
    .byte 0   ; RCtrl
    .byte 0   ; LAlt
    .byte 0   ; RAlt
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 96  ; Grave
    .byte 45  ; minus
    .byte 61  ; equals
    .byte 91  ; Square bracket left
    .byte 93  ; Square bracket Right
    .byte 92  ; Backslash
    .byte 59  ; Semicolon
    .byte 39  ; Apostrophe
    .byte 44  ; Comma
    .byte 46  ; Period
    .byte 47  ; Forslash

TABLE_S:
    .byte 65  ; A
    .byte 66  ; B
    .byte 67  ; C
    .byte 68  ; D
    .byte 69  ; E
    .byte 70  ; F
    .byte 71  ; G
    .byte 72  ; H
    .byte 73  ; I
    .byte 74  ; J
    .byte 75  ; K
    .byte 76  ; L
    .byte 77  ; M
    .byte 78  ; N
    .byte 79  ; O
    .byte 80  ; P
    .byte 81  ; Q
    .byte 82  ; R
    .byte 83  ; S
    .byte 84  ; T
    .byte 85  ; U
    .byte 86  ; V
    .byte 87  ; W
    .byte 88  ; X
    .byte 89  ; Y
    .byte 90  ; Z
    .byte 33  ; !
    .byte 64  ; @
    .byte 35  ; #
    .byte 36  ; $
    .byte 37  ; %
    .byte 94  ; ^
    .byte 38  ; &
    .byte 42  ; *
    .byte 40  ; Right parenthesis
    .byte 41  ; Left parenthesis
    .byte 32  ; Space
    .byte 10  ; Enter
    .byte 0   ; F1
    .byte 0   ; F2
    .byte 0   ; F3
    .byte 0   ; F4
    .byte 0   ; F5
    .byte 0   ; F6
    .byte 0   ; F7
    .byte 0   ; F8
    .byte 0   ; F9
    .byte 0   ; F10
    .byte 0   ; F11
    .byte 0   ; F12
    .byte 0   ; Escape
    .byte 0   ; Tab
    .byte 0   ; Caps Lock
    .byte 8   ; Backspace
    .byte 0   ; Home
    .byte 0   ; End
    .byte 0   ; PgUp
    .byte 0   ; PgDn
    .byte 0   ; Up
    .byte 0   ; Down
    .byte 0   ; Left
    .byte 0   ; Right
    .byte 0   ; LShift
    .byte 0   ; RShift
    .byte 0   ; LCtrl
    .byte 0   ; RCtrl
    .byte 0   ; LAlt
    .byte 0   ; RAlt
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 0   ; padding
    .byte 126 ; Tilde
    .byte 95  ; Underscore
    .byte 43  ; Plus symbol
    .byte 123 ; Open  brace
    .byte 125 ; Close brace
    .byte 124 ; Vertical bar
    .byte 58  ; Colon
    .byte 34  ; Quotation marks
    .byte 60  ; Less-than
    .byte 62  ; Greater-than
    .byte 63  ; Question mark

; Command Strings
CMD_ECHO:
    .byte 69  ; 'E'
    .byte 67  ; 'C'
    .byte 72  ; 'H'
    .byte 79  ; 'O'
    .byte 0   ; Null terminator

CMD_FILE:
    .byte 70  ; 'F'
    .byte 73  ; 'I'
    .byte 76  ; 'L'
    .byte 69  ; 'E'
    .byte 0   ; Null terminator

CMD_FORMAT:
    .byte 70  ; 'F'
    .byte 79  ; 'O'
    .byte 82  ; 'R'
    .byte 77  ; 'M'
    .byte 65  ; 'A'
    .byte 84  ; 'T'
    .byte 0   ; Null terminator

CMD_LOAD:
    .byte 76
    .byte 79
    .byte 65
    .byte 68
    .byte 0

UNKNOWN_STR:
    .byte 85  ; U
    .byte 110 ; n
    .byte 107 ; k
    .byte 110 ; n
    .byte 111 ; o
    .byte 119 ; w
    .byte 110 ; n
    .byte 32  ;
    .byte 99  ; c
    .byte 111 ; o
    .byte 109 ; m
    .byte 109 ; m
    .byte 97  ; a
    .byte 110 ; n
    .byte 100 ; d
    .byte 33  ; !
    .byte 0

ERR_STR:
    .byte 69
    .byte 82
    .byte 82
    .byte 58
    .byte 32
    .byte 0

CMD_RUN:
    .byte 82  ; R
    .byte 85  ; U
    .byte 78  ; N
    .byte 0

CMD_PAL:
    .byte 80
    .byte 65
    .byte 76
    .byte 0

BADADDR_STR:
    .byte 66  ; B
    .byte 97  ; a
    .byte 100 ; d
    .byte 32  ;
    .byte 97  ; a
    .byte 100 ; d
    .byte 100 ; d
    .byte 114 ; r
    .byte 101 ; e
    .byte 115 ; s
    .byte 115 ; s
    .byte 33  ; !
    .byte 0
