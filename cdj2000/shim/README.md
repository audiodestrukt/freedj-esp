# shim: ESP-IDF headers for code shared from the ESP32 decks

Just enough of FreeRTOS, esp_log, esp_timer, esp_netif and lwIP's sockets for
freedj-esp files to compile unchanged for the CDJ-2000 (first
`p4/deck/main/prolink.cpp`). The sockets run on `main/netstack.cpp`; tasks
are the cooperative ones in `main/coop.cpp` (so a "critical section" is a
no-op: nothing switches tasks except a yield).
