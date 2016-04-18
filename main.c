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

struct line_entry {
	int start, end;
	
};

struct queue {
	struct node {
		int item;
		struct node *next;
	} *head, *tail;

	int size;
};

void push (struct queue *queue, int src) 
{
	struct node *n = calloc(sizeof(struct node), 1);
	
	n->item = (src);
	n->next = NULL;
	
	if (queue->head == NULL)
		queue->head = n;
	else
		queue->tail->next = n;
	
	queue->tail = n;
	queue->size++;
}
int pop (struct queue *queue) 
{
	struct node *head = queue->head;
	int item = 0;
	
	if (queue->size <= 0)
		return item;
	
	item = head->item;
	
	queue->head = head->next;
	queue->size--;
	
	free(head);
	
	return item;
}


int split_lines (struct img_dt src, unsigned char background, struct line_entry **out_lines)
{
	int cur_y = 0;
	int pos = 0, max = src.x * src.y;
	int state = 0;
	int line_no = 0;
	int num_lines = 0;
	
	unsigned char *bg_line = calloc(src.x + 1, sizeof(char));
	
	memset(bg_line, background, src.x);
	
	for (pos = 0; pos < max; ) {
		if (!memcmp(&src.flat[pos], bg_line, src.x)) {
			if (state == 0) {
				state = 1;
			}
		} else {
			if (state == 1) {
				num_lines++;
				state = 0;
			} 
		}
		
		cur_y++;
		pos += src.x;
	}
	
	cur_y = 0;
	state = 0;
	pos = 0;
	
	if (!num_lines)
		return 0;
	
	struct line_entry *lines = calloc(sizeof(struct line_entry), num_lines + 1);
	*out_lines = lines;
	
	for (pos = 0; pos < max; ) {
		if (!memcmp(&src.flat[pos], bg_line, src.x)) {
			if (state == 0) {
				state = 1;
				lines[line_no].start = cur_y;
			} 
		} else {
			if (state == 1) {
				lines[line_no].end = cur_y;
				line_no++;
				state = 0;
			} 
		}
		
		cur_y++;
		pos += src.x;
	}
	
	free(bg_line);
	
	return num_lines;
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
	
	flood_fill(src, index_flat(src, pos, 1, -1), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, -1, 1), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, -1,-1), find, rep, threshold);
	flood_fill(src, index_flat(src, pos, 1,  1), find, rep, threshold);
	
	return 0;
}


int flood_cmp (struct img_dt src, int pos_src, int pos_dst, 
		int *missing_pixels, int threshold)
{
	if (pos_src < 0 || src.used[pos_src] || pos_dst < 0 || src.used[pos_dst])
		return 0;
		
	unsigned char background[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	
	if (memcmp(&src.flat[pos_src], &src.flat[pos_dst], src.pixsz)) {
		if (memcmp(&src.flat[pos_src], background, src.pixsz))
			if (++(*missing_pixels) > threshold)
				return -1;			
		return 0;
	}
	
	src.used[pos_src] = 1;
	src.used[pos_dst] = 1;
	
	flood_cmp(src,  index_flat(src, pos_src, 0, -1), 
			index_flat(src, pos_dst, 0, -1), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, 0,  1), 
			index_flat(src, pos_dst, 0,  1), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, -1, 0), 
			index_flat(src, pos_dst, -1, 0), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, 1,  0), 
			index_flat(src, pos_dst, 1,  0), 
			missing_pixels, threshold);
	
	flood_cmp(src,  index_flat(src, pos_src, 1, -1), 
			index_flat(src, pos_dst, 1, -1), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, -1, 1), 
			index_flat(src, pos_dst, -1, 1), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, -1,-1), 
			index_flat(src, pos_dst, -1,-1), 
			missing_pixels, threshold);
	flood_cmp(src,  index_flat(src, pos_src, 1,  1), 
			index_flat(src, pos_dst, 1,  1), 
			missing_pixels, threshold);
	
	return (*missing_pixels > threshold) ? -1 : 0;
}


int flood_boundaries (struct img_dt src, int pos, unsigned char *find, 
		int *x0, int *y0, int *x1, int *y1)
{
	if (pos < 0 || src.used[pos])
		return -1;
		
	if (!memcmp(&src.flat[pos], find, src.pixsz))
		return -1;	/* find = background */
		
	src.used[pos] = 1;
	
	int cx = (pos % src.x);
	int cy = (pos / src.x);
	
	if (cx > *x1)		*x1 = cx;
	if (cx < *x0)		*x0 = cx;
	if (cy > *y1)		*y1 = cy;
	if (cy < *y0)		*y0 = cy;

	flood_boundaries(src, index_flat(src, pos, 0, -1), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, 0,  1), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, -1, 0), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, 1,  0), find, x0, y0, x1, y1);
	
	flood_boundaries(src, index_flat(src, pos, 1, -1), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, -1, 1), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, -1,-1), find, x0, y0, x1, y1);
	flood_boundaries(src, index_flat(src, pos, 1,  1), find, x0, y0, x1, y1);
	
	return 0;
}


int fill_lines (struct img_dt src, struct line_entry *lines, int num_lines, 
		unsigned char *background, unsigned char *rep, int **letter_pos)
{
	int last_mid_line, mid_line, i, pos;
	int end = (src.x * src.y);
	struct queue letter_queue;
	int num_letters = 0;
	
	memset(&letter_queue, 0, sizeof(struct queue));
	
	mid_line = last_mid_line = 0;
	
	for (i = 0; i < num_lines; i++) {
		last_mid_line = mid_line + (10 * src.x);
		mid_line = src.x * ((lines[i].end + lines[i].start) / 2);
		
		memset(&src.flat[mid_line], 0x40, src.x);

		for (pos = last_mid_line; pos < mid_line; pos += src.pixsz)
			if (!flood_fill(src, pos, background, rep, 127))
				push(&letter_queue, pos);
				
		for (pos = (last_mid_line - (9 * src.x)); pos < last_mid_line; 
							pos += src.pixsz)
			if (!flood_fill(src, pos, background, rep, 127))
				push(&letter_queue, pos);
	}
	
	for (pos = mid_line + src.x; pos < end; pos += src.pixsz)
		if (!flood_fill(src, pos, background, rep, 127))
			push(&letter_queue, pos);

	num_letters = letter_queue.size;
	
	*letter_pos = calloc(sizeof(int), letter_queue.size + 1);
	i = 0;
	
	while (letter_queue.size) 
		(*letter_pos)[i++] = pop(&letter_queue);
	
	return num_letters;
}

int main (int argc, const char **argv)
{
	struct img_dt src;
	unsigned char background[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	unsigned char rep[4] = {0x20, 0x99, 0x99, 0x99};
	
	if (alloc_img_from_file("/Users/nobody1/Desktop/test.png", &src, 0))
		return -1;

	src.used = calloc(sizeof(char), src.x * src.y);
	int i, j, pos = 0;
	
	struct line_entry *lines;
	int num_lines = split_lines(src, 0xFF, &lines);
	
	for (i = 0; i < num_lines; i++)
		printf("line %d: row %d - %d\n", i, lines[i].start, lines[i].end);
	
	int *letters;
	int num_letters;
	
	num_letters = fill_lines(src, lines, num_lines, background, rep, &letters);
	
	for (i = 0; i < num_letters; i++) {
		pos = letters[i];
		
		printf("letter at pos %d\n", pos);
			src.flat[pos+0] = 0x90;
			src.flat[pos+1] = 0x10;
			src.flat[pos+2] = 0x10;
	}
	
	for (i = 0; i < num_letters; i++) {
		for (j = 0; j < num_letters; j++) {
			if (i == j)
				continue;
			int missing_pix = 0;
			
			memset(src.used, 0, src.x * src.y);
			
			if (!flood_cmp(src, letters[i], letters[j], &missing_pix, 100))
				printf("letter %d = letter %d\n", i, j);
		}
	
		if (j == num_letters)
			printf("done searching for ind %d\n", i);
			
		int x0 = src.x, y0 = src.y, x1 = 0, y1 = 0;
		memset(src.used, 0, src.x * src.y);
		
		flood_boundaries (src, letters[i], background, &x0, &y0, &x1, &y1);
		
		printf("(%d, %d) -> (%d, %d)\n", x0, y0, x1, y1);
	}
	
	free(lines);
	free(letters);
	
	if (stbi_write_png("/Users/nobody1/Desktop/out.png", src.x / src.pixsz, src.y, 
			   src.pixsz, src.flat, src.x) < 0)
		printf("error writing\n");
	
	free(src.flat);
	free(src.used);
	
	return 0;
}
