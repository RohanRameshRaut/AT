#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM 10485760 // 10MB

typedef struct FileWord {
	unsigned short frequency;
	unsigned char len;
} FileWord;

typedef struct Word Word;
struct Word {
	short frequency;
	unsigned char len;
	char* name;
};

typedef struct HeapNode HeapNode;
struct HeapNode {
	Word word;
	int file_index;
};

int compare_words(const void *a, const void *b) {
	Word *recA = (Word *)a;
	Word *recB = (Word *)b;

	if (recA->frequency > recB->frequency) return -1;
	if (recA->frequency < recB->frequency) return 1;
	return 0;
}

void swap_nodes(HeapNode *a, HeapNode *b) {
	HeapNode temp = *a;
	*a = *b;
	*b = temp;
}

void max_heapify(HeapNode heap[], int size, int idx) {
	int largest = idx;
	int left = 2 * idx + 1;
	int right = 2 * idx + 2;

	if (left < size && heap[left].word.frequency > heap[largest].word.frequency)
		largest = left;
	if (right < size && heap[right].word.frequency > heap[largest].word.frequency)
		largest = right;

	if (largest != idx) {
		swap_nodes(&heap[idx], &heap[largest]);
		max_heapify(heap, size, largest);
	}
}

int create_initial_runs_variable(const char *input_file, const char *prefix, int pass, size_t *max_record_bound) {
	FILE *in = fopen(input_file, "rb");
	if (!in) {
		perror("Failed to open input_file for initial runs");
		return 0;
	}

	int max_records = RAM / sizeof(Word);
	if (max_records < 2) max_records = 2;

	Word *buffer = (Word *) malloc(max_records * sizeof(Word));
	if (!buffer) {
		perror("Failed to allocate memory buffer array");
		fclose(in);
		return 0;
	}

	int run_count = 0, is_eof = 0;
	*max_record_bound = sizeof(FileWord);

	while (!is_eof) {
		size_t current_ram_used = 0;
		int read_count = 0;

		while (read_count < max_records) {
			FileWord fw;
			if (fread(&fw, sizeof(FileWord), 1, in) != 1) {
				is_eof = 1;
				break;
			}

			size_t projected_record_size = sizeof(Word) + fw.len + 1;

			if (projected_record_size > *max_record_bound) {
				*max_record_bound = projected_record_size;
			}

			if (current_ram_used + projected_record_size > RAM) {
				if (read_count == 0) {
					printf("Warning: Single record (%zu bytes) exceeding RAM budget.\n", projected_record_size);
				} else {
					fseek(in, -(long)sizeof(FileWord), SEEK_CUR);
					break;
				}
			}

			Word w;
			w.frequency = fw.frequency;
			w.len = fw.len;
			w.name = (char *) malloc(w.len + 1);

			if (!w.name) {
				perror("Dynamic memory allocation failed");
				fseek(in, -(long)sizeof(FileWord), SEEK_CUR);
				is_eof = 1;
				break;
			}

			if (fread(w.name, sizeof(unsigned char), w.len, in) != (size_t)w.len) {
				free(w.name);
				is_eof = 1;
				break;
			}

			w.name[w.len] = '\0';

			current_ram_used += projected_record_size;
			buffer[read_count] = w;
			read_count++;
		}

		if (read_count > 0) {
			qsort(buffer, read_count, sizeof(Word), compare_words);
			char temp_filename[256];
			snprintf(temp_filename, sizeof(temp_filename), "%s_p%d_r%d.dat", prefix, pass, run_count);

			FILE *out = fopen(temp_filename, "wb");
			if (!out) {
				perror("Failed to open temporary run file for writing");
				for (int i = 0; i < read_count; i++) free(buffer[i].name);
				free(buffer);
				fclose(in);
				return run_count;
			}

			for (int i = 0; i < read_count; i++) {
				FileWord fw = { buffer[i].frequency, buffer[i].len };
				fwrite(&buffer[i].len, sizeof(unsigned char), 1, out);
				fwrite(buffer[i].name, sizeof(unsigned char), buffer[i].len, out);

				free(buffer[i].name);
			}

			fclose(out);
			run_count++;
		}
	}

	free(buffer);
	fclose(in);
	return run_count;
}

void merge_k_runs_variable(const char *prefix, int current_pass, int start_run_idx, int num_files_to_merge, int next_pass, int next_run_idx) {
	FILE **fps = (FILE**)malloc(num_files_to_merge * sizeof(FILE*));
	HeapNode *heap = (HeapNode*)malloc(num_files_to_merge * sizeof(HeapNode));
	int heap_size = 0;	

	size_t current_heap_ram = (num_files_to_merge * sizeof(FILE*)) + (num_files_to_merge * sizeof(HeapNode));

	for (int i = 0; i < num_files_to_merge; i++) {
		char temp_filename[256];
		snprintf(temp_filename, sizeof(temp_filename), "%s_p%d_r%d.dat", prefix, current_pass, start_run_idx + i);
		fps[i] = fopen(temp_filename, "rb");
		if (!fps[i]) {
			perror("Failed to open run file for merging");
			exit(EXIT_FAILURE);
		}

		FileWord fw;
		if (fread(&fw, sizeof(FileWord), 1, fps[i]) == 1) {
			size_t record_ram = fw.len + 1 + sizeof(Word);
			if (current_heap_ram + record_ram > RAM) {
				fprintf(stderr, "Fatal: Phase 2 Initialization over budget.\n");
				exit(EXIT_FAILURE);
			}

			Word w;
			w.frequency = fw.frequency;
			w.len = fw.len;
			w.name = (char *) malloc(fw.len + 1);

			if (fread(w.name, sizeof(unsigned char), w.len, fps[i]) == (size_t)w.len) {
				w.name[w.len] = '\0';
				heap[heap_size].word = w;
				heap[heap_size].file_index = i;
				heap_size++;
				current_heap_ram += record_ram;
			} else {
				free(w.name);
			}
		}
	}

	for (int i = (heap_size - 1) / 2; i >= 0; i--) {
		max_heapify(heap, heap_size, i);
	}

	char out_filename[256];
	snprintf(out_filename, sizeof(out_filename), "%s_p%d_r%d.dat", prefix, next_pass, next_run_idx);
	FILE *out = fopen(out_filename, "wb");

	if (!out) {
		perror("Failed to open next pass target stream");
		exit(EXIT_FAILURE);
	}

	while (heap_size > 0) {
		HeapNode root = heap[0];

		FileWord fw = { root.word.frequency, root.word.len };
		fwrite(&root.word.len, sizeof(unsigned char), 1, out);
		fwrite(root.word.name, sizeof(unsigned char), root.word.len, out);

		size_t freed_ram = root.word.len + 1 + sizeof(Word);
		free(root.word.name);
		current_heap_ram -= freed_ram;

		FileWord next_fw;
		if (fread(&next_fw, sizeof(FileWord), 1, fps[root.file_index]) == 1) {
			size_t next_record_ram = next_fw.len + 1 + sizeof(Word);

			Word next_word;
			next_word.frequency = next_fw.frequency;
			next_word.len = next_fw.len;
			next_word.name = (char *) malloc(next_fw.len + 1);

			if (fread(next_word.name, sizeof(unsigned char), next_word.len, fps[root.file_index]) == (size_t)next_word.len) {
				next_word.name[next_word.len] = '\0';
				heap[0].word = next_word;
				current_heap_ram += next_record_ram;
			} else {
				free(next_word.name);
				heap[0] = heap[heap_size - 1];
				heap_size--;
			}
		} else {
			heap[0] = heap[heap_size - 1];
			heap_size--;
		}

		if (heap_size > 0) {
			max_heapify(heap, heap_size, 0);
		}
	}

	fclose(out);
	for (int i = 0; i < num_files_to_merge; i++) {
		fclose(fps[i]);
		char temp_filename[256];
		snprintf(temp_filename, sizeof(temp_filename), "%s_p%d_r%d.dat", prefix, current_pass, start_run_idx + i);
		remove(temp_filename);
	}
	free(fps);
	free(heap);
}

void external_merge_sort(const char *input_file, const char *output_file, const char *prefix) {
	int current_pass = 0;
	size_t max_record_bound = 0;

	int run_count = create_initial_runs_variable(input_file, prefix, current_pass, &max_record_bound);

	if (run_count == 0) {
		FILE *empty_out = fopen(output_file, "wb");
		if (empty_out) fclose(empty_out);
		return;
	}

	size_t safe_budget = (RAM * 9) / 10;
	int dynamic_k_way = (int)(safe_budget / max_record_bound);

	if (dynamic_k_way < 2) dynamic_k_way = 2;
	if (dynamic_k_way > 1024) dynamic_k_way = 1024;

	while (run_count > 1) {
		int next_pass = current_pass + 1;
		int next_run_idx = 0;

		for (int i = 0; i < run_count; i += dynamic_k_way) {
			int files_to_merge = (run_count - i < dynamic_k_way) ? (run_count - i) : dynamic_k_way;
			merge_k_runs_variable(prefix, current_pass, i, files_to_merge, next_pass, next_run_idx);
			next_run_idx++;
		}
		run_count = next_run_idx;
		current_pass = next_pass;
	}

	char last_file[256];
	snprintf(last_file, sizeof(last_file), "%s_p%d_r0.dat", prefix, current_pass);
	remove(output_file);
	if (rename(last_file, output_file) != 0) {
		perror("Failed to export final sorted dataset file");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char** argv) {
	if (argc != 2) return 1;
	const char *input_file = argv[1];
	const char *output_file = "final_sorted_decreasing.dat";
	const char *temp_prefix = "temp_var_run";

	external_merge_sort(input_file, output_file, temp_prefix);

	FILE *verify = fopen(output_file, "rb");
	if (verify) {
		printf("Verified Decreasing Sorted Structure Data Output:\n");
		unsigned char len;
		while (fread(&len, sizeof(unsigned char), 1, verify) == 1) {
			char *name = (char*)malloc(len + 1);
			if (fread(name, sizeof(unsigned char), len, verify) == (size_t)len) {
				name[len] = '\0';
				printf("Name Length: %2d | Name String: %s\n",len, name);
			}
			free(name);
		}
		fclose(verify);
	}
	return 0;
}
