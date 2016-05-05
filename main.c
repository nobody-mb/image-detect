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

#define MAX_MIN_CMP 999999999
#define CMP_BUF_SIZ 512

struct queue {
	struct node {
		int item;
		struct node *next;
	} *head, *tail;

	int size;
};
	 
struct ocr_data {
	struct img_dt {
		int x, y, pixsz;
		unsigned char **img;
		unsigned char *flat;
		unsigned char *used;
	} src;

	unsigned char background[4];
	unsigned char rep[4];
	int tolerance;
	
	struct line_entry {
		int start;
		int end;
	} *lines;
	int num_lines;
	
	int *letters;
	int num_letters;
	
	struct letter_data {
		int buf_pos;
		int x0, y0;
		int x1, y1;
	} *glyphs;

	const char *src_path;
	const char *let_path;
	const char *dst_path;
};

int alloc_img_from_file (const char *name, struct img_dt *ptr, int expect_size)
{
	unsigned char *buf = stbi_load(name, &ptr->x, &ptr->y, &ptr->pixsz, 0);
	
	ptr->x *= ptr->pixsz;
	
	ptr->flat = calloc(ptr->y, ptr->x + 1);
	memcpy(ptr->flat, buf, ptr->y * ptr->x);
	
    	stbi_image_free(buf);

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
	int num_lines, i;

	if (!(num_lines = split_state(src.x * src.y, src.flat, background, 
					src.x, NULL)))
		return 0;
	
	*out_lines = calloc(sizeof(struct line_entry), num_lines + 1);
	
	split_state(src.x * src.y, src.flat, background, src.x, *out_lines);
	
	for (i = 0; i < num_lines; i++)
		printf("line %d: row %d - %d\n", i, 
			(*out_lines)[i].start, 
			(*out_lines)[i].end);
	
	return num_lines;
}

void push_adjacent (struct img_dt src, struct queue *cq, int pos)
{
	int s, d;
	
	for (s = -1; s <= 1; s++)
		for (d = -1; d <= 1; d++)
			if (s || d)
				push(cq, index_flat(src, pos, s, d));
}

int32_t abs_no_branch (int32_t src)
{
	uint32_t tmp = (uint32_t)src;
	
	tmp >>= 1;
	tmp <<= 1;
	
	return (int32_t)(tmp & 0x7FFFFFFF);
}

int flood_fill (struct ocr_data *ocr, int pos)
{
	struct queue cq;
	int i, total = 0, flag = 0;
	
	memset(&cq, 0, sizeof(struct queue));
	
	push(&cq, pos);
	
	while (cq.size) {
		total = 0;
		if ((pos = pop(&cq)) < 0 || ocr->src.used[pos])
			continue;	
			
		
		for (i = 0; i < ocr->src.pixsz; i++) {
			if (ocr->src.flat[pos + i] > ocr->background[i])
				total += (ocr->src.flat[pos + i] - 
					  ocr->background[i]);
			else
				total += (ocr->background[i] - 
					  ocr->src.flat[pos + i]);
		}

		if (total <= (ocr->tolerance * ocr->src.pixsz)) {
			memcpy(&ocr->src.flat[pos], ocr->background, ocr->src.pixsz);
			continue;
		}
		
		flag = 1;
		ocr->src.used[pos] = 1;
	
		memcpy(&ocr->src.flat[pos], ocr->rep, ocr->src.pixsz);
		
		push_adjacent(ocr->src, &cq, pos);
	}
	
	return (!flag) ? -1 : 0;
}


int flood_boundaries (struct img_dt src, int pos, unsigned char *find, 
		int *x0, int *y0, int *x1, int *y1)
{				
	struct queue cmp_queue;
	int cx, cy;
	
	memset(src.used, 0, src.x * src.y);
	
	*x0 = src.x;
	*y0 = src.y;
	*x1 = *y1 = 0;

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
		
		push_adjacent(src, &cmp_queue, pos);
	}
	
	while (cmp_queue.size)
		pop(&cmp_queue);
	
	return 0;
}

int cmp_block_letter (struct img_dt src, struct letter_data l1, 
			struct img_dt ltr, struct letter_data l2, 
			unsigned char *background, int x, int y)
{
	int l2_x = l2.x1 - l2.x0;
	int l2_y = l2.y1 - l2.y0;
	int mask, total_missed = 0;
	
	if (src.pixsz == 3) 		mask = 0x00FFFFFF;
	else if (src.pixsz == 4) 	mask = 0xFFFFFFFF;
	else 				return -1;
	
	if (!l2_y) 			return -1;
	
	unsigned int bkg = (*(unsigned int *)background) & mask;
	
	unsigned char *sptr = src.flat + ((y + l1.y0) * src.x) + (x + l1.x0);
	unsigned char *lptr = ltr.flat + (l2.y0 * ltr.x) + l2.x0;

	#if 1
	unsigned int cmp_data[6] = { mask, bkg, src.pixsz, ltr.x, src.x, l2_x };

	asm volatile (
		"movl 	0(%%rbx), %%r9d\n"
		"movl 	4(%%rbx), %%r10d\n"
		"movsl	8(%%rbx), %%r11\n"
		"movsl 	12(%%rbx), %%r12\n"
		"movsl 	16(%%rbx), %%r13\n"
		"movsl 	20(%%rbx), %%r14\n"
		"xorq 	%%rax, %%rax\n"
		"jmp 	cbl_loopcheck\n"
	"cbl_loop:\n"
		"xorq 	%%r8, %%r8\n"	/* j = 0 */
		"jmp 	cbl_not_missed\n"
	"cbl_rowloop:\n"
		"movl 	(%%rsi, %%r8), %%ebx\n"	/* snt = (uint)sptr[j] */
		"movl 	(%%rdi, %%r8), %%ecx\n"	/* lnt = (uint)lptr[j] */
		"addq 	%%r11, %%r8\n"	/* j += pixsz */
		"andl 	%%r9d, %%ebx\n"	/* snt &= mask */
		"andl 	%%r9d, %%ecx\n"	/* lnt &= mask */
		"cmpl 	%%ebx, %%ecx\n"	/* if snt == lnt */
		"je 	cbl_not_missed\n"
		"cmpl 	%%ebx, %%r10d\n"	/* if snt == bkg */
		"je 	cbl_missed\n"
		"cmpl 	%%ecx, %%r10d\n"	/* if lnt == bkg */
		"jne 	cbl_not_missed\n"
	"cbl_missed:\n"
		"incq 	%%rax\n"		/* total_missed++ */
	"cbl_not_missed:\n"	
		"cmpq 	%%r8, %%r14\n"	/* j < l2_x */
		"jge 	cbl_rowloop\n"
	"cbl_rowend:\n"
		"addq 	%%r12, %%rdi\n"	/* lptr += ltr.x */
		"addq 	%%r13, %%rsi\n"	/* sptr += src.x */
	"cbl_loopcheck:\n"
		"decq 	%%rdx\n"		/* l2_y-- */
		"jnz 	cbl_loop\n"
		: "=a" (total_missed)	
		: "b" (cmp_data), "d" (l2_y), "S" (sptr), "D" (lptr)
	        : "r8", "r9", "r10", "r11", "r12", 
	          "r13", "r14", "rcx", "rbx");
	        
	#else
	int i, j;
	unsigned int snt, lnt;
	
	while (l2_y-- > 0) {
		for (j = 0; j < l2_x; j += src.pixsz) {
			snt = ((*(unsigned int *)(sptr + j)) & mask);
			lnt = ((*(unsigned int *)(lptr + j)) & mask);

			if ((snt != lnt) && (snt == bkg || lnt == bkg))
				++total_missed;		
		}
		
		lptr += ltr.x;
		sptr += src.x;
	}
	#endif
	return total_missed; 
}

int cmp_letters_multiple (struct img_dt src, struct letter_data l1, 
			struct img_dt ltr, struct letter_data l2, 
			unsigned char *background)
{
	int l1_x, l1_y, l2_x, l2_y;	
	int total_missed = 0;
	int x = 0, y = 0, dx, dy;
	int min_tot_missed = MAX_MIN_CMP;	

	if ((l1_x = l1.x1 - l1.x0) < 0 ||
	    (l1_y = l1.y1 - l1.y0) < 0 ||
	    (l2_x = l2.x1 - l2.x0) < 0 ||
	    (l2_y = l2.y1 - l2.y0) < 0 ||
	    ((l1_x > l2_x) && (l1_y < l2_y)) || 
	    ((l1_x < l2_x) && (l1_y > l2_y)))
		return -1;
		
	if ((l1_x < l2_x) && (l1_y < l2_y)) 
		return cmp_letters_multiple(ltr, l2, src, l1, background);

	if ((dx = l1_x - l2_x) > (4 * src.pixsz) || (dy = l1_y - l2_y) > 4)
		return -1;

	do {
		x = 0;
		do {
			total_missed = cmp_block_letter(src, l1, ltr, 
					l2, background, x, y);	
	
			if (total_missed < min_tot_missed)
				min_tot_missed = total_missed;
		} while ((x += src.pixsz) <= dx);
	} while (++y <= dy);	
	 
	return min_tot_missed;
}

const char *get_last (const char *dir, unsigned char cmp)
{
	const char *start = dir;
	
	while (*dir)
		dir++;
	
	while (dir >= start && *dir != cmp)
		dir--;
	
	return (dir == start) ? NULL : dir;
}


int save_letter (struct img_dt src, struct letter_data l1, 
		 int index, const char *path)
{				
	int l1_x = l1.x1 - l1.x0;
	int l1_y = l1.y1 - l1.y0;

	int i, retn = 0;
	char wpath[1024];
	unsigned char *buf = calloc(sizeof(char), (l1_x * l1_y) + 1);
	
	memset(wpath, 0, sizeof(wpath));
	snprintf(wpath, sizeof(wpath), "%s/%d.png", path, index);
	
	for (i = 0; i < l1_y; i++)
		memcpy(buf+(i*l1_x), &src.flat[((l1.y0+i)*src.x)+l1.x0], l1_x);

	retn = stbi_write_png(wpath, (l1_x / src.pixsz), l1_y, src.pixsz, 
				buf, l1_x);

	free(buf);
	
	return retn;
}

int find_closest (const char *path, int *min_val, char *min_buf, 
		struct img_dt src, struct letter_data glyph, 
		unsigned char *background)
{
	DIR *dr;
	char wpath[1024];
	struct letter_data l2;
	struct img_dt letter_dt;
	struct stat st;
	struct dirent *ds;
	int k;
	
	memset(min_buf, 0, CMP_BUF_SIZ);
	
	if ((dr = opendir(path)) == NULL)
		return -1;
		
	while ((ds = readdir(dr))) {
		memset(&letter_dt, 0, sizeof(struct img_dt));
		memset(&l2, 0, sizeof(struct letter_data));
		
		if (*(ds->d_name) == '.') 
			continue;
		
		memset(wpath, 0, sizeof(wpath));
		snprintf(wpath, sizeof(wpath), "%s/%s", path, ds->d_name);
		
		if (stat(wpath, &st) < 0 || 
		    S_ISDIR(st.st_mode) ||
		    alloc_img_from_file(wpath, &letter_dt, 0)) 
			continue;

		l2.x0 = l2.y0 = 0;
		l2.x1 = letter_dt.x;
		l2.y1 = letter_dt.y;
		
		k = cmp_letters_multiple(src, glyph, letter_dt, l2, background);
		
		if (k >= 0 && k <= *min_val) {
			*min_val = k;
			strncpy(min_buf, ds->d_name, CMP_BUF_SIZ);
		}
		
		free(letter_dt.flat);
	}
	
	closedir(dr);

	return 0;
}

int read_letters (struct ocr_data ocr)
{
	int i = 0, min_val;
	char *letter_buf = calloc(sizeof(char), ocr.num_letters * 3);
	char *letter_ptr = letter_buf;
	char min_buf[CMP_BUF_SIZ];
	
	for (i = 0; i < ocr.num_letters; i++) {
		min_val = MAX_MIN_CMP;
		memset(min_buf, 0, sizeof(min_buf));
		
		fprintf(stderr, "[%s]: letter #%d at pos %d: (%d, %d) -> (%d, %d)\n", 
			__func__, i, ocr.letters[i], 
			ocr.glyphs[i].x0, ocr.glyphs[i].y0, 
			ocr.glyphs[i].x1, ocr.glyphs[i].y1);
		
		if (find_closest(ocr.let_path, &min_val, min_buf, ocr.src, 
				 ocr.glyphs[i], ocr.background) < 0)
			printf("couldn't find closest\n");

		printf("closest match for %d: %s (%d off)\n", i, min_buf, min_val);
		
		if (min_val >= MAX_MIN_CMP) {
			if ((ocr.glyphs[i].x1 - ocr.glyphs[i].x0) <= 1 || 
			    (ocr.glyphs[i].y1 - ocr.glyphs[i].y0) <= 1)
				printf("bad dimensions\n");
			else if (save_letter(ocr.src, ocr.glyphs[i], i, 
				 ocr.let_path) < 0)
				printf("error saving %d\n", i);
		}
		
		if (i > 0 && (ocr.glyphs[i].x0 - ocr.glyphs[i - 1].x1) >= 
						(4 * ocr.src.pixsz))
			*letter_ptr++ = ' ';
			
		if (i > 0 && (ocr.glyphs[i].y0 - ocr.glyphs[i - 1].y1) >= (4))
			*letter_ptr++ = '\n';
			
		*letter_ptr++ = *((char *)get_last(min_buf, '/') + 1);
		
		if (*(letter_ptr - 1) == 0)
			*(letter_ptr - 1) = '?';
	}
	
	printf("[%s]: detected letters %s\n", __func__, letter_buf);
	free(letter_buf);
	
	return 0;
}

int measure_letters (struct ocr_data *ocr)
{
	int i; 		/* WARNING: function allocates memory to ocr->glyphs */

	if (!ocr || !(ocr->glyphs = calloc(sizeof(struct letter_data), 
					   ocr->num_letters + 1)))
		return -1;
	
	for (i = 0; i < ocr->num_letters; i++) {
		ocr->glyphs[i].buf_pos = ocr->letters[i];

		memset(&ocr->src.flat[ocr->letters[i]], 0, ocr->src.pixsz);

		flood_boundaries(ocr->src, ocr->letters[i], ocr->background, 
				&(ocr->glyphs[i].x0), &(ocr->glyphs[i].y0), 
				&(ocr->glyphs[i].x1), &(ocr->glyphs[i].y1));

		ocr->glyphs[i].x1 += ocr->src.pixsz;
		ocr->glyphs[i].y1 += 1;
	}

	return 0;
}

void queue_push_loop (struct ocr_data *ocr, struct queue *letter_queue, 
			int start, int end)
{
	int pos;
	
	for (pos = start; pos < end; pos += ocr->src.pixsz)
		if (!flood_fill(ocr, pos))
			push(letter_queue, pos);
}

int fill_lines (struct ocr_data *ocr)
{
	int srcx = ocr->src.x;	/* WARNING: allocates memory to ocr->letters */
	int end = ocr->src.x * ocr->src.y;
	struct queue letter_queue;
	int i, mid_line = 0, last_mid = 0;
	
	memset(&letter_queue, 0, sizeof(struct queue));
	memset(ocr->src.used, 0, ocr->src.x * ocr->src.y);

	for (i = 0; i < ocr->num_lines; i++) {
		last_mid = mid_line + (10 * srcx);
		mid_line = srcx * ((ocr->lines[i].end + ocr->lines[i].start) / 2);
		
		memset(&ocr->src.flat[mid_line], 0x40, srcx);
		
		queue_push_loop(ocr, &letter_queue, last_mid, mid_line);
		queue_push_loop(ocr, &letter_queue, (last_mid - (9 * srcx)), last_mid);	
	}
	queue_push_loop(ocr, &letter_queue, mid_line + srcx, end);

	ocr->num_letters = letter_queue.size;
	ocr->letters = calloc(sizeof(int), letter_queue.size + 1);

	for (i = 0; letter_queue.size; i++) 
		ocr->letters[i] = pop(&letter_queue);
	
	return ocr->num_letters;
}

int ocr_init (struct ocr_data *ocr, const char *src_path, const char *let_path, 
		const char *dst_path, int tolerance, unsigned int background,
		unsigned int rep)
{
	memset(ocr, 0, sizeof(struct ocr_data));
	
	ocr->src_path = src_path;
	ocr->let_path = let_path;
	ocr->dst_path = dst_path;
	
	ocr->tolerance = tolerance;
	
	if (alloc_img_from_file(ocr->src_path, &(ocr->src), 0))
		return -1;
		
	memcpy(ocr->background, &background, sizeof(ocr->background));
	memcpy(ocr->rep, &rep, sizeof(ocr->rep));	
		
	ocr->src.used = calloc(ocr->src.x, ocr->src.y);
	
	return 0;
}

int ocr_save_and_free (struct ocr_data *ocr)
{
	int retn = stbi_write_png(ocr->dst_path, ocr->src.x / ocr->src.pixsz,
				ocr->src.y, ocr->src.pixsz, 
				ocr->src.flat, ocr->src.x);

	free(ocr->lines);
	free(ocr->letters);
	free(ocr->glyphs);
	free(ocr->src.flat);
	free(ocr->src.used);

	return retn;
}

int main (int argc, const char **argv)
{
	struct ocr_data ocr;

	if (ocr_init(&ocr, "/Users/nobody1/Desktop/allchars.png",
			   "/Users/nobody1/Desktop/letters", 
			   "/Users/nobody1/Desktop/out.png",
			   127, 0xFFFFFFFF, 0xFF999920) < 0) { 
		printf("error initializing ocr\n");
		return -1;
	}

	ocr.num_lines = split_lines(ocr.src, 0xFF, &(ocr.lines));

	if (fill_lines(&ocr) < 0)
		printf("error detecting lines\n");

	if (measure_letters(&ocr) < 0)
		printf("error measuring\n");

	if (read_letters(ocr) < 0)
		printf("error reading\n");
		
	if (ocr_save_and_free(&ocr) < 0)
		printf("error writing\n");
	
	return 0;
}
