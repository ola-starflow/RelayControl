#ifndef SLG47011_CONFIG_DATA_H
#define SLG47011_CONFIG_DATA_H

#include <stdint.h>
#include <stddef.h>

#define SLG47011_RAM_CONFIG_START_ADDR   0x0000u
#define SLG47011_RAM_CONFIG_END_ADDR     0x2249u
#define SLG47011_RAM_CONFIG_MAX_LEN      0x224Au

extern const uint8_t slg47011_ram_config[];
extern const size_t slg47011_ram_config_len;

#endif /* SLG47011_CONFIG_DATA_H */
