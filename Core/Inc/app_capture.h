#ifndef APP_CAPTURE_H
#define APP_CAPTURE_H
#include <stdint.h>
/* Optional debugger request; serviced by A through the normal save state machine. */
#define APP_CAPTURE_SAVE_REQUEST 0x53415645U
extern volatile uint32_t appCaptureSaveRequest;
void AppCapture_Init(void);
void AppCapture_Process(void);
/* Audio ISR only; no SD or network work here. count is exclusive end sample. */
void AppCapture_Audio(const volatile int16_t *pcm, uint32_t frames,
                      uint64_t count, uint64_t localNs, uint32_t epoch);
/* Main loop only. First recognized signature starts the retained round. */
void AppCapture_Trigger(void);
void AppCapture_Log(const char *format, ...);
int AppCapture_Busy(void);
const char *AppCapture_Text(void);
const char *AppCapture_Detail(void);
#endif
