#ifndef MCX_MAC_EMULATOR_UMAC_MOUSE_CLIP_H_
#define MCX_MAC_EMULATOR_UMAC_MOUSE_CLIP_H_

#include <stdint.h>

/* Bound the pending quadrature backlog without changing the movement angle
 * by clipping X and Y independently. The single common scale is applied to
 * the accumulated vector, so rapid diagonal movement stays diagonal.
 */
static inline void umac_mouse_clip_add(int *pending_x, int *pending_y,
				       int dx, int dy, int limit)
{
	int64_t x = (int64_t)*pending_x + dx;
	int64_t y = (int64_t)*pending_y + dy;
	int64_t abs_x = x < 0 ? -x : x;
	int64_t abs_y = y < 0 ? -y : y;
	int64_t magnitude = abs_x > abs_y ? abs_x : abs_y;

	if (limit > 0 && magnitude > limit) {
		int64_t round = magnitude / 2;

		x = x < 0 ? -((-x * limit + round) / magnitude) :
			(x * limit + round) / magnitude;
		y = y < 0 ? -((-y * limit + round) / magnitude) :
			(y * limit + round) / magnitude;
	}
	*pending_x = (int)x;
	*pending_y = (int)y;
}

#endif /* MCX_MAC_EMULATOR_UMAC_MOUSE_CLIP_H_ */
