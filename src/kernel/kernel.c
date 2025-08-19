void main() {
    volatile unsigned short* video_memory = (volatile unsigned short*)0xB8000;

    for (int i = 0; i < 80 * 25; i++) {
        video_memory[i] = (0x0F << 8) | ' ';
    }

    const char* message = "KintsugiOS Kernel loaded successfully!";

    int pos = 0;
    for (int i = 0; message[i] != '\0'; i++) {
        video_memory[pos + i] = (0x0F << 8) | message[i];
    }

    while(1);
}
