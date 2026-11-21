#pragma once

// WiFi builds: the GPS track page on port 80 (http://<device ip>/).
// Elsewhere both calls do nothing.
void trackWebBegin();
void trackWebLoop();
