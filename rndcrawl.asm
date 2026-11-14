; rndcrawl.asm - crawls across the character buffer, writing random characters,
; writes DONE to the screen, wipes, then loops infinitely.
START:
    LOADI R0, 0x05920   ; cursor (starts at top left)
    LOADI R1, 0         ; default glyph
    LOADI R9, 3         ; palette 3
    LOADI R10, 1        ; general increment
    LOADI R15, 96       ; glyph offset

LOOP:
    LOADI R11, 0x0627F  ; last cell of char buffer
    JGT R0, R11, RESET  ; if R0 > end, move to reset
    RND R1, 2           ; R1 in [0..3]
    ADD R1, R15         ; R1 in [96..99]
    WCHR R0, R9, R1
    ADD R0, R10
    CALL DELAY
    JMP LOOP

RESET:
    JZ R9, END          ; if palette already 0, go to END
    LOADI R0, 0x05920   ; reset cursor to top-left
    SUB R9, R10         ; palette -= 1
    JMP LOOP

END:
    ; Compute cursor near middle of screen, 32 cells back
    LOADI R12, 0x05920  ; CHRBUF_BASE
    LOADI R11, 0x0627F  ; CHRBUF_END
    LOADI R5, 1
    SUB   R11, R12      ; R11 = end - base
    SHR   R11, R5       ; R11 = (end - base) / 2
    LOADI R5, 32        ; back off 32 cells
    SUB   R11, R5
    ADD   R11, R12      ; R11 = absolute address
    MOV   R0, R11       ; cursor = R11
    LOADI R1, 68        ; 'D'
    LOADI R2, 79        ; 'O'
    LOADI R3, 78        ; 'N'
    LOADI R4, 69        ; 'E'
    LOADI R9, 2         ; palette 2
    WCHR R0, R9, R1
    ADD R0, R10
    WCHR R0, R9, R2
    ADD R0, R10
    WCHR R0, R9, R3
    ADD R0, R10
    WCHR R0, R9, R4
    ; One last crawl: blank the screen from the top
    LOADI R0, 0x05920   ; cursor back to top-left
    LOADI R1, 0         ; null glyph (blank)
ENDLOOP:
    LOADI R11, 0x0627F  ; last cell of char buffer
    JGT R0, R11, DONE   ; if R0 > end, done
    WCHR R0, R9, R1
    ADD R0, R10
    CALL DELAY
    JMP ENDLOOP
DONE:
    LOADI R0, 0x05920
    JMP START

DELAY:
    LOADI R11, 3200
DELAY_LOOP:
    LOADI R12, 1
    SUB R11, R12
    JNZ R11, DELAY_LOOP
    RET
