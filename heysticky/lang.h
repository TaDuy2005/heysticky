// Language (TASK-14, PLAN §13): every text on the Sticky is written TR("English", "Tiếng Việt") where it is used.
// No hardware here, so the host tests can include it.
#pragma once
inline bool g_vi = false;  // Settings › Language & Font › System Language (flash "lang" = "vi")
#define TR(en, vi) (g_vi ? (vi) : (en))
