#pragma once
#include <stdint.h>
#include <stdbool.h>

void nxs_client_init(const char *peer_address_str, const uint8_t pin[4]);

bool nxs_connect(void);
bool nxs_disconnect(void);
bool nxs_up(void);
bool nxs_down(void);

bool nxs_connect_up_disconnect(void);
bool nxs_connect_down_disconnect(void);