#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdint.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct img_dt {
	int x, y, pixsz;
	unsigned char **img;
	unsigned char *flat;
	unsigned char *used;
};

int alloc_img_from_file (const char *fname, struct img_dt *ptr, int expect_size)
{
	unsigned char *buffer = stbi_load(fname, &ptr->x, &ptr->y, &ptr->pixsz, 0);
	
	ptr->x *= ptr->pixsz;
	
	ptr->flat = malloc(ptr->y * ptr->x);
	memcpy(ptr->flat, buffer, ptr->y * ptr->x);
	
    	stbi_image_free(buffer);
	
	fprintf(stderr, "alloc image %s (%d x %d) pixsz = %d\n", 
		fname, ptr->x, ptr->y, ptr->pixsz);
	
	return 0;
}

int index_flat (struct img_dt src, int pos, int dx, int dy)
{
	int cy = (pos / src.x);
	int cx = (pos % src.x);
	
	if (cy < 0 || cy > src.y || cx < 0 || cx > src.x)
		return -1;
	
	return (src.x * (cy + dy)) + (cx + (dx * src.pixsz));
}

int test_pixel (struct img_dt src, int pos, unsigned char *cmp, int dx, int dy)
{
	int new_ind;

	if ((new_ind = index_flat(src, pos, dx, dy)) < 0)
		return -1;
	
	printf("cmp old %d to new %d\n", pos, new_ind);
	
	return !!(memcmp(&src.flat[new_ind], cmp, src.pixsz));
}

int main (int argc, const char **argv)
{
	struct img_dt src;
	
	if (alloc_img_from_file("/Users/nobody1/Desktop/test.png", &src, 0))
		return -1;
	
	int end = (src.x * src.y);
	int state = 0;
	src.used = calloc(sizeof(char), src.x * src.y);
	int pos = 0;
	int up, dn, left, right, center;
	
	unsigned char background[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	
	int x1 = 0, y1 = 0;
	int x0 = src.x, y0 = src.y;
	
	int cx = 0, cy = 0;
	
	while (pos < end) {
		if ((center = test_pixel(src, pos, background, 0, 0)))
			state = 1;
		
		up = test_pixel(src, pos, background, 0, -1);
		dn = test_pixel(src, pos, background, 0, 1);
		left = test_pixel(src, pos, background, -1, 0);
		right = test_pixel(src, pos, background, 1, 0);

		if (!up && !dn && !left && !right && !center) {
			if (state)
				break;
		} else {
			if (center && x0 > cx)
				x0 = cx;
			if (center && x1 < cx)
				x1 = cx;
			
			if (center && y0 > cy)
				y0 = cy;
			if (center && y1 < cy)
				y1 = cy;
			
			if (left && x0 > (cx - 1))
				x0 = cx - 1;
			
			if (right && x1 < (cx + 1))
				x1 = cx + 1;
			
			if (dn && y0 > (cy - 1))
				y0 = cy - 1;
			
			if (up && y1 < (cy + 1))
				y1 = cy + 1;
		}
			
		pos += src.pixsz;
		src.used[pos] = 1;
		
		printf("(%d, %d): %d, up %d dn %d left %d right %d\n", cx, cy, center, up, dn, left, right);
		
		cx += src.pixsz;
		if (cx >= src.x) {
			cx = 0;
			cy++;
		}
	}
	
	
	free(src.flat);
	free(src.used);
	
	printf("(%d, %d) -> (%d, %d)\n", x0, y0, x1, y1);
	
	return 0;
}

/*
 
 
 (-1, 10) -> (111, 12)
 (183 x 30)

 
 
 */
