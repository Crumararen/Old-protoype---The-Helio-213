; demo.asm
.ORG 0x0EAC0
START:
    LOADI R1 72
    WCHR  R0 R9 R1
    HALT