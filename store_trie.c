#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define NAN UINT_MAX

typedef struct Node Node;

struct Node{
	unsigned int arr[26];
	unsigned int location;
};

Node *node;

unsigned int offset;

typedef struct Word Word;

struct Word{
	short frequency;
	unsigned char len;
};

FILE *wordFile, *fileTrie;

Word *word;
char* name;

unsigned int newNode(){
	Node *newnode = (Node *) malloc (sizeof(Node));
	if(!newnode) return NAN;
	for(int i=0;i<26;i++){
		newnode->arr[i] = 0;//NULL
	}
	newnode->location = NAN;

	unsigned int offset;

	fseek(fileTrie, 0, SEEK_END);
	offset = ftell(fileTrie);
	fwrite(newnode, sizeof(Node), 1, fileTrie);//write new node at eof
	free(newnode);

	return offset;//return the new node's index
}

unsigned int getWord(){
	unsigned int word_location = ftell(wordFile);
	if(fread(word, sizeof(Word), 1, wordFile) == 1){
		name = malloc(word->len + 1);
		fread(name, sizeof(unsigned char), word->len, wordFile);
		name[word->len] = '\0';
		return word_location;
	}
	return NAN;
}

void writeNode(unsigned int offset) {
	fseek(fileTrie, offset, SEEK_SET);
	fwrite(node, sizeof(Node), 1, fileTrie);
}

void getNode(unsigned int offset){
	fseek(fileTrie, offset, SEEK_SET);
	fread(node, sizeof(Node), 1, fileTrie);
}

void writefileTrie(){
	unsigned int i, index, word_location, curr_offset = 0, child_offset;

	fseek(fileTrie, 0, SEEK_END);
	if (ftell(fileTrie) == 0) {
		newNode();
	}

	while((word_location = getWord())!= NAN){
		curr_offset = 0;
		for(i=0;i<word->len;i++){

			getNode(curr_offset);
			index = name[i] - 'a';

			if(node->arr[index] == 0){
				child_offset = newNode();
				getNode(curr_offset);
				node->arr[index] = child_offset;
				writeNode(curr_offset);
				curr_offset = child_offset;
			} else{
				curr_offset = node->arr[index];
			}
		}
		getNode(curr_offset);
		node->location = word_location;
		writeNode(curr_offset);
		free(name);
	}
}

void printTrie(unsigned int offset, int depth, char *word){
    Node current;

    fseek(fileTrie, offset, SEEK_SET);
    fread(&current, sizeof(Node), 1, fileTrie);

    if(current.location != NAN){
        word[depth] = '\0';
        printf("%s\n", word);
    }

    for(int i = 0; i < 26; i++){
        if(current.arr[i]){
            word[depth] = 'a' + i;
            printTrie(current.arr[i], depth + 1, word);
        }
    }
}

int main(int argc, char** argv){
	if(argc != 2) return 1;

	wordFile = fopen(argv[1], "rb");
	if (!wordFile) return 1;
	fileTrie = fopen("fileTrie.trie", "rb+");
	if (!fileTrie) {
		fileTrie = fopen("fileTrie.trie", "wb+");
	}
	if (!fileTrie) {
		fclose(wordFile);
		return 1;
	}

	word = (Word *) malloc (sizeof(Word));
	node = (Node *) malloc (sizeof(Node));

	if(word && node){
		writefileTrie();
		char word_buf[256];
		printTrie(0, 0, word_buf);
	}

	free(word);
	free(node);

	fclose(wordFile);
	fclose(fileTrie);

	return 0;
}
