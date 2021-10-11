#pragma once

struct patchSwitches_t {
	bool patchDelayExecution     = false;
	bool patchResumeThread       = true;
	bool patchQueryVirtualMemory = true;
};

bool initializePatchModule();
void memoryPatch(DWORD);
void enablePatch();
void disablePatch();