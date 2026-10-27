/* Draw-list transforms must scale geometry without changing effect inputs. */
#include "../src/render/draw.h"
#include "test.h"

static void test_shader_transform(void) {
	const float times[] = { 0.5f, 600.25f, 3599.75f };
	const float scales[][2] = { { 1.05f, 0.87f }, { 0.96f, 1.04f }, { 0.7f, 0.7f } };
	const rvn_box_t limit = { -1000, -1000, 1000, 1000 };
	for (size_t t = 0; t < sizeof(times) / sizeof(times[0]); t++) {
		RDrawInstance original = {
			.rect = { 10, 20, 110, 70 }, .clip = { 0, 0, 200, 200 },
			.radii = { 4, 8, 12, 16 }, .uv = { 1, 0.25f, 3, -1 },
			.color = 0x12345678u, .border_color = 0x87654321u,
			.border_width = times[t], .flags = R_PRIM_SHADER | R_INSTANCE_NO_POINTER
		};
		RDrawInstance instances[2] = { original, original };
		instances[1].flags = R_PRIM_RECT;
		instances[1].border_width = 2;
		rvn_drawlist_t list = { .instances = instances, .instance_count = 2 };
		float width = 100, height = 50, length_scale = 1;
		/* Button squash, release overshoot, then a parent pop transition. */
		for (size_t i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
			float sx = scales[i][0], sy = scales[i][1];
			rvn_drawlist_transform(&list, 0, 0, sx, sy, 60, 45, 3, -2, limit);
			width *= sx; height *= sy; length_scale *= fminf(sx, sy);
			CHECK(fabsf(instances[0].rect[2] - instances[0].rect[0] - width) < 0.001f);
			CHECK(fabsf(instances[0].rect[3] - instances[0].rect[1] - height) < 0.001f);
			CHECK(fabsf(instances[0].radii[0] - 4 * length_scale) < 0.001f);
			CHECK(fabsf(instances[1].border_width - 2 * length_scale) < 0.001f);
			CHECK(instances[0].border_width == times[t]); /* Seconds are not pixels. */
			CHECK(!memcmp(instances[0].uv, original.uv, sizeof(original.uv)));
			CHECK(instances[0].color == original.color && instances[0].border_color == original.border_color);
			CHECK(instances[0].flags == original.flags);
		}
	}
}

int main(void) {
	if (!test_init(0)) return 1;
	RUN(test_shader_transform);
	Rshutdown();
	return test_report("test_draw");
}
