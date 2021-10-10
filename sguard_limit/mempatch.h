#pragma once


bool initializePatchModule();
void memoryPatch(DWORD);
void enablePatch();
void disablePatch();