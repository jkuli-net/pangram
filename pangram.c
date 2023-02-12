#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

//pangram - uses every letter of the alphabet
//heterogram - uses each letter only once

// duplicate words and words that use a letter more than once are removed
// words are converted to bitmask with a bit set for each letter in the word 
//   (bitwise & is used to test for mask collision, bitwise | is used to combine masks)
// the list at level 0 is de-anagramed, when a solution is found all words (anagrams) matching the word mask are printed

// for each word in current list
//   add the word to the sentence, updating the sentence bitmask
//   create a new list with only words that fit into sentence bitmask
//      this causes the list length to shrink rapidly, giving a big speedup
//   new list starts from position of current word, giving sentences in alphabetic order
//      this reduces the search space to only sentences in alphabetic order, giving a big speedup
//   if a list with this sentence bitmask has been processed before
//     if that list was longer, skip this list
//     if that list was shorter, process only the beginning of this list, the part that was not processed before, because of alphabetic
//     big speedup, difficult to implement
//     i use a large array that holds number of words processed at every sentence bitmask
// whenever a solution is found, mark the bitmask at each level as unprocessed, so they will not be skipped when solutions exist
//     this makes the dedupe inefficient, but we need to print all the solutions that existed when the leaf was processed before

#define NUM_LETTERS 26
#define ALL_LETTERS_MASK ((1 << 26) - 1)            //mask with bit set for every letter

int qsort_strcmp(const void * a, const void * b)
{
  return strcmp(*(const char**)a, *(const char**)b);
}

void pangram_heterogram(const char *dict_file)
{

	FILE* fp;    
    char* dictionary_data;                          //entire contents of dict_file
    int len_file;                                   //total length of dictionary_data
    int i, iWord;                                   //used as iterators

    char** word_list;                               //pointer to start of each word in dictionary_data, words will be null terminated
    int* word_mask;                                 //mask with bit set for each letter in the word, for each word in word_list
    int num_words;                                  //number of words in word_list and word_mask

    int level_word_index[NUM_LETTERS];              //position in dictionary at each loop level
    int level_mask[NUM_LETTERS];                    //bitmask for letters in the current sentence at each loop level
    int level_stop_index[NUM_LETTERS];              //loop end index for each level, not always the length of dictionary
    int* level_dict[NUM_LETTERS];                   //individual dictionary of word masks for each level
    int level_dict_length[NUM_LETTERS];             //length of each dictionary    
    int* level_dict_data;                           //buffer that level_dict points in to
	int* mask_completed_length;                     //when a mask is processed, the length of the current dictionary is stored at mask_completed_length[mask]
                                                    //if the mask is encountered later, length is used to skip some words
                                                    // (1<<26)*sizeof(int) == 256mb

    int level;                                      //stack level, increments as words are added to current sentence, decrements when no solutions remain 
	int num_solutions;                              //number of completed pangram heterograms


	//load a dictionary into dictionary_data
	fp = fopen(dict_file, "rb");
    if(!fp)
    {
        printf("dictionary file not found\n");
        return;
    }
    fseek(fp, 0, SEEK_END);
    len_file = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    dictionary_data = (char*)malloc(len_file+1);
    fread(dictionary_data, 1, len_file, fp);
    dictionary_data[len_file] = 0;
    fclose(fp);

    //count number of lines in dictionary_data
    num_words = 1;
    for(i = 0; dictionary_data[i]; i++)
    {
        if(dictionary_data[i] == '\n')
            num_words++;
    }

    //allocate the main word_list and word_mask
    word_list = (char**)malloc(num_words * sizeof(char*));
    word_mask = (int*)malloc(num_words * sizeof(int));

    //set word_list to point to each word in dictionary_data, set num_words
    //null terminate the words
    num_words = 0;
    for(i = 0; dictionary_data[i]; i++)
    {
        word_list[num_words] = &dictionary_data[i];
        num_words++;
        
        while(1)
        {
            if(dictionary_data[i] == 0)
            {
                i--;
                break;
            }
            if(dictionary_data[i] == '\r')
                dictionary_data[i] = 0;
            if(dictionary_data[i] == '\n')
            {
                dictionary_data[i] = 0;
                break;
            }
            i++;
        }
    }
    
	//sort the list
    qsort(word_list, num_words, sizeof(char*), qsort_strcmp);
    
	printf("initial word count: %i\n", num_words);
	printf("converting dictionary to bitmask\n");
	printf("removing words that use a letter more than once\n");
     
    //calculate word_mask for each word
    //set mask=0 for words that use a letter more than once
    //words with mask=0 will be removed later
	for (iWord = 0; iWord < num_words; iWord++)
	{
		int mask = 0;
        int c, bit;

		i = 0;
		while (word_list[iWord][i])
		{
            word_list[iWord][i] = tolower(word_list[iWord][i]);
			c = word_list[iWord][i];

			if (c >= 'a' && c <= 'z')
			{
				bit = 1 << (c - 'a');
				if (mask & bit)
				{
					mask = 0;
					break;
				}
				mask |= bit;
			}
			i++;
		}
        
        word_mask[iWord] = mask;
	}
       

    //compact inplace the word_list and word_mask, removing duplicate words and word_mask==0
	printf("resizing word list\n");
	for (i = iWord = 0; i < num_words; i++)
	{
		if (word_mask[i] == 0)
			continue;

		//remove duplicates
		if(iWord && word_mask[i] == word_mask[iWord-1] && (strcmp(word_list[i], word_list[iWord - 1]) == 0))
			continue;

		word_list[iWord] = word_list[i];
		word_mask[iWord] = word_mask[i];
		iWord++;
	}
    num_words = iWord;
	printf("current word count: %i\n", num_words);
    
    //initialize level_dict pointers to point into level_dict_data allocation
    level_dict_data = (int*)malloc(sizeof(int) * num_words * NUM_LETTERS);
    for(i = 0; i < NUM_LETTERS; i++)
    {
        level_dict[i] = &level_dict_data[i * num_words];
    }

    //allocate 256mb on heap
    mask_completed_length = (int*)calloc(1 << 26, sizeof(int));
        
	//level_dict[0], the current dictionary for stack level 0, is initialized to de-anagram-ed version of word_mask
    //leaving those words in the main word_list to be printed
	printf("removing anagrams\n");
	level_dict_length[0] = 0;
	for (i = 0; i < num_words; i++)
	{
		for (iWord = 0; iWord < level_dict_length[0]; iWord++)
		{
			if (word_mask[i] == level_dict[0][iWord])
				break;
		}
		if(iWord != level_dict_length[0])
			continue;
		level_dict[0][level_dict_length[0]] = word_mask[i];
		level_dict_length[0]++;
	}
	printf("current word count: %i\n", level_dict_length[0]);
    
    //initialize vars for level 0
    level = 0;
    level_word_index[0] = -1;
	level_stop_index[0] = level_dict_length[0];
    level_mask[0] = 0;
    num_solutions = 0;

    //main loop
    while(1)
    {
	    level_word_index[level]++;										//move to the next word for this level
	    if (level_word_index[level] >= level_stop_index[level])			//if at end of dictionary
	    {
		    level--;											        //return to previous recursion level
		    if (level < 0)
			    break;                                                  //finished, exit loop
		    continue;
	    }											

	    level++;			                                                                                //increase recursion level    
	    level_mask[level] = level_mask[level - 1] | level_dict[level - 1][level_word_index[level - 1]];	    //create a sentence mask for this level
	    level_word_index[level] = -1; 

	    if(level_mask[level] == ALL_LETTERS_MASK)                       //if sentence contains all letters
	    {
		    //reopen all current leaves, prevents skipping these leaves
            //this is a bit inefficient, i could have saved a list of suffix solutions at this bitmask
			for (i = 0; i < level; i++)
				mask_completed_length[level_mask[i]] = 0;
		
		    //print the current sentence
		    for (i = 0; i < level; i++)
		    {
			    int mask = level_dict[i][level_word_index[i]];
			    int anagrams_found = 0;
			    for (iWord = 0; iWord < num_words; iWord++)
			    {
				    if (word_mask[iWord] == mask)
				    {
					    if (anagrams_found)
						    printf("|");
					    else
					    {
						    if (i)
							    printf(" ");
					    }
					    printf("%s", word_list[iWord]);
					    anagrams_found++;
				    }
			    }
		    }

			num_solutions++;
			printf("      found at %i mins %0.3f secs (%i)\n",
				(int)(clock() / (60 * CLOCKS_PER_SEC)),
				(float)(clock() % (60 * CLOCKS_PER_SEC)) / CLOCKS_PER_SEC,
				num_solutions);            
			level--;									//continue searching for more pangram heterograms
			continue;
	    }	
    
	    //create a new dict for this level, based on the prev levels dict, only containing valid words
	    //start from prev levels current index, produces only sentences in alphabetic order
        level_dict_length[level] = 0;
	    for (i = level_word_index[level - 1] + 1; i < level_dict_length[level - 1]; ++i)
	    {
		    if ((level_mask[level] & level_dict[level - 1][i]) == 0)
			    level_dict[level][level_dict_length[level]++] = level_dict[level - 1][i];
	    }
        level_stop_index[level] = level_dict_length[level];
    
        //test if a leaf with this bitmask has been calculated previously
        if(mask_completed_length[level_mask[level]])
        {
            //if the dict size then was greater than now, skip it
            if(mask_completed_length[level_mask[level]] >= level_dict_length[level])
            {
			    level--;
			    continue;
            }
            //only process words at start of dict, skipping prev size words
		    level_stop_index[level] = level_dict_length[level] - mask_completed_length[level_mask[level]];
	    }
        //store the number of words in dict at this bitmask
	    mask_completed_length[level_mask[level]] = level_dict_length[level];
    }
    
	printf("finished at %i mins %0.3f secs, with %i solutions.\n",
		(int)(clock() / (60 * CLOCKS_PER_SEC)),
		(float)(clock() % (60 * CLOCKS_PER_SEC)) / CLOCKS_PER_SEC,
		num_solutions);

    free(dictionary_data);
    free(word_list);
    free(word_mask);
    free(level_dict_data);
    free(mask_completed_length);  
}



int main(int argc, const char** argv)
{
    //some pangram heterograms found by others:

    //Blowzy night-frumps vex'd Jack Q.
    //Cwm fjord-bank glyphs vext quiz.
    //Squdgy fez, blank jimp crwth vox!
    //Q-kelt vug dwarf combs jynx phiz.
    //TV quiz drag nymphs blew JFK cox.
    
    const char *filename = "Collins Scrabble Words (2019).txt";
    if(argc >= 2)
        filename = argv[1];
	pangram_heterogram(filename);
    getchar();    

	return 0;
}
