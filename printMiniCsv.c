#include <stdio.h>
#include <stdlib.h>

// Matrix header matching miniCsv format
typedef struct Matrix {
    unsigned int row, col, x, y, start_arr_row, start_arr_col;
} Matrix;

void printMiniCsv(const char *csv_filename, const char *dict_filename) {
    FILE *f = fopen(csv_filename, "rb");
    FILE *dict = fopen(dict_filename, "rb");
    Matrix m_out;

    if (!f) {
        fprintf(stderr, "Error: Could not open compressed file '%s'\n", csv_filename);
        if (dict) fclose(dict);
        return;
    }

    if (!dict) {
        fprintf(stderr, "Error: Could not open dictionary file '%s'\n", dict_filename);
        fclose(f);
        return;
    }

    // Read the 24-byte Matrix header
    if (fread(&m_out, sizeof(Matrix), 1, f) != 1) {
        fprintf(stderr, "Error: Failed to read matrix header from '%s'\n", csv_filename);
        fclose(f);
        fclose(dict);
        return;
    }

    printf("=== SPREADSHEET MATRIX (%u Rows x %u Cols) ===\n\n", m_out.row, m_out.col);

    for (unsigned int i = 0; i < m_out.row; i++) {
        printf("Row %u:\n", i);
        for (unsigned int j = 0; j < m_out.col; j++) {
            unsigned char cell_len;
            if (fread(&cell_len, sizeof(unsigned char), 1, f) != 1) {
                fprintf(stderr, "\nError: Unexpected end of file while reading cell length.\n");
                fclose(f);
                fclose(dict);
                return;
            }

            printf("  Cell[%u][%u] (words: %u): ", i, j, cell_len);

            for (unsigned char k = 0; k < cell_len; k++) {
                unsigned char b1;
                if (fread(&b1, sizeof(unsigned char), 1, f) != 1) {
                    fprintf(stderr, "\nError reading byte\n");
                    fclose(f);
                    fclose(dict);
                    return;
                }

                // Decode Variable-Byte (VByte) Integer
                unsigned int val = 0;
                if ((b1 & 0x80) == 0) {
                    // 1-byte encoding (0xxxxxxx)
                    val = b1;
                } else if ((b1 & 0xC0) == 0x80) {
                    // 2-byte encoding (10xxxxxx xxxxxxxx)
                    unsigned char b2;
                    fread(&b2, sizeof(unsigned char), 1, f);
                    val = ((b1 & 0x3F) << 8) | b2;
                } else if ((b1 & 0xC0) == 0xC0) {
                    // 3-byte encoding (11xxxxxx xxxxxxxx xxxxxxxx)
                    unsigned char b2, b3;
                    fread(&b2, sizeof(unsigned char), 1, f);
                    fread(&b3, sizeof(unsigned char), 1, f);
                    val = ((b1 & 0x3F) << 16) | (b2 << 8) | b3;
                }

                // --- DEREFERENCING FROM DICTIONARY FILE ---
                // Save position in miniCsv
                long csv_pos = ftell(f);

                // Seek to offset 'val' inside the words file
                fseek(dict, val, SEEK_SET);

                // Read word length (1 byte) followed by word characters
                unsigned char word_len;
                if (fread(&word_len, sizeof(unsigned char), 1, dict) == 1) {
                    char *word_str = malloc(word_len + 1);
                    if (word_str) {
                        if (fread(word_str, sizeof(char), word_len, dict) == word_len) {
                            word_str[word_len] = '\0';
                            printf("%s ", word_str);
                        }
                        free(word_str);
                    }
                } else {
                    printf("[INVALID_OFFSET:%u] ", val);
                }

                // Restore position in miniCsv
                fseek(f, csv_pos, SEEK_SET);
            }
            printf("\n");
        }
        printf("\n");
    }

    fclose(f);
    fclose(dict);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <miniCsv_file> <sorted_words_dictionary>\n", argv[0]);
        printf("Example: %s miniCsv final_sorted_decreasing.dat\n", argv[0]);
        return 1;
    }

    printMiniCsv(argv[1], argv[2]);

    return 0;
}
