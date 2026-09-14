#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define BLOCK 100
#define ARR 26
#define BUFF 100
#define ISALPHABET(x) (x >= 'a' && x <= 'z') //only lower case
#define ENDOFWORD(x) (!ISALPHABET(x))

typedef struct Node Node;

struct Node{
	int16_t arr[ARR];//27 X 100 = 2700
	short frequency;
};

Node *node_arr;

typedef struct Word Word;

struct Word{
	short frequency;
	unsigned char len;
};

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

void appendTrie(Node* root, char* buf, int depth, FILE *f){
	if(!root) return;

	if(root->frequency > 0){
		Word *word = (Word *) malloc(sizeof(Word));
		if(!word) return;

		word->len = depth;
		word->frequency = root->frequency;

		fwrite(word, sizeof(Word), 1, f);
		fwrite(buf, sizeof(unsigned char), depth, f);
		free(word);
	}

	for(int i=0;i<ARR;i++){
		if(root->arr[i] != -1){
			buf[depth] = 'a' + i;//append char to buf
			appendTrie(&node_arr[root->arr[i]], buf, depth + 1, f);
		}
	}
}

void mergeTrie(Node* root, FILE *f){
	Word *w = (Word *) malloc(sizeof(Word));
	if(!w) return;
	int n, i;
	unsigned char* ch;
	fseek(f, 0, SEEK_SET);

	while((n = fread(w, sizeof(Word), 1, f)) > 0){
		ch = (unsigned char*) malloc(w->len * sizeof(unsigned char));
		if(!ch) break;
		fread(ch, sizeof(unsigned char), w->len, f);
		Node* og_root = root;

		for(i=0;i<w->len;i++){
			int index = ch[i] - 'a';
			if(og_root->arr[index] == -1) break;
			og_root = &node_arr[og_root->arr[index]];
		}
		if(i == w->len && og_root->frequency > 0){
			w->frequency += og_root->frequency;
			long current_pos = ftell(f);
			long word_start_offset = current_pos - (w->len + sizeof(Word));
			fseek(f, word_start_offset , SEEK_SET);

			fwrite(w, sizeof(Word), 1, f);//it will drag f till structure not name too so, need to fseek
			fseek(f, current_pos, SEEK_SET);
			og_root->frequency = 0;
		}
		free(ch);
	}
	free(w);
	fseek(f, 0, SEEK_END);
	char word_buf[256];
	appendTrie(root, word_buf, 0, f);
}

int main(int argc, char** argv){
	if(argc !=2 ) return 1;
	FILE *f = fopen(argv[1], "rb");
	if(!f) return 1;
	char* buf = malloc(BUFF * sizeof(char));

	int n;

	FILE *ff = fopen("frequency_csv", "rb+");
	if (!ff) {
		ff = fopen("frequency_csv", "wb+");
	}
	if (!ff) {
		perror("Error creating/opening frequency_csv");
		fclose(f);
		return 1;
	}

	while((n = fread(buf, sizeof(char), BUFF-1, f)) > 0){
		if(n == BUFF-1){
			while(n > 0 && ISALPHABET(buf[n-1])){
				fseek(f, -1, SEEK_CUR);
				n--;
			}
		}
		buf[n] = '\0';
		node_arr = (Node *)malloc(BLOCK * sizeof(Node));
		if(!node_arr) return 1;
		int m=0;
		Node *root = newNode(&node_arr[m++]);//1st element of BLOCK

		for(int i=0;buf[i] != '\0';i++){
			if(ISALPHABET(buf[i])){
				root = &node_arr[0];
				int j = 0;
				while(!ENDOFWORD(buf[i+j]) && buf[i+j] != '\0'){
					int index = buf[i+j] - 'a';//get the index in terms of(0-25)

					if(root->arr[index] == -1){
						if(m >= BLOCK){
							fprintf(stderr, "Trie is full\n");
							break;
						}
						newNode(&node_arr[m]);
						root->arr[index] = m++;
					}
					root = &node_arr[root->arr[index]];//mth block's address
					j++;
				}
				root->frequency += 1;
				i += (j-1);
			}
		}
		mergeTrie(&node_arr[0], ff);
		free(node_arr);
	}
	printFile(ff);

	fclose(ff);
	fclose(f);
	free(buf);
	return 0;
}
