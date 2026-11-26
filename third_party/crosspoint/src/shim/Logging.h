// HeySticky shim: CrossPoint logging compiled out (the Sticky's USB serial shows nothing anyway).
#pragma once
#include <string>
#define LOG_ERR(origin, format, ...) ((void)0)
#define LOG_INF(origin, format, ...) ((void)0)
#define LOG_DBG(origin, format, ...) ((void)0)
inline std::string getLastLogs() { return {}; }
inline void clearLastLogs() {}
