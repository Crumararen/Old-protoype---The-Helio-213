#include <stdio.h>
#include <string.h>

int main() {
    unsigned char font[4096];
    FILE *f = fopen("memory/font.bin", "rb");
    if (!f) return 1;
    fread(font, 1, 4096, f);
    fclose(f);

    // Shift all characters right by 1 (32 bytes per character)
    memmove(font + 32, font, 4096 - 2048);

    // Clear the first character (32 bytes of zeros)
    memset(font, 0, 2048);

    f = fopen("memory/font.bin", "wb");
    if (!f) return 1;
    fwrite(font, 1, 4096, f);
    fclose(f);

    printf("Font shifted successfully!\n");
    return 0;
}