#include <stdio.h>
#include <stdlib.h>

#define BLOCK 100
#define ARR 26
#define BUFF 1024
#define ISALPHABET(x) (x >= 'a' && x <= 'z') //only lower case
#define ENDOFWORD(x) (!ISALPHABET(x))
#define ENDOFLINE(x) (x == '\n')
#define ISCOLUMN(x) (x == ',')

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

typedef struct Node Node;

struct Node{
	int16_t arr[ARR];//27 X 100 = 2700
	short frequency;
};

Node *node_arr, *root;

typedef struct Matrix Matrix;

struct Matrix{
	int row, col;
};

typedef struct Cell Cell;

struct Cell{
	unsigned char len;
	int *arr;
};

char *buf;
int n, m=0;
FILE *f, *ff;
Matrix *matrix;

Node* newNode(Node *node_arr){
	for(int i=0;i<ARR;i++){
		node_arr->arr[i] = -1;
	}
	node_arr->frequency = 0;

	return node_arr;
}

void printFile(FILE *f){
	Word *w = (Word *) malloc(sizeof(Word));
	if(!w) return;
	int n, i;
	unsigned char* ch;
	fseek(f, 0, SEEK_SET);

	while((n = fread(w, sizeof(Word), 1, f)) > 0){
		ch = (unsigned char*) malloc((w->len+1) * sizeof(unsigned char));
		if(!ch) break;
		fread(ch, sizeof(unsigned char), w->len, f);
		ch[w->len] = '\0';
		printf("%s | %d\n", ch, w->frequency);
		free(ch);
	}
	free(w);
}

void getBuffer(){
	while((n = fread(buf, sizeof(char), BUFF-1, f)) > 0){
		if(n == BUFF-1){
			while(n > 0 && !ENDOFLINE(buf[n-1])){
				fseek(f, -1, SEEK_CUR);
				n--;
			}
		}
	}
}

void getMatrix(){
	FILE *my_f = f;
	fseek(my_f, 0, SEEK_SET);
	int n, i, row = 0, oneLine = 1, col = 1;
	matrix = (Matrix *) malloc (sizeof(Matrix));
	while((n = fread(buf, sizeof(char), BUFF-1, my_f)) > 0){
		i = 0;
		while(i < n){
			if(ISCOLUMN(buf[i]) && oneLine){
				col += 1;
			}

			if(ENDOFLINE(buf[i])){
				oneLine = 0;
				row += 1;	
			}
			i++;
		}
	}
	matrix.row = row;
	matrix.col = col;
}

int getWordCount(int j){
	int in_word = 0, word_count = 0;
	for(int i = j;!ISCOLUMN(buf[i]);i++){
		if(!ISALPHABET(buf[i])){
			in_word = 0;
		} else if(!in_word){
			in_word = 1;
			word_count += 1;
		}
	}
	return word_count;
}

void fillTrie(){
	
}

void fillCell(Cell *cell, int *j){
	int index;
	for(int (*i)=j;ISALPHABET(buf[(*i)]);(*i)++){
		index = buf[(*i)] - 'a';
	}
}

void createCell(){
	int index = 0;
	for(int i=0;i<matrix.row;i++){
		Cell cell[matrix.col];
		if(index >= n){
			getBuffer();
			index = 0;
		}
		for(int j=0;j<matrix.col;j++){
			int word_count = getWordCount(index);
			cell[j].len = word_count;
			cell[j].arr = malloc(word_count);
			if(!cell[j].arr) return;
			fillCell(&cell[j], &(index++));
		}
		writeCell(&cell[matrix.col]);
		for(int j=0;j<matrix.col;j++){
			free(cell[j].arr);
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 3) return 1;
	const char *miniCsv = "miniCsv";
	f = fopen(argv[1], "rb");
	ff = fopen(argv[2], "rb");
	if(!f &&!ff) return 1;

	buf = malloc(BUFF * sizeof(char));
	if(!buf) return 1;

	node_arr = (Node *)malloc(BLOCK * sizeof(Node));
	if(!node_arr) return 1;
	int m=0;
	root = newNode(&node_arr[m++]);//1st element of BLOCK
	fillTrie();

	getMatrix();
	createCell();

	return 0;
}
