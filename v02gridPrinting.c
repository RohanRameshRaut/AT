#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ncurses.h>

// 24-byte Matrix Header
typedef struct Matrix {
    unsigned int row, col, x, y, start_arr_row, start_arr_col;
} Matrix;

// Cell struct
typedef struct Cell {
    unsigned char count;
    unsigned int *word_ids; // Dictionary offsets
} Cell;

// --- Global RAM & File State ---
Matrix matrix;
int *row_map = NULL;
int *col_map = NULL;

// Active Buffer Window in RAM (Screen Size + 50% Scroll Margins)
Cell **ram_buffer = NULL;
int buf_start_r = -1, buf_end_r = -1;
int buf_start_c = -1, buf_end_c = -1;
int buf_rows = 0, buf_cols = 0;

char swap_filepath[256];
char original_csv_path[256];

// Helper: Decode VByte integer from memory buffer
unsigned int decode_vbyte_from_buf(const unsigned char *buf, int *offset) {
    unsigned char b1 = buf[(*offset)++];
    if ((b1 & 0x80) == 0) {
        return b1;
    } else if ((b1 & 0xC0) == 0x80) {
        unsigned char b2 = buf[(*offset)++];
        return ((b1 & 0x3F) << 8) | b2;
    } else {
        unsigned char b2 = buf[(*offset)++];
        unsigned char b3 = buf[(*offset)++];
        return ((b1 & 0x3F) << 16) | (b2 << 8) | b3;
    }
}

// Executes 'cp -f src dst' using fork() + execvp()
int copy_file_fork(const char *src_path, const char *dst_path) {
    pid_t pid = fork();

    if (pid < 0) {
        return 0; // Fork failed
    } 
    else if (pid == 0) {
        // Child process: execute cp
        char *args[] = {"cp", "-f", (char *)src_path, (char *)dst_path, NULL};
        execvp("cp", args);
        _exit(127); // Exec failed
    } 
    else {
        // Parent process: wait for child
        int status;
        if (waitpid(pid, &status, 0) == -1) return 0;
        return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

// Create .swap file on startup using fork() + execvp()
int init_swap_file(const char *csv_path) {
    snprintf(original_csv_path, sizeof(original_csv_path), "%s", csv_path);
    snprintf(swap_filepath, sizeof(swap_filepath), "%s.swap", csv_path);

    if (!copy_file_fork(csv_path, swap_filepath)) return 0;

    // Read header & row/col index maps from swap file into RAM
    FILE *swap_fp = fopen(swap_filepath, "rb");
    if (!swap_fp) return 0;

    if (fread(&matrix, sizeof(Matrix), 1, swap_fp) != 1) {
        fclose(swap_fp);
        return 0;
    }

    row_map = malloc(matrix.row * sizeof(int));
    col_map = malloc(matrix.col * sizeof(int));

    fseek(swap_fp, matrix.start_arr_row, SEEK_SET);
    fread(row_map, sizeof(int), matrix.row, swap_fp);

    fseek(swap_fp, matrix.start_arr_col, SEEK_SET);
    fread(col_map, sizeof(int), matrix.col, swap_fp);

    fclose(swap_fp);
    return 1;
}

// Write row_map and col_map updates to .swap file
int update_swap_file_maps() {
    FILE *fp = fopen(swap_filepath, "r+b");
    if (!fp) return 0;

    fseek(fp, matrix.start_arr_row, SEEK_SET);
    fwrite(row_map, sizeof(int), matrix.row, fp);

    fseek(fp, matrix.start_arr_col, SEEK_SET);
    fwrite(col_map, sizeof(int), matrix.col, fp);

    fclose(fp);
    return 1;
}

// Save .swap file changes back to original miniCsv file (:w / :wq)
int flush_swap_to_original() {
    update_swap_file_maps(); // Ensure maps are flushed to .swap first
    return copy_file_fork(swap_filepath, original_csv_path);
}

// Free RAM buffer allocation
void free_ram_buffer() {
    if (ram_buffer) {
        for (int r = 0; r < buf_rows; r++) {
            for (int c = 0; c < buf_cols; c++) {
                if (ram_buffer[r][c].word_ids) {
                    free(ram_buffer[r][c].word_ids);
                }
            }
            free(ram_buffer[r]);
        }
        free(ram_buffer);
        ram_buffer = NULL;
    }
    buf_start_r = buf_end_r = buf_start_c = buf_end_c = -1;
    buf_rows = buf_cols = 0;
}

// Fetch on-demand viewport chunk (screen size + 50% scroll padding)
void load_viewport_chunk_to_ram(int vis_top_r, int vis_bot_r, int vis_top_c, int vis_bot_c) {
    int vis_r_count = vis_bot_r - vis_top_r + 1;
    int vis_c_count = vis_bot_c - vis_top_c + 1;

    int pad_v = vis_r_count / 2; // 50% margin vertical
    int pad_h = vis_c_count / 2; // 50% margin horizontal

    int target_start_r = (vis_top_r - pad_v < 0) ? 0 : vis_top_r - pad_v;
    int target_end_r = (vis_bot_r + pad_v >= (int)matrix.row) ? (int)matrix.row - 1 : vis_bot_r + pad_v;

    int target_start_c = (vis_top_c - pad_h < 0) ? 0 : vis_top_c - pad_h;
    int target_end_c = (vis_bot_c + pad_h >= (int)matrix.col) ? (int)matrix.col - 1 : vis_bot_c + pad_h;

    // Check if current RAM buffer already encompasses requested region
    if (ram_buffer &&
        target_start_r >= buf_start_r && target_end_r <= buf_end_r &&
        target_start_c >= buf_start_c && target_end_c <= buf_end_c) {
        return; // Current RAM buffer is valid
    }

    // Reallocate RAM buffer for new viewport chunk
    free_ram_buffer();

    buf_start_r = target_start_r;
    buf_end_r = target_end_r;
    buf_start_c = target_start_c;
    buf_end_c = target_end_c;

    buf_rows = buf_end_r - buf_start_r + 1;
    buf_cols = buf_end_c - buf_start_c + 1;

    ram_buffer = malloc(buf_rows * sizeof(Cell *));
    for (int r = 0; r < buf_rows; r++) {
        ram_buffer[r] = malloc(buf_cols * sizeof(Cell));
        for (int c = 0; c < buf_cols; c++) {
            ram_buffer[r][c].count = 0;
            ram_buffer[r][c].word_ids = NULL;
        }
    }

    FILE *fp = fopen(swap_filepath, "rb");
    if (!fp) return;

    // Read cells required for viewport chunk
    for (int r = 0; r < buf_rows; r++) {
        int actual_r = buf_start_r + r;
        int mapped_r = row_map[actual_r];

        for (int c = 0; c < buf_cols; c++) {
            int actual_c = buf_start_c + c;
            int mapped_c = col_map[actual_c];

            int total_cells_before = (mapped_r * matrix.col) + mapped_c;
            fseek(fp, sizeof(Matrix), SEEK_SET);

            // Skip preceding cells on disk
            for (int i = 0; i < total_cells_before; i++) {
                unsigned char len;
                if (fread(&len, sizeof(unsigned char), 1, fp) != 1) break;
                for (int k = 0; k < len; k++) {
                    unsigned char b1;
                    if (fread(&b1, sizeof(unsigned char), 1, fp) != 1) break;
                    if ((b1 & 0x80) == 0) {
                    } else if ((b1 & 0xC0) == 0x80) {
                        fseek(fp, 1, SEEK_CUR);
                    } else if ((b1 & 0xC0) == 0xC0) {
                        fseek(fp, 2, SEEK_CUR);
                    }
                }
            }

            // Read target cell VByte sequence
            unsigned char cell_len;
            if (fread(&cell_len, sizeof(unsigned char), 1, fp) == 1) {
                ram_buffer[r][c].count = cell_len;
                if (cell_len > 0) {
                    ram_buffer[r][c].word_ids = malloc(cell_len * sizeof(unsigned int));
                    for (int k = 0; k < cell_len; k++) {
                        unsigned char b1;
                        if (fread(&b1, sizeof(unsigned char), 1, fp) != 1) break;
                        unsigned int val = 0;
                        if ((b1 & 0x80) == 0) {
                            val = b1;
                        } else if ((b1 & 0xC0) == 0x80) {
                            unsigned char b2;
                            if (fread(&b2, sizeof(unsigned char), 1, fp) != 1) break;
                            val = ((b1 & 0x3F) << 8) | b2;
                        } else if ((b1 & 0xC0) == 0xC0) {
                            unsigned char b2, b3;
                            if (fread(&b2, sizeof(unsigned char), 1, fp) != 1) break;
                            if (fread(&b3, sizeof(unsigned char), 1, fp) != 1) break;
                            val = ((b1 & 0x3F) << 16) | (b2 << 8) | b3;
                        }
                        ram_buffer[r][c].word_ids[k] = val;
                    }
                }
            }
        }
    }
    fclose(fp);
}

// Dynamically computes and builds the cell text directly from sorted_words_file
char *build_cell_text_dynamic(FILE *dict_fp, int target_r, int target_c) {
    if (target_r < buf_start_r || target_r > buf_end_r || 
        target_c < buf_start_c || target_c > buf_end_c) {
        return NULL;
    }

    int r_idx = target_r - buf_start_r;
    int c_idx = target_c - buf_start_c;

    Cell *cell = &ram_buffer[r_idx][c_idx];
    if (cell->count == 0 || !cell->word_ids) return NULL;

    // Step 1: Calculate exact string size from sorted_words_file
    size_t total_str_len = 0;
    for (int k = 0; k < cell->count; k++) {
        unsigned int offset = cell->word_ids[k];
        fseek(dict_fp, offset, SEEK_SET);

        unsigned char word_len = 0;
        if (fread(&word_len, sizeof(unsigned char), 1, dict_fp) == 1) {
            total_str_len += word_len;
        }
    }

    // Account for spaces between words + null terminator
    total_str_len += (cell->count - 1) + 1;

    // Step 2: Dynamically allocate exact string memory
    char *out_buf = malloc(total_str_len);
    if (!out_buf) return NULL;

    // Step 3: Populate buffer with word bytes
    size_t curr_pos = 0;
    for (int k = 0; k < cell->count; k++) {
        unsigned int offset = cell->word_ids[k];
        fseek(dict_fp, offset, SEEK_SET);

        unsigned char word_len = 0;
        if (fread(&word_len, sizeof(unsigned char), 1, dict_fp) == 1) {
            if (k > 0) {
                out_buf[curr_pos++] = ' ';
            }
            if (fread(&out_buf[curr_pos], sizeof(char), word_len, dict_fp) == word_len) {
                curr_pos += word_len;
            }
        }
    }
    out_buf[curr_pos] = '\0';

    return out_buf; // Caller frees returned string
}

void draw_grid(FILE *dict_fp, int cur_r, int cur_c, int top_row, int top_col, int base_cell_width, const char *status_msg) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    clear();

    // Top Status Header Bar
    attron(A_REVERSE);
    mvprintw(0, 0, " [R:%d C:%d] | Map(R:%d C:%d) | Total: %ux%u | RAM Buf: [%d-%d, %d-%d] | ':' Vim | 'q': Quit ", 
            cur_r, cur_c, row_map[cur_r], col_map[cur_c], matrix.row, matrix.col,
            buf_start_r, buf_end_r, buf_start_c, buf_end_c);
    for (int x = getcurx(stdscr); x < max_x; x++) addch(' ');
    attroff(A_REVERSE);

    // Full cell hover preview
    char *hover_buf = build_cell_text_dynamic(dict_fp, cur_r, cur_c);

    mvprintw(1, 0, " FULL VALUE: ");
    attron(A_BOLD);
    printw("%s", hover_buf ? hover_buf : "<EMPTY>");
    attroff(A_BOLD);

    mvhline(2, 0, ACS_HLINE, max_x);

    int start_y = 3;
    int max_vis_rows = max_y - start_y - 3;

    // Dynamic row margin calculation
    int max_row_idx = (matrix.row > 0) ? (int)matrix.row - 1 : 0;
    int row_digits = 1;
    int temp = max_row_idx;
    while (temp >= 10) { row_digits++; temp /= 10; }
    if (row_digits < 3) row_digits = 3;

    int margin_width = row_digits + 2;
    int max_vis_cols = (max_x - margin_width) / (base_cell_width + 1);

    if (max_vis_rows <= 0) max_vis_rows = 1;
    if (max_vis_cols <= 0) max_vis_cols = 1;

    // Fetch required viewport + margin into RAM
    int vis_bot_r = (top_row + max_vis_rows - 1 < (int)matrix.row) ? top_row + max_vis_rows - 1 : (int)matrix.row - 1;
    int vis_bot_c = (top_col + max_vis_cols - 1 < (int)matrix.col) ? top_col + max_vis_cols - 1 : (int)matrix.col - 1;
    load_viewport_chunk_to_ram(top_row, vis_bot_r, top_col, vis_bot_c);

    // Print headers with Hover Expansion
    mvprintw(start_y, 0, "%*s", margin_width, "");
    for (int c = 0; c < max_vis_cols && (top_col + c) < (int)matrix.col; c++) {
        int actual_c = top_col + c;
        int active_w = base_cell_width;

        if (actual_c == cur_c && hover_buf) {
            int hover_len = strlen(hover_buf);
            if (hover_len > active_w) active_w = hover_len;
        }

        printw("| %-*d ", active_w - 2 > 0 ? active_w - 2 : 1, actual_c);
    }
    mvhline(start_y + 1, 0, ACS_HLINE, max_x);

    // Print grid cells with Hover Expansion
    for (int r = 0; r < max_vis_rows && (top_row + r) < (int)matrix.row; r++) {
        int actual_r = top_row + r;
        int screen_y = start_y + 2 + r;
        
        mvprintw(screen_y, 0, "%*d |", row_digits, actual_r);

        for (int c = 0; c < max_vis_cols && (top_col + c) < (int)matrix.col; c++) {
            int actual_c = top_col + c;
            char *cell_str = build_cell_text_dynamic(dict_fp, actual_r, actual_c);
            int text_len = cell_str ? strlen(cell_str) : 0;
            int effective_width = base_cell_width;

            // Expand cell width dynamically on hover
            if (actual_r == cur_r && actual_c == cur_c) {
                if (text_len > effective_width) effective_width = text_len;
            }

            char *display_fmt = malloc(effective_width + 1);
            if (display_fmt) {
                if (text_len > effective_width) {
                    if (effective_width > 3) {
                        strncpy(display_fmt, cell_str, effective_width - 3);
                        display_fmt[effective_width - 3] = '\0';
                        strcat(display_fmt, "...");
                    } else {
                        strncpy(display_fmt, cell_str, effective_width);
                        display_fmt[effective_width] = '\0';
                    }
                } else {
                    memset(display_fmt, ' ', effective_width);
                    if (cell_str) memcpy(display_fmt, cell_str, text_len);
                    display_fmt[effective_width] = '\0';
                }

                if (actual_r == cur_r && actual_c == cur_c) {
                    attron(A_STANDOUT | A_BOLD);
                    printw("%s", display_fmt);
                    attroff(A_STANDOUT | A_BOLD);
                } else {
                    printw("%s", display_fmt);
                }
                free(display_fmt);
            }

            printw("|");
            if (cell_str) free(cell_str);
        }
    }

    if (hover_buf) free(hover_buf);

    if (status_msg && status_msg[0] != '\0') {
        mvprintw(max_y - 1, 0, "%s", status_msg);
    }

    refresh();
}

int handle_vim_command(const char *cmd_buf, int *cur_r, int *cur_c, int *cell_width, char *status_msg) {
    while (*cmd_buf == ' ' || *cmd_buf == ':') cmd_buf++;

    if (strlen(cmd_buf) == 0) return 1;

    // Quit (:q)
    if (strcmp(cmd_buf, "q") == 0 || strcmp(cmd_buf, "quit") == 0) {
        return 0;
    }

    // Write changes from .swap to miniCsv (:w)
    if (strcmp(cmd_buf, "w") == 0 || strcmp(cmd_buf, "write") == 0) {
        if (flush_swap_to_original()) {
            snprintf(status_msg, 256, "Saved changes from .swap to original file.");
        } else {
            snprintf(status_msg, 256, "Error saving to original file.");
        }
        return 1;
    }

    // Write and quit (:wq)
    if (strcmp(cmd_buf, "wq") == 0) {
        flush_swap_to_original();
        return 0;
    }

    // Swap Rows (:swapRow(2, 5) or :swapRow 2 5)
    int r1 = -1, r2 = -1;
    if (sscanf(cmd_buf, "swapRow(%d, %d)", &r1, &r2) == 2 || sscanf(cmd_buf, "swapRow %d %d", &r1, &r2) == 2) {
        if (r1 >= 0 && r1 < (int)matrix.row && r2 >= 0 && r2 < (int)matrix.row) {
            int temp = row_map[r1];
            row_map[r1] = row_map[r2];
            row_map[r2] = temp;
            update_swap_file_maps(); // Write immediately to .swap file
            free_ram_buffer();       // Invalidate RAM cache to reload with new map
            snprintf(status_msg, 256, "Swapped row %d & %d (Saved to .swap file)", r1, r2);
        } else {
            snprintf(status_msg, 256, "Error: Invalid row indices (%d, %d)", r1, r2);
        }
        return 1;
    }

    // Swap Cols (:swapCol(1, 8) or :swapCol 1 8)
    int c1 = -1, c2 = -1;
    if (sscanf(cmd_buf, "swapCol(%d, %d)", &c1, &c2) == 2 || sscanf(cmd_buf, "swapCol %d %d", &c1, &c2) == 2) {
        if (c1 >= 0 && c1 < (int)matrix.col && c2 >= 0 && c2 < (int)matrix.col) {
            int temp = col_map[c1];
            col_map[c1] = col_map[c2];
            col_map[c2] = temp;
            update_swap_file_maps(); // Write immediately to .swap file
            free_ram_buffer();       // Invalidate RAM cache to reload with new map
            snprintf(status_msg, 256, "Swapped col %d & %d (Saved to .swap file)", c1, c2);
        } else {
            snprintf(status_msg, 256, "Error: Invalid column indices (%d, %d)", c1, c2);
        }
        return 1;
    }

    // Jump to row
    int target_row = -1;
    if (sscanf(cmd_buf, "goto %d", &target_row) == 1 || sscanf(cmd_buf, "%d", &target_row) == 1) {
        if (target_row >= 0 && target_row < (int)matrix.row) {
            *cur_r = target_row;
            snprintf(status_msg, 256, "Jumped to row %d", target_row);
        } else {
            snprintf(status_msg, 256, "Error: Row %d out of bounds", target_row);
        }
        return 1;
    }

    snprintf(status_msg, 256, "Unknown command: %s", cmd_buf);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <miniCsv_file> <sorted_words_file>\n", argv[0]);
        return 1;
    }

    int cell_width = 12;
    printf("Enter initial cell display width (> 1): ");
    if (scanf("%d", &cell_width) != 1 || cell_width <= 1) {
        cell_width = 12;
    }

    const char *csv_path = argv[1];
    const char *dict_path = argv[2];

    if (!init_swap_file(csv_path)) {
        fprintf(stderr, "Failed to initialize .swap file for %s\n", csv_path);
        return 1;
    }

    FILE *dict_fp = fopen(dict_path, "rb");
    if (!dict_fp) {
        fprintf(stderr, "Error opening dictionary file %s\n", dict_path);
        if (row_map) free(row_map);
        if (col_map) free(col_map);
        remove(swap_filepath);
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int cur_r = 0, cur_c = 0;
    int top_row = 0, top_col = 0;
    char status_msg[256] = "";

    while (1) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        int start_y = 3;
        int max_vis_rows = max_y - start_y - 3;

        int max_row_idx = (matrix.row > 0) ? (int)matrix.row - 1 : 0;
        int row_digits = 1;
        int temp = max_row_idx;
        while (temp >= 10) { row_digits++; temp /= 10; }
        if (row_digits < 3) row_digits = 3;
        int margin_width = row_digits + 2;

        int max_vis_cols = (max_x - margin_width) / (cell_width + 1);

        if (max_vis_rows <= 0) max_vis_rows = 1;
        if (max_vis_cols <= 0) max_vis_cols = 1;

        if (cur_r < top_row) top_row = cur_r;
        if (cur_r >= top_row + max_vis_rows) top_row = cur_r - max_vis_rows + 1;

        if (cur_c < top_col) top_col = cur_c;
        if (cur_c >= top_col + max_vis_cols) top_col = cur_c - max_vis_cols + 1;

        draw_grid(dict_fp, cur_r, cur_c, top_row, top_col, cell_width, status_msg);

        int ch = getch();
        status_msg[0] = '\0';

        if (ch == 'q' || ch == 'Q') break;

        if (ch == ':') {
            char cmd_buf[128] = "";
            int cmd_pos = 0;

            echo();
            curs_set(1);

            while (1) {
                mvprintw(max_y - 1, 0, ":%-*s", max_x - 2, cmd_buf);
                move(max_y - 1, cmd_pos + 1);
                refresh();

                int input_ch = getch();

                if (input_ch == '\n' || input_ch == '\r') {
                    break;
                } else if (input_ch == 27) { // ESC
                    cmd_buf[0] = '\0';
                    break;
                } else if (input_ch == KEY_BACKSPACE || input_ch == 127 || input_ch == '\b') {
                    if (cmd_pos > 0) {
                        cmd_pos--;
                        cmd_buf[cmd_pos] = '\0';
                    }
                } else if (isprint(input_ch) && cmd_pos < (int)sizeof(cmd_buf) - 1) {
                    cmd_buf[cmd_pos++] = (char)input_ch;
                    cmd_buf[cmd_pos] = '\0';
                }
            }

            noecho();
            curs_set(0);

            if (strlen(cmd_buf) > 0) {
                if (!handle_vim_command(cmd_buf, &cur_r, &cur_c, &cell_width, status_msg)) {
                    break; // Exit requested via :q or :wq
                }
            }
            continue;
        }

        switch (ch) {
            case KEY_UP: case 'k': case 'K':
                if (cur_r > 0) cur_r--;
                break;
            case KEY_DOWN: case 'j': case 'J':
                if (cur_r < (int)matrix.row - 1) cur_r++;
                break;
            case KEY_LEFT: case 'h': case 'H':
                if (cur_c > 0) cur_c--;
                break;
            case KEY_RIGHT: case 'l': case 'L':
                if (cur_c < (int)matrix.col - 1) cur_c++;
                break;
        }
    }

    endwin();
    fclose(dict_fp);

    // Free resources
    free_ram_buffer();
    if (row_map) free(row_map);
    if (col_map) free(col_map);

    // Remove temporary .swap file upon exit
    remove(swap_filepath);

    return 0;
}
