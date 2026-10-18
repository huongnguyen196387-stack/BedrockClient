#pragma once
#include <cstdint>
#include <cstddef>
struct AInputQueue; struct AInputEvent;
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_UP 1
int32_t AInputEvent_getType(const AInputEvent*);
int32_t AMotionEvent_getAction(const AInputEvent*); float AMotionEvent_getX(const AInputEvent*, size_t); float AMotionEvent_getY(const AInputEvent*, size_t);
int64_t AMotionEvent_getEventTime(const AInputEvent*);
int32_t AKeyEvent_getKeyCode(const AInputEvent*); int32_t AKeyEvent_getAction(const AInputEvent*);
void AInputQueue_finishEvent(AInputQueue*, AInputEvent*, int);
