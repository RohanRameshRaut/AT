#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# 1. Remove old executables and generated data files
echo "Cleaning old files..."
rm -rf *.o sort_trie trie_recovered store_trie converter 02converter miniCsv final_sorted_decreasing.dat fileTrie.trie frequency_csv

# 2. Set the input CSV file (default to sampleCsv.csv if not provided as an argument)
INPUT_CSV="${1:-sampleCsv.csv}"

if [ ! -f "$INPUT_CSV" ]; then
    echo "Error: Input file '$INPUT_CSV' not found!"
    exit 1
fi

echo "Using input file: $INPUT_CSV"

# 3. Compile all C source files
echo "Compiling C source files..."
gcc -c trie_recovered.c
gcc -c sort_trie.c
gcc -c store_trie.c
gcc -c converter.c

# Link executables
gcc trie_recovered.o -o trie_recovered
gcc sort_trie.o -o sort_trie
gcc store_trie.o -o store_trie
gcc converter.o -o converter

echo "Compilation successful!"

# 4. Run the data processing pipeline sequentially
echo "Step 1: Running trie_recovered to generate frequency_csv..."
./trie_recovered "$INPUT_CSV"

echo "Step 2: Running sort_trie to generate final_sorted_decreasing.dat..."
./sort_trie frequency_csv

echo "Step 3: Running store_trie to generate fileTrie.trie..."
./store_trie final_sorted_decreasing.dat

echo "Step 4: Running converter with input CSV and fileTrie.trie..."
./converter "$INPUT_CSV" fileTrie.trie

echo "Pipeline completed successfully!"
