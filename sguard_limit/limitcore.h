#pragma once
#include <Windows.h>
#include <atomic>


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
	std::atomic<bool>      limitEnabled;
	std::atomic<DWORD>     limitPercent;
	std::atomic<bool>      useKernelMode;

public:
	void     hijack();
	void     enable();                      // [if & only if] g_Mode choose that mode, enable switch works.
	void     disable();
	void     setPercent(DWORD percent);     // setxxx is designed to trigger enable.
};