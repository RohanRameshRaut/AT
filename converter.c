#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define BLOCK 100
#define ARR 26
#define BUFF 1024
#define ISALPHABET(x) ((x) >= 'a' && (x) <= 'z') // only lower case
#define ENDOFWORD(x) (!ISALPHABET(x))
#define ENDOFLINE(x) ((x) == '\n' || (x) == '\r')
#define ISCOLUMN(x) ((x) == ',')
#define NAN UINT_MAX

typedef struct Node Node;

struct Node {
    unsigned int arr[26];
    unsigned int location;
};

Node *node;
FILE *fileTrie, *wordFile, *csvFile;

typedef struct FileWord {
    short frequency;
    unsigned char len;
} FileWord;

typedef struct Word Word;
struct Word {
    short frequency;
    unsigned char len;
    char* name;
};

typedef struct Matrix Matrix;
struct Matrix {
    int row, col;
};

typedef struct Cell Cell;
struct Cell {
    unsigned char len;
    unsigned int *arr;
};

char *buf;
int n, m = 0;
Matrix *matrix;

void printFile(FILE *f) {
    Word *w = (Word *) malloc(sizeof(Word));
    if (!w) return;
    int read_count;
    unsigned char* ch;
    fseek(f, 0, SEEK_SET);

    while ((read_count = fread(w, sizeof(Word), 1, f)) > 0) {
        ch = (unsigned char*) malloc((w->len + 1) * sizeof(unsigned char));
        if (!ch) break;
        fread(ch, sizeof(unsigned char), w->len, f);
        ch[w->len] = '\0';
        printf("%s | %d\n", ch, w->frequency);
        free(ch);
    }
    free(w);
}

void getBuffer() {
    n = fread(buf, sizeof(char), BUFF - 1, wordFile);
    if (n <= 0) {
        buf[0] = '\0';
        n = 0;
        return;
    }

    buf[n] = '\0';

    if (n == BUFF - 1 && !ENDOFLINE(buf[n - 1])) {
        int rewind_count = 0;
        while (n > 0 && !ENDOFLINE(buf[n - 1])) {
            n--;
            rewind_count++;
        }
        if (rewind_count > 0) {
            fseek(wordFile, -rewind_count, SEEK_CUR);
            buf[n] = '\0';
        }
    }
}

void getMatrix() {
    fseek(wordFile, 0, SEEK_SET);
    int read_bytes, i, row = 0, oneLine = 1, col = 1;
    matrix = (Matrix *) malloc(sizeof(Matrix));
    while ((read_bytes = fread(buf, sizeof(char), BUFF - 1, wordFile)) > 0) {
        i = 0;
        while (i < read_bytes) {
            if (ISCOLUMN(buf[i]) && oneLine) {
                col += 1;
            }

            if (ENDOFLINE(buf[i])) {
                if (buf[i] == '\n') {
                    oneLine = 0;
                    row += 1;
                }
            }
            i++;
        }
    }
    matrix->row = row;
    matrix->col = col;

    fwrite(matrix, sizeof(Matrix), 1, csvFile);
    fseek(wordFile, 0, SEEK_SET);
}

// Helper to safely get the current char or refill buffer if needed
char getChar(int *index) {
    if (*index >= n) {
        getBuffer();
        *index = 0;
        if (n == 0) return '\0';
    }
    return buf[*index];
}

int getWordCount(int index) {
    int in_word = 0, word_count = 0;
    int curr_idx = index;
    char c;

    while ((c = getChar(&curr_idx)) != '\0' && !ISCOLUMN(c) && !ENDOFLINE(c)) {
        if (!ISALPHABET(c)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            word_count += 1;
        }
        curr_idx++;
    }
    return word_count;
}

void getNode(unsigned int offset) {
    fseek(fileTrie, offset, SEEK_SET);
    fread(node, sizeof(Node), 1, fileTrie);
}

void fillCell(Cell *cell, int *j) {
    int index;
    char c;

    for (int k = 0; k < cell->len; k++) {
        cell->arr[k] = NAN;

        // Skip non-alphabetic, non-column, non-line-ending characters
        while ((c = getChar(j)) != '\0' && !ISALPHABET(c) && !ISCOLUMN(c) && !ENDOFLINE(c)) {
            (*j)++;
        }

        getNode(0);
        while ((c = getChar(j)) != '\0' && ISALPHABET(c)) {
            index = c - 'a';
            if (node->arr[index]) {
                getNode(node->arr[index]);
            }
            (*j)++;
        }

        c = getChar(j);
        if (!ISALPHABET(c) && node->location != NAN) {
            cell->arr[k] = node->location;
        }
    }

    // Consume trailing delimiters (commas, carriage returns, newlines)
    while ((c = getChar(j)) != '\0' && (ISCOLUMN(c) || ENDOFLINE(c))) {
        (*j)++;
        if (c == '\n' || ISCOLUMN(c)) break;
    }
}

void writeCell(Cell *cell) {
    for (int j = 0; j < matrix->col; j++) {
        fwrite(&cell[j].len, sizeof(unsigned char), 1, csvFile);
        fwrite(cell[j].arr, sizeof(unsigned int), cell[j].len, csvFile);
    }
}

void createCell() {
    int index = 0;
    for (int i = 0; i < matrix->row; i++) {
        Cell cell[matrix->col];
        for (int j = 0; j < matrix->col; j++) {
            int word_count = getWordCount(index);
            cell[j].len = word_count;
            cell[j].arr = malloc(word_count * sizeof(unsigned int));
            if (!cell[j].arr) return;
            fillCell(&cell[j], &index);
        }
        writeCell(cell);
        for (int j = 0; j < matrix->col; j++) {
            free(cell[j].arr);
        }
    }
}

void printMiniCsv() {
    FILE *f;
    Matrix m_out;
    Cell cell;
    unsigned int *arr;

    f = fopen("miniCsv", "rb");
    if (!f) {
        perror("miniCsv");
        return;
    }

    if (fread(&m_out, sizeof(Matrix), 1, f) != 1) {
        printf("Could not read Matrix\n");
        fclose(f);
        return;
    }

    printf("Rows = %d, Cols = %d\n\n", m_out.row, m_out.col);

    for (int i = 0; i < m_out.row; i++) {
        printf("Row %d:\n", i);
        for (int j = 0; j < m_out.col; j++) {
            if (fread(&cell.len, sizeof(unsigned char), 1, f) != 1) {
                printf("Error reading cell length\n");
                fclose(f);
                return;
            }

            arr = malloc(cell.len * sizeof(unsigned int));
            if (!arr) {
                perror("malloc");
                fclose(f);
                return;
            }

            if (fread(arr, sizeof(unsigned int), cell.len, f) != cell.len) {
                printf("Error reading cell data\n");
                free(arr);
                fclose(f);
                return;
            }

            printf("  Cell[%d][%d] len = %u : ", i, j, cell.len);
            for (int k = 0; k < cell.len; k++) {
                printf("%u ", arr[k]);
            }
            printf("\n");
            free(arr);
        }
    }

    fclose(f);
}

int main(int argc, char** argv) {
    if (argc != 3) return 1;

    csvFile = fopen("miniCsv", "wb+");
    wordFile = fopen(argv[1], "rb");
    fileTrie = fopen(argv[2], "rb");

    if (!wordFile || !fileTrie || !csvFile) return 1;

    buf = malloc(BUFF * sizeof(char));
    node = (Node *) malloc(sizeof(Node));
    if (!buf || !node) return 1;

    getBuffer();
    getMatrix();
    createCell();

    free(buf);
    free(node);

    fclose(csvFile);
    fclose(wordFile);
    fclose(fileTrie);

    printMiniCsv();

    return 0;
}
