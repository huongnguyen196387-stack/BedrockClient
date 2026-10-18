#pragma once
typedef unsigned int EGLBoolean; typedef void* EGLDisplay; typedef void* EGLSurface; typedef void* EGLContext; typedef int EGLint;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_DRAW 0x3059
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
EGLDisplay eglGetCurrentDisplay(); EGLSurface eglGetCurrentSurface(EGLint); EGLContext eglGetCurrentContext();
EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint*);
