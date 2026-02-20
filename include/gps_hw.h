#pragma once

void hwinit();

// User Equivalent Range Error :

#if GPS_MODEL == UBLOX
#define GPS_UERE 3 // Meter
#else
#define GPS_UERE 4 // Meter, konserativ, ≈ 5 m User Equivalent Range Error bei kommerziellen gps erwartbar
#endif
