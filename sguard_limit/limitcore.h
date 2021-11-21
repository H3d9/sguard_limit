#pragma once
#include <Windows.h>


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
	volatile bool          useKernelMode;

public:
	void     init();
	void     hijack();
	void     enable();                      // [if & only if] g_Mode choose that mode, enable switch works.
	void     disable();
	void     setPercent(DWORD percent);     // setxxx is designed to trigger enable.
};