#ifndef Main_h
#define Main_h
#include "sprites.c"
#include <MAKERphone.h>

typedef enum {
	APP_PHONE = 0,
	APP_CONTACTS,
	APP_MESSAGES,
	APP_SETTINGS,
	APP_CLOCK,
	APP_FLASHLIGHT,
    APP_FAKESETTINGS,
    // Keep at the end
    APP_COUNT,
} Apps;

extern MAKERphone mp;
extern int backgroundColors[7];
extern String backgroundColorsNames[7];
extern String titles[APP_COUNT];
// extern Oscillator *osc;
extern int textPointer;
extern StaticJsonBuffer<capacity> jb;
extern String audioFiles[100];
extern uint16_t audioCount;

void menuDrawBox(String text, uint8_t i, int32_t y);
int8_t menu(const char* title, String* items, uint8_t length);
void menuDrawCursor(uint8_t i, int32_t y);
String readSerial();
uint16_t countSubstring(String string, String substring);
void callNumber(String number);
int16_t audioPlayerMenu(const char* title, String* items, uint16_t length, uint16_t index = 0);
void listAudio(const char * dirname, uint8_t levels);
bool startupWizard();
void controlTry();
#endif
