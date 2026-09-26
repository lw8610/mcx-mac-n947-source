#ifndef MCX_MAC_UMAC_DISK_OVERLAY_H_
#define MCX_MAC_UMAC_DISK_OVERLAY_H_

#include <stdint.h>

#define UMAC_DISK_OVERLAY_SECTORS 64U

void umac_disk_overlay_init(const uint8_t *image, unsigned int image_size);
int umac_disk_overlay_read(void *ctx, uint8_t *data, unsigned int offset,
			   unsigned int len);
int umac_disk_overlay_write(void *ctx, uint8_t *data, unsigned int offset,
			    unsigned int len);
unsigned int umac_disk_overlay_used(void);
unsigned int umac_disk_overlay_write_count(void);
unsigned int umac_disk_overlay_error_count(void);

#endif
