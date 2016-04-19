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

struct letter_data {
	int buf_pos;
	int x0, y0;
	int x1, y1;
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
	struct line_entry *lines;
	
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
	
	if (!num_lines) {
		free(bg_line);
		return 0;
	}
	
	*out_lines = lines = calloc(sizeof(struct line_entry), num_lines + 1);

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

int flood_cmp (struct img_dt src, int pos_src, int pos_dst, int threshold)
{
	unsigned char background[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	struct queue cmp_queue;
	int missing_pixels = 0;
	int s, d;
	
	if (pos_src < 0 || src.used[pos_src] || pos_dst < 0 || src.used[pos_dst])
		return 0;
		
	memset(&cmp_queue, 0, sizeof(struct queue));
	
	push(&cmp_queue, pos_src);
	push(&cmp_queue, pos_dst);
	
	while (cmp_queue.size) {
		pos_src = pop(&cmp_queue);
		pos_dst = pop(&cmp_queue);
		
		if (src.used[pos_src] || src.used[pos_dst])
			continue;

		if (!memcmp(&src.flat[pos_src], background, src.pixsz))
			continue;
		
		
		src.used[pos_src] = 1;
		src.used[pos_dst] = 1;
		
		if (memcmp(&src.flat[pos_src], &src.flat[pos_dst], src.pixsz)) {
			if (++missing_pixels > threshold)
				break;
		}

		for (s = -1; s <= 1; s++) {
			for (d = -1; d <= 1; d++) {
				if (!s && !d) continue;
				push(&cmp_queue,index_flat(src, pos_src, s, d));
				push(&cmp_queue,index_flat(src, pos_dst, s, d));
			}
		}
	}
	
	while (cmp_queue.size)
		pop(&cmp_queue);
	
	return (missing_pixels > threshold) ? -1 : 0;
}


int flood_boundaries (struct img_dt src, int pos, unsigned char *find, 
		int *x0, int *y0, int *x1, int *y1)
{				
	struct queue cmp_queue;
	int s, d, cx, cy;
	
	memset(src.used, 0, src.x * src.y);
	
	*x0 = src.x;
	*y0 = src.y;
	*x1 = *y1 = 0;
	
	if (pos < 0 || src.used[pos])
		return -1;
		
	memset(&cmp_queue, 0, sizeof(struct queue));

	push(&cmp_queue, pos);
	
	while (cmp_queue.size) {
		pos = pop(&cmp_queue);
		
		if (pos < 0 || src.used[pos] || !memcmp(&src.flat[pos], find, src.pixsz))
			continue;	
			
		src.used[pos] = 1;
	
		cx = (pos % src.x);
		cy = (pos / src.x);
		
		if (cx > *x1)	*x1 = cx;
		if (cx < *x0)	*x0 = cx;
		if (cy > *y1)	*y1 = cy;
		if (cy < *y0)	*y0 = cy;
		
		for (s = -1; s <= 1; s++)
			for (d = -1; d <= 1; d++)
				if (s || d)
					push(&cmp_queue, index_flat(src, pos, s, d));
	}
	
	while (cmp_queue.size)
		pop(&cmp_queue);
	
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

	struct letter_data *glyphs = calloc(sizeof(struct letter_data), num_letters + 1);
	
	for (i = 0; i < num_letters; i++) {
		pos = letters[i];
		
		printf("letter at pos %d\n", pos);
			src.flat[pos+0] = 0x90;
			src.flat[pos+1] = 0x10;
			src.flat[pos+2] = 0x10;

		flood_boundaries (src, letters[i], background, 
				&(glyphs[i].x0), &(glyphs[i].y0), 
				&(glyphs[i].x1), &(glyphs[i].y1));
		
		printf("(%d, %d) -> (%d, %d)\n", glyphs[i].x0, glyphs[i].y0, 
			glyphs[i].x1, glyphs[i].y1);
		
	}
	
	free(lines);
	free(letters);
	free(glyphs);
	
	if (stbi_write_png("/Users/nobody1/Desktop/out.png", src.x / src.pixsz, src.y, 
			   src.pixsz, src.flat, src.x) < 0)
		printf("error writing\n");
	
	free(src.flat);
	free(src.used);
	
	return 0;
}
