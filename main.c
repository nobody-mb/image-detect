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
	
	//fprintf(stderr, "alloc image %s (%d x %d) pixsz = %d\n", 
	//	name, ptr->x, ptr->y, ptr->pixsz);
	
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

int split_state (int max, unsigned char *flat, unsigned char background, 
		int row_len, struct line_entry *lines)
{
	int pos = 0, cur_y = 0, num_lines = 0, state = 0;
	unsigned char *bg_line = calloc(max + 1, sizeof(char));
	
	memset(bg_line, background, row_len);
	
	for (pos = 0; pos < max; pos += row_len, cur_y++) {
		if (!memcmp(&flat[pos], bg_line, row_len)) {
			if (state) continue;
			state = 1;
			if (lines) lines[num_lines].start = cur_y;
		} else {
			if (!state) continue;
			if (lines) lines[num_lines].end = cur_y;
			num_lines++;
			state = 0;
		}
	}
	
	free(bg_line);

	return num_lines;
}

int split_lines (struct img_dt src, unsigned char background, 
		struct line_entry **out_lines)
{
	int num_lines;

	if (!(num_lines = split_state(src.x * src.y, src.flat, background, src.x, NULL)))
		return 0;
	
	*out_lines = calloc(sizeof(struct line_entry), num_lines + 1);
	
	split_state(src.x * src.y, src.flat, background, src.x, *out_lines);
	
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
		if ((pos = pop(&cmp_queue)) < 0 || src.used[pos])
			continue;	
			
		if (!memcmp(&src.flat[pos], find, src.pixsz))
			continue;
			
		src.used[pos] = 1;
	
		cx = (pos % src.x);
		cy = (pos / src.x);
		
		if (cx >= *x1)	*x1 = cx;
		if (cx <= *x0)	*x0 = cx;
		if (cy >= *y1)	*y1 = cy;
		if (cy <= *y0)	*y0 = cy;
		
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

int cmp_letters (struct img_dt src, struct letter_data l1, struct letter_data l2)
{
	int l1_x = l1.x1 - l1.x0;
	int l1_y = l1.y1 - l1.y0;
	int l2_x = l2.x1 - l2.x0;
	int l2_y = l2.y1 - l2.y0;
	int total_missed = 0;
	
	if (l1_x < 0 || l1_y < 0 || l2_x != l1_x || l2_y != l1_y)
		return -1;

	int i, j;
	for (i = 0; i < l1_y; i++) {
		for (j = 0; j < l1_x; j++) {
			int l1c = l1.buf_pos + (i * src.x) + j;
			int l2c = l2.buf_pos + (i * src.x) + j;
			
			if (src.flat[l1c] != src.flat[l2c])
				++total_missed;
		
		}
	}
	
	return total_missed;
}

int cmp_letters_multiple (struct img_dt src, struct letter_data l1, 
			struct img_dt letter, struct letter_data l2)
{
	int l1_x = l1.x1 - l1.x0;
	int l1_y = l1.y1 - l1.y0;
	int l2_x = l2.x1 - l2.x0;
	int l2_y = l2.y1 - l2.y0;	
	int total_missed = 0;

	if (l1_x < 0 || l1_y < 0 || l2_x != l1_x || l2_y != l1_y)
		return -1;
	
	int letter_pos = 0;
	int i, j;
	
	for (i = l1.y0; i < l1.y1; i++)
		for (j = l1.x0; j < l1.x1; j++)
			if (src.flat[(i * src.x) + j] != 0xFF && 
			 src.flat[(i * src.x) + j] != 
			 letter.flat[((i - l1.y0) * src.x) + (j - l1.x0)])
				++total_missed;

	
				 
	return total_missed;
}


int save_letter (struct img_dt src, struct letter_data l1, int index)
{				
	int l1_x = l1.x1 - l1.x0;
	int l1_y = l1.y1 - l1.y0;

	int i, retn = 0;
	char wpath[1024];
	unsigned char *buf = calloc(sizeof(char), (l1_x * l1_y) + 1);
	
	memset(wpath, 0, sizeof(wpath));
	snprintf(wpath, sizeof(wpath), "/Users/nobody1/Desktop/letters/%d.png", index);
	
	for (i = 0; i < l1_y; i++)
		memcpy(buf + (i * l1_x), &src.flat[((l1.y0 + i) * src.x) + l1.x0], l1_x);		
		
	retn = stbi_write_png(wpath, l1_x / src.pixsz, l1_y, src.pixsz, buf, l1_x);

	free(buf);
	
	return retn;
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
		glyphs[i].buf_pos = pos = letters[i];

		printf("letter at pos %d\n", pos);
		src.flat[pos+0] = 0x90;
		src.flat[pos+2] = src.flat[pos+1] = 0x10;

		flood_boundaries (src, letters[i], background, 
				&(glyphs[i].x0), &(glyphs[i].y0), 
				&(glyphs[i].x1), &(glyphs[i].y1));

		printf("(%d, %d) -> (%d, %d)\n", glyphs[i].x0, glyphs[i].y0, 
			glyphs[i].x1, glyphs[i].y1);
			
		if (save_letter(src, glyphs[i], i) < 0)
			printf("error saving %d\n", i);
	}
	
	
	
	for (i = 0; i < num_letters; i++) {
		DIR *dr;
		char wpath[1024];
		struct letter_data l2;
		struct img_dt letter_dt;
		struct stat st;
		struct dirent *ds;
		
		if ((dr = opendir("/Users/nobody1/Desktop/letters")) == NULL)
			return -1;
			
		while ((ds = readdir(dr))) {
			memset(&letter_dt, 0, sizeof(struct img_dt));
			memset(&l2, 0, sizeof(struct letter_data));
			if (*(ds->d_name) == '.') continue;

			memset(wpath, 0, sizeof(wpath));
			snprintf(wpath, sizeof(wpath), 
				"/Users/nobody1/Desktop/letters/%s", ds->d_name);
			
			if (stat(wpath, &st) < 0) continue;
				
			if (S_ISDIR(st.st_mode)) continue;
			
			if (alloc_img_from_file(wpath, &letter_dt, 0)) continue;
				
			l2.x0 = l2.y0 = 0;
			l2.x1 = letter_dt.x;
			l2.y1 = letter_dt.y;
				
			int k = cmp_letters_multiple(src, glyphs[i], letter_dt, l2);
			
			if (k >= 0 && k < (4 * src.pixsz)) 
				printf("letter %d = %s\n", i, ds->d_name);
			
			free(letter_dt.flat);
		}
		
		closedir(dr);
		
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
