#include "ktuilib.h"
static const unsigned notes[] = {262, 330, 392, 523, 392, 330, 294, 349, 440, 587, 440, 349};
static void tone(unsigned n) { syscall(KOS_SYS_SOUND_TONE, n, 150, 0, 0); }
#if SOUND_APP == 1
int main(int argc, char** argv) { (void)argc; (void)argv; tui_clear(TUI_DESKTOP); tui_bar(0, "KMusa - Japanese motif generator"); tui_box(8,4,64,14,"Moonlit garden",TUI_BORDER); tui_text(12,8,"Enter/P: generate and play pentatonic motif",TUI_STATUS); tui_text(12,10,"Q: quit",TUI_STATUS); tui_present(79,24); for (;;) { int k=tui_key(1); if(k=='q'||k==27)return 0; if(k=='p'||k=='\n')for(int i=0;i<12;++i)tone(notes[i]); } }
#elif SOUND_APP == 2
int main(int argc, char** argv) { (void)argc; (void)argv; tui_clear(TUI_DESKTOP); tui_bar(0,"KListen - TUI audio player"); tui_box(8,4,64,14,"Playlist",TUI_BORDER); tui_text(12,8,"1 Moonlit garden theme",TUI_STATUS); tui_text(12,10,"Enter: play | Q: quit",TUI_STATUS); tui_present(79,24); for (;;) { int k=tui_key(1); if(k=='q'||k==27)return 0; if(k=='1'||k=='\n')for(int i=0;i<12;++i)tone(notes[i]); } }
#else
int main(int argc, char** argv) { (void)argc; (void)argv; u32 info[2]={0}; syscall(KOS_SYS_SOUND_INFO,(u32)info,0,0,0); for(;;){ tui_clear(TUI_DESKTOP); tui_bar(0,"SoundCtl - audio service and volume"); tui_box(12,5,56,12,"PC speaker audio",TUI_BORDER); tui_text(18,8,"Up/Down volume | T test | Q quit",TUI_STATUS); tui_text(18,10,"Volume:",TUI_KEYWORD); tui_number(28,10,info[0],TUI_NUMBER); tui_text(32,10,"%",TUI_STATUS); tui_text(18,12,info[1]?"Audio service: online":"Audio service: offline",info[1]?TUI_SUCCESS:TUI_ERROR); tui_present(79,24); int k=tui_key(1); if(k=='q'||k==27)return 0; if(k==TUI_UP&&info[0]<100)++info[0]; if(k==TUI_DOWN&&info[0])--info[0]; if(k=='t')tone(440); if(k==TUI_UP||k==TUI_DOWN)syscall(KOS_SYS_SOUND_VOLUME,info[0],0,0,0); } }
#endif
