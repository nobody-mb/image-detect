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

int alloc_img_from_file (const char *name, struct img_dt *ptr, int expect_size)
{
	unsigned char *buf = stbi_load(name, &ptr->x, &ptr->y, &ptr->pixsz, 0);
	
	ptr->x *= ptr->pixsz;
	
	ptr->flat = malloc(ptr->y * ptr->x);
	memcpy(ptr->flat, buf, ptr->y * ptr->x);
	
    	stbi_image_free(buf);
	
	fprintf(stderr, "alloc image %s (%d x %d) pixsz = %d\n", 
		name, ptr->x, ptr->y, ptr->pixsz);
	
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

int flood_fill (struct img_dt src, int pos, unsigned char *find, 
		unsigned char *rep, int threshold)
{
	if (pos < 0 || src.used[pos])
		return -1;
	
	int i, total = 0;
	for (i = 0; i < src.pixsz; i++) {
		if (src.flat[pos + i] > find[i])
			total += (src.flat[pos + i] - find[i]);
		else
			total += (find[i] - src.flat[pos + i]);
	}

	if (total <= (threshold * src.pixsz)) {
		memcpy(&src.flat[pos], find, src.pixsz);
		return -1;
	}
	
	src.used[pos] = 1;
	
	memcpy(&src.flat[pos], rep, src.pixsz);
	
	flood_fill(src, index_flat(src, pos, 0, -1), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, 0,  1), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, -1, 0), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, 1,  0), find, rep, threshold);
	
	return 0;
}

int main (int argc, const char **argv)
{
	struct img_dt src;
	unsigned char background[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	unsigned char rep[4] = {0x20, 0x99, 0x99, 0x99};
	
	if (alloc_img_from_file("/Users/nobody1/Desktop/test.png", &src, 0))
		return -1;
	
	int end = (src.x * src.y);
	src.used = calloc(sizeof(char), src.x * src.y);
	int pos = 0;
	
	for (pos = 0; pos < end; pos += src.pixsz)
		flood_fill(src, pos, background, rep, 96);
	
	if (stbi_write_png("/Users/nobody1/Desktop/out.png", src.x / src.pixsz, src.y, 
			   src.pixsz, src.flat, src.x) < 0)
		printf("error writing\n");
	
	free(src.flat);
	free(src.used);
	
	return 0;
}
