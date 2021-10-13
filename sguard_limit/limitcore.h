#pragma once
#include <Windows.h>

// wndproc button command
#define IDM_PERCENT90       203
#define IDM_PERCENT95       204
#define IDM_PERCENT99       205
#define IDM_PERCENT999      206
#define IDM_STOPLIMIT       207


// limit module (sington)
class LimitManager {

private:
	static LimitManager    limitManager;

private:
	LimitManager();
	~LimitManager()                                  = default;
	LimitManager(const LimitManager&)                = delete;
	LimitManager(LimitManager&&)                     = delete;
	LimitManager& operator= (const LimitManager&)    = delete;
	LimitManager& operator= (LimitManager&&)         = delete;

public:
	static LimitManager&   getInstance();

public:
	volatile bool          limitEnabled;
	volatile DWORD         limitPercent;

public:
	void     hijack();
	void     enable();                      // [if & only if] g_Mode choose that mode, enable switch works.
	void     disable();
	void     setPercent(DWORD percent);     // setxxx is designed to trigger enable.
	void     wndProcAddMenu(HMENU hMenu);   // append menu structure. used in wndProc callback.
};