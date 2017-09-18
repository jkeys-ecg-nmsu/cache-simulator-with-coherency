//written by: Jeremy Keys 
//Last modified: 4-13-17

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/****************** useful constants/magic numbers ******************/

const int ADDRESS_LENGTH     = 32;
const int NUM_BITS_PER_BYTE	 = 8;
const int NUM_BYTES_PER_WORD = 4;
const int NUM_BITS_PER_LRU   = 2;
const int NUM_BITS_PER_VALID = 1;
const int NUM_BITS_PER_DIRTY = 1;
const int NUM_CYCLES_PER_HIT = 1;

unsigned int NUM_CORES = 2;

const char WRITE_BACK = 'B';
const char WRITE_THRU = 'T';
const char WRITE_OP   = 'W';
const char READ_OP    = 'R';
const char MODIFIED   = 'M';
const char INVALID    = 'I';
const char SHARED     = 'S';

int debug = 0;

/****************** Useful OO structures ******************/

typedef struct Entry Entry;

//class to help with logging
typedef struct MemOp {
	unsigned int addr;
	unsigned int tag;
	unsigned int index;
	unsigned int fullOffset;
	unsigned int entryOffset;
	unsigned int byteOffset;
} MemOp;

typedef struct Entry {
	//uint32_t* dataBlock;	
	unsigned int tag;
	unsigned int valid;
	unsigned int dirty;
	
	//A6 additions
	unsigned int LRUCounter;
	
	//A7 additions
	char state; //MODIFIED, SHARED or INVALID
	int entryID;
} Entry;

typedef struct Set {
	Entry* entries;	
	unsigned int numEntriesInUse; 
	unsigned int numEntries;	
	
	//A7 additions
	int setID;
} Set;

typedef struct Cache {
	unsigned int blockSize;		//blockSize in number of bytes 
	unsigned int numDataWords, numBytes;
	unsigned int numEntries, numInstructions;
	unsigned int numReads, numWrites, numReadHits, numWriteHits, numWriteMisses, numReadMisses;
	unsigned int numHits, numMisses;
	unsigned int numCycles, numCyclesPerMiss;
	unsigned int indexLength, offsetLength, tagLength;
	
	double avgMemAccessTime, hitRatio;
	
	//A6 additions
	Set* sets;	
	char writePolicy;
	unsigned int numSets;
	unsigned int setAssociativity;
	unsigned int numEntriesPerSet;
	unsigned int numOverheadBits, numOverheadBytes;
	unsigned int numWritesToCache, numWritesToMem;
	unsigned int numBytesPerSet, numBytesPerBlock, numWordsPerBlock;	
	unsigned int entryOffsetLength, byteOffsetLength; 

	//A7 additions
	unsigned int numBlocksInvalidated, numWriteBacksDueToAccessNeed,numWriteBacksDueToReadMiss, NumWritesToCacheDueToWriteOp, NumWritesBacksDueToWriteThruPolicy;
	unsigned int numWritesToCacheDueToReadMiss;
	unsigned int cacheID;
} Cache;

typedef struct MulticoreCache {
	Cache *caches;
	
	int numCores; //== numCaches
} MulticoreCache;


/****************** print functions ******************/

void printCacheInit(Cache *c) {
	printf("\nCaches constructed!\nTotal size of each cache (data only) in bytes: %u\nset associativity: %d\n", 	c->numDataWords*NUM_BYTES_PER_WORD, c->setAssociativity);
	printf("bytes per block: %d\nwords per block: %d\nnum entries: %d\nnum sets: %d\nentries per set: %d\ntag length: %d\nindex length: %d\noffset length: %d\n\n", c->blockSize*NUM_BYTES_PER_WORD, c->blockSize, c->numEntries, c->numSets, c->numEntriesPerSet, c->tagLength, c->indexLength, c->offsetLength);
}

void printEntry(Entry *e, unsigned int byteAddress, unsigned int blockAddress, unsigned int index, unsigned int offsetFull, unsigned int offsetEntry, unsigned int offsetByte, unsigned int newTag) {	
	printf("byte address: 0x%x\nindex: 0x%x, %u\nfull offset: 0x%x\noldTag: 0x%x\nnewTag: 0x%x\n", byteAddress, index, index, offsetFull, e && e->valid ? e->tag : 0, newTag);
	printf("valid bit %u dirty bit %u\n\n", e->valid, e->dirty);
}

void printSet(Set *s, int n) {
	//printf("\nSet %d\n", n);
	
	for(int i = 0; i != s->numEntries; i++) {
		Entry *e = s->entries+i;
		printf("valid: %u\tdirty: %u\ttag: %u\n\n", e->valid, e->dirty, e->tag);
	}	
	printf("\n");
}	

void printCacheStatsHelper(Cache *c) {
	printf("Cache ID: %d\n", c->cacheID);
	printf("Total size of cache (data only) in bytes: %u\n", 	c->numDataWords*NUM_BYTES_PER_WORD);
	printf("Total size of cache (data only) in words: %u\n", 	c->numDataWords);
	printf("Total size of each block (data only) in bytes: %u\n", 	c->numDataWords*NUM_BYTES_PER_WORD/c->numEntries);
	printf("Total size of each block (data only) in words: %u\n", 	c->numDataWords/c->numEntries);
	printf("Total number of cache overhead bytes: %u\n", 		c->numOverheadBytes);
	printf("Total number of memory operations: %u\n", 			c->numInstructions);
	printf("Total number of write ops: %u\n", 						c->numWrites);
	printf("Total number of write hits: %u\n", 					c->numWriteHits);
	printf("Total number of write misses: %u\n", 				c->numWriteMisses);
	printf("Total number of read ops: %u\n", 						c->numReads);
	printf("Total number of read hits: %u\n", 					c->numReadHits);
	printf("Total number of read misses: %u\n", 				c->numReadMisses);
	printf("Total number of writes to mem: %u\n", 				c->numWritesToMem);
	printf("Total number of writes to mem due to another cache needing access to shared block: %u\n",c->numWriteBacksDueToAccessNeed);
	printf("Total number of writes to mem due to another cache invalidating and then modifying shared block: %u\n",c->numBlocksInvalidated);
	if(c->writePolicy == WRITE_BACK) printf("Total number of writes to mem due to a read miss on a dirty block: %u\n",c->numWriteBacksDueToReadMiss);
	if(c->writePolicy == WRITE_THRU) printf("Total number of writes to mem due to cache write w/ write-through policy: %u\n",c->NumWritesBacksDueToWriteThruPolicy);
	
	printf("Total number of writes to cache: %u\n", 			c->numWritesToCache);
	printf("Total number of writes to cache due to read misses(read from mem, write to cache): %u\n",c->numWritesToCacheDueToReadMiss);
	printf("Total number of writes to cache due to write operations: %u\n",c->NumWritesToCacheDueToWriteOp);
	printf("Hit ratio: %f\n", 							c->hitRatio);
	
	printf("Average memory access time: %f cycles\n", 	c->avgMemAccessTime); //TODO: need cast to float?
}

void printCacheStats(MulticoreCache *mcc) {
	printf("Number of cores: %d\n", NUM_CORES);
	for(int i = 0; i != NUM_CORES; i++) {
		printCacheStatsHelper(mcc->caches+i);
		printf("\n");
	}
	printf("\n");
}

/****************** initialization, cleanup (free), and utility funcs ******************/


//http://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2
int IsPowerOfTwo(int x)
{
    return (x != 0) && ((x & (x - 1)) == 0); //return 0 if not power
}

int processProgArgs(char** argv, int argc, int *blockSize, int *totalNumDataWords, int *numCyclesPerMiss, int *setAssociativity, char *writePolicy) {
		
	if(argc % 2 != 0) {
		printf("Must provide an odd amount of arguments\n"); //will actually be even, b/c argv[0] is name of program
		return 1;
	}
	
	for(int i = 1; i < (argc-1); i += 2) { 			//start at 1, b/c argv[0] is name of prog
		char* flag = argv[i];				
		char *flagValue_s = argv[i+1];		
		int flagValue = atoi(flagValue_s);
		
		if(strcmp(flag, "-b") == 0) {
			*blockSize = flagValue;	
			if(!IsPowerOfTwo(flagValue)) {
				printf("Number of words per block must be a power of 2\n");
				return 3;
			} //end if -b flag error cond					
		} else if(strcmp(flag, "-m") == 0) {
			*numCyclesPerMiss = flagValue;	
			if (flagValue <= 0) {
				printf("Valid number of cycles per miss must be positive\n");
				return 4;
			} //end if -m flag error cond		
		} else if(strcmp(flag, "-n") == 0) {
			*totalNumDataWords = flagValue;		
			if(!IsPowerOfTwo(flagValue)) {				
				printf("Number of data words must be a power of 2\n");
				return 5;
			} //end if -n flag error cond
		} else if(strcmp(flag, "-w") == 0) {
			*writePolicy = flagValue_s[0];		
			if(*writePolicy != 'T' && *writePolicy != 'B') {				
				printf("Valid flags are 'T' (write-through) or 'B' (write-back)\n");
				return 6;
			} //end if -n flag error cond
		}else if(strcmp(flag, "-a") == 0) {
			*setAssociativity = flagValue;		
			if(!IsPowerOfTwo(flagValue)) {				
				printf("Number of sets (set-associativity) must be a power of 2\n");
				return 6;
			} //end if -n flag error cond
		}else if(strcmp(flag, "-debug") == 0) {
			if(flagValue != 0) debug = 1;		
		} else if(strcmp(flag, "-c") == 0) {
			if(!IsPowerOfTwo(flagValue)) {				
				printf("Number of cores/caches must be a power of 2\n");
				return 9;
			}	
			NUM_CORES = flagValue;
		} else {
			printf("Invalid flag given\n");
			return 7;
		} //end ifs
	} //end arg processing for	
	
	return 0;
}

void initCache(Cache *c, unsigned int coreID, unsigned int blockSize, unsigned int numDataWords, unsigned int numCyclesPerMiss, unsigned int setAssociativity, char writePolicy) {
	c->cacheID = coreID;
	c->writePolicy = writePolicy;
	c->blockSize = blockSize;		//blocksize in numWords					//TODO
	c->numDataWords = numDataWords;
	c->numCyclesPerMiss = numCyclesPerMiss;									
	c->numEntries = c->numDataWords / blockSize;							//TODO
	c->numBytes = numDataWords*NUM_BYTES_PER_WORD;							//TODO
	c->setAssociativity = setAssociativity;
	c->numSets = c->numEntries / c->setAssociativity;						//TODO
	c->numEntriesPerSet = c->numEntries / c->numSets;						//TODO
	c->numBytesPerBlock = (unsigned int) blockSize*NUM_BYTES_PER_WORD;		//TODO
	c->indexLength = (unsigned int) log2(c->numSets);						//TODO
	c->offsetLength = (unsigned int) log2(blockSize*NUM_BYTES_PER_WORD);	//TODO
	c->tagLength = ADDRESS_LENGTH - c->indexLength - c->offsetLength; 		//TODO
	c->numHits = c->numMisses = c->numReads = c->numWritesToMem = 0;
	c->numWritesToCache = c->numReadMisses = c->numWriteMisses = c->numReadHits = 0;
	c->numWriteHits = c->numCycles = c->numInstructions = c->numBlocksInvalidated = 0;
	c->numWriteBacksDueToAccessNeed = c->numWriteBacksDueToReadMiss = 0;
	c->NumWritesToCacheDueToWriteOp = c->NumWritesBacksDueToWriteThruPolicy = 0;
			
	c->entryOffsetLength = (unsigned int) log2(c->numEntriesPerSet);		//TODO
	c->byteOffsetLength = c->offsetLength - c->entryOffsetLength;			//TODO

	/*** calculate overhead ***/	
	
	//LRU bits + valid bits + address bits (just the tag)
	c->numOverheadBits = (NUM_BITS_PER_LRU * c->numEntries) + (NUM_BITS_PER_VALID * c->numEntries) + ((ADDRESS_LENGTH- c->offsetLength - c->indexLength) * c->numEntries);
	
	//+ (if write back, dirty bits)
	if(writePolicy == WRITE_BACK) c->numOverheadBits += NUM_BITS_PER_DIRTY * c->numEntries; 
		
	c->numOverheadBytes = c->numOverheadBits / 32; //32 bits per byte
	
	/*** allocate space for the cache entries ***/	
	
	c->sets = malloc(sizeof(Set) * c->numSets);
	
	Set *s;
	Entry *e;
	int entryID = 0;
	for(int i = 0; i != c->numSets; i++) {
		s = c->sets+i;
		s->entries = malloc(sizeof(Entry) * c->numEntriesPerSet);
		// s->dataBlock = malloc(sizeof(uint32_t) * c->blockSize);
		s->numEntries = c->numEntriesPerSet;
		s->numEntriesInUse = 0;
		s->setID = i;
		for(int j = 0; j != c->numEntriesPerSet; j++) {
			e = s->entries+j;
			// e->dataBlock = malloc(sizeof(uint8_t) * blockSize); //uint8_t is guaranteed 8 bits (1 byte)			
			e->state = INVALID;
			e->tag = 0;
			e->valid = 0;
			e->dirty = 0;
			e->entryID = j;
		}	
	}
	
}

void initMulticoreCache(MulticoreCache *mcc, int blockSize, int numDataWords, int numCyclesPerMiss, int setAssociativity, char writePolicy) {	
	mcc->caches = malloc(sizeof(Cache) * NUM_CORES);
	
	for(int i = 0; i != NUM_CORES; i++) {
		initCache(mcc->caches+i, i /*i is coreID*/, blockSize, numDataWords, numCyclesPerMiss, setAssociativity, writePolicy);
	}
	
	printCacheInit(mcc->caches);	
}

void freeCache(Cache *c) {
	for(int i = 0; i != c->numSets; i++) {
		free(c->sets[i].entries);	//free the blocks for each set
		// free(c->sets[i].dataBlock);
	}
		
	free(c->sets);	//then free all set objects
}

void freeMCC(MulticoreCache *mcc) {
	for(int i = 0; i != NUM_CORES; i++) {
		freeCache(mcc->caches+i);
	}
	
	free(mcc->caches);
}

Entry* getLeastRecentlyUsedEntry(Set *s, int *entryID) {
	assert(s->numEntries == s->numEntriesInUse);
	
	Entry* lru = s->entries, *e;
	for(int i = 0; i != s->numEntriesInUse; i++) {
		e = s->entries+i;
		if(lru->LRUCounter < e->LRUCounter) {
			*entryID = i;
			lru = e;
		}
	}
	
	return lru;
}

void updateLRUs(Set *s, Entry *evict) { //update all LRUs except evicted block, which becomes 0
	for(int i = 0; i != s->numEntries; i++) {
		Entry *e = s->entries+i;
		if(e == evict) continue;
		e->LRUCounter++; //then increment this entry's LRU counter
	}
	
	evict->LRUCounter = 0;
}

Entry* getEntry(Set *s, int entryID) {
	return (s->entries+entryID);
}

Set* getSet(Cache *c, int setID) {
	return (c->sets+setID);
}

int matchingEntryExists(Set *s, int newTag) {
	Entry *e;
	for(int i = 0; i != s->numEntries; i++) {
		e = s->entries+i;
		if(e->tag == newTag) {
			return 1;
		}
	}
	
	return 0;
}

Entry *matchingEntry(Set *s, int newTag, int *entryID) {
	Entry *e;
	for(int i = 0; i != s->numEntries; i++) {
		e = s->entries+i;
		if(e->tag == newTag) {
			*entryID = i;
			return e;
		}
	}
	
	assert(1 != 1); //should never reach this
	return NULL; //in case I change logic
}


Entry *getUnusedEntry(Set *s, int *entryID) {
	Entry *e;
	for(int i = 0; i != s->numEntries; i++) {
		e = s->entries+i;
		if(!(e->valid)) {
			*entryID = i;
			return e;
		}
	}
	
	assert(1 != 1); //should never reach this
	return NULL; //in case I change logic
}

/****************** functions for handling different cases of reads and writes ******************/

void handleReadHit(Cache *c, Entry* e) {
	if(debug) printf("  -READ HIT!\n");
	c->numHits++;
	c->numReadHits++;	
}

void handleReadMiss(Cache *c, Entry *e) {		
	if(debug) printf("  -READ MISS!\n");
	//TODO: read data block from memory, store in this entry
	c->numCycles += c->numCyclesPerMiss; 
	c->numMisses++;
	c->numWritesToCache++;
	c->numWritesToCacheDueToReadMiss++;
	c->numReadMisses++;	
}

void handleWriteHit(Cache *c, Entry* e) {	
	if(debug) printf("  -WRITE HIT!\n");
	c->numHits++;
	c->numWriteHits++;
	e->dirty = 1; 	
}

void handleWriteMiss(Cache *c, Entry *e) {
	if(debug) printf("  -WRITE MISS!\n");
	c->numCycles += c->numCyclesPerMiss; 
	c->numMisses++;
	c->numWriteMisses++;
}

int handleRead(MulticoreCache *mcc, Cache *c, Set *s, int newTag) {
	Cache *otherCache;
	Set *otherSet; 		//used for checking corresponding set in other caches
	Entry *otherEntry;  //used for checking corresponding entry in otherSet
	int entryID, setID = s->setID, modifiedBlockFound = 0;
	c->numReads++;
	
	Entry *e;
	
	/*** check whether we read hit or read miss, and handle accordingly ***/
	
	//check for matching entry
	if(matchingEntryExists(s, newTag)) {
		e = matchingEntry(s, newTag, &entryID);
		//(debug) printf("  -block with matching tag and valid data found in set!\n", entryID);
		handleReadHit(c, e);
	} else if(s->numEntriesInUse == s->numEntries) { //if no matching entry, check if set is full
		e = getLeastRecentlyUsedEntry(s, &entryID);
		handleReadMiss(c, e);
		if(debug) printf("  -set is full, selecting least recently used block to evict (index %d of entries array) after handling coherency...\n", entryID);
	} else { //no matching and set is not full, get first unused entry
		e = getUnusedEntry(s, &entryID);
		s->numEntriesInUse++;
		handleReadMiss(c, e);
		if(debug) printf("  -empty entry in set, will insert at block %d of entries array after handling coherency...\n", entryID);
	}		
	
	
	/*** check the current state of the block we are reading, and handle accordingly ***/
	
	switch(e->state) {
		case 'I':
			if(debug) printf("  -Reading an INVALID block, first check all corresponding blocks for MODIFIED state before reading from memory:\n");
			//if invalid on read, we "must verify that the line is not in the "M" state in any other cache" (wikipedia)
			for(int i = 0; i != NUM_CORES; i++) {
				otherCache = mcc->caches+i;
				if(otherCache == c) continue; 				//skip the current cache
				otherSet = getSet(otherCache, setID);			//get corresponding set
				otherEntry = getEntry(otherSet, entryID);   //get corresponding entry
				
				//if there is a matching modified entry, write that entry to memory
				//also copy it to the current entry, and then invalide the other entry
				if(otherEntry->state == MODIFIED) {
					if(debug) printf("    -CORRESPONDING MODIFIED BLOCK FOUND IN CACHE ID %d! Copying block from that cache to current cache #%d, then invalidating and evicting block in that cache...\n", otherCache->cacheID, c->cacheID);
					otherCache->numWritesToMem++;
					otherCache->numBlocksInvalidated++;
					otherEntry->state = INVALID; 
					otherSet->numEntriesInUse--;
					otherEntry->valid = otherEntry->dirty = otherEntry->tag = otherEntry->LRUCounter = 0;
					modifiedBlockFound = 1;
				}
			}
			
			if(!modifiedBlockFound) {
				if(debug) printf("    -No corresponding modified block, so reading from memory store (instead of other cache) to this cache...\n", otherCache->cacheID, c->cacheID);
			}
			break;
		case 'M':
		if(debug) printf("    -Reading from a MODIFIED block, supplying data without reading from memory store...\n");
		//"When a read request arrives at a cache for a block in the "M" or "S" states, the cache supplies the data."
		 //nothing else to do, we've already served the data
			break;
		case 'S':
		if(debug) printf("    -Reading from a SHARED block, supplying data without reading from memory store...\n");
		//When a read request arrives at a cache for a block in the "M" or "S" states, the cache supplies the data.
		 //nothing else to do, we've already served the data		
	}
	
	/*** finally, we reset the current entries LRU counter, set it to valid, and update its tag  ***/
	
	updateLRUs(s,e); //increment LRU counter for all entries in set except e, which becomes 0
	e->tag = newTag;
	e->state = SHARED;
	e->valid = 1;
}

int handleWrite(MulticoreCache *mcc, Cache *c, Set *s, int newTag) {
	Cache *otherCache;
	Entry *e = NULL, *otherEntry;  //used for checking corresponding entry in otherSet;
	Set *otherSet; 		//used for checking corresponding set in other caches
	int entryID, setID = s->setID, modifiedBlockFound = 0;
	c->numWrites++;
	c->numWritesToCache++;
	c->NumWritesToCacheDueToWriteOp++;
	
	/*** check whether we write hit or write miss, and handle accordingly ***/
	
	//check for matching entry
	if(matchingEntryExists(s, newTag)) {
		e = matchingEntry(s, newTag, &entryID);
		//if(debug) printf("    -block with matching tag and valid bit found in set!\n", entryID);
		handleWriteHit(c, e);
	} else if(s->numEntriesInUse == s->numEntries) { //if no matching entry, check if set is full
		e = getLeastRecentlyUsedEntry(s, &entryID);
		handleWriteMiss(c, e);
		if(debug) printf("  -set is full, selecting least recently used block to evict (index %d of entries array) after handling coherency...\n", entryID);
	} else { //no matching and set is not full, get first unused entry
		e = getUnusedEntry(s, &entryID);
		handleWriteMiss(c, e);
		if(debug) printf("  -empty entry in set, will insert at block %d of entries array after handling coherency...\n", entryID);
		s->numEntriesInUse++;
	}
	
	switch(e->state) {
		case 'I':
			if(debug) printf("    -Writing to an INVALID block, first notify other caches to evict any corresponding blocks which are modified or shared\n");
		 //"If the block is in the "I" state, the cache must notify any other caches that might contain the block in the "S" or "M" states that they must evict the block. If the block is in another cache in the "M" state, that cache must either write the data to the backing store or supply it to the requesting cache. If at this point the cache does not yet have the block locally, the block is read from the backing store before being modified in the cache. After the data is modified, the cache block is in the "M" state."
			//for every corresponding entry, if it is modified, evict it
			for(int i = 0; i != NUM_CORES; i++) {
				otherCache = mcc->caches+i;
				if(otherCache == c) continue;
				otherSet = getSet(otherCache, setID);			//get corresponding set
				otherEntry = getEntry(otherSet, entryID);   //get corresponding entry
				
				//" If the block is in another cache in the "M" state, that cache must either write the data to the backing store or supply it to the requesting cache."
				//if there is a matching modified entry, write that entry to memory; also copy it to the current entry, and then invalide the other entry
				if(otherEntry->state == MODIFIED) {
					if(debug) printf("      -CORRESPONDING MODIFIED BLOCK FOUND IN CACHE ID %d! Copying that block to current cache #%d (current core mem op), then invalidating and evicting...\n", otherCache->cacheID, c->cacheID);
					otherCache->numWritesToMem++;
					otherCache->numBlocksInvalidated++;
					otherEntry->state = INVALID; 
					otherSet->numEntriesInUse--;
					otherEntry->valid = otherEntry->dirty = otherEntry->tag = otherEntry->LRUCounter = 0;
					modifiedBlockFound = 1;
				}				
			}
			
			if(!modifiedBlockFound) {
				if(debug) printf("      -No corresponding modified block, so reading from memory store (instead of other cache) to this cache...\n");
			}
			
			break;
		case 'M':
			if(debug) printf("    -Writing to a MODIFIED block, nothing else to do...\n");
			//"When a write request arrives at a cache for a block in the "M" state, the cache modifies the data locally."
			break;
		case 'S':
			//"If the block is in the "S" state, the cache must notify any other caches that might contain the block in the "S" state that they must evict the block. This notification may be via bus snooping or a directory, as described above. Then the data may be locally modified."
			//for every corresponding entry, if it is shared, evict it
			if(debug) printf("    -Writing to a SHARED block, notifying other caches to evict matching SHARED blocks...\n");
			for(int i = 0; i != NUM_CORES; i++) {
				otherCache = mcc->caches+i;
				if(otherCache == c) continue;
				otherSet = getSet(otherCache, setID);			//get corresponding set
				otherEntry = getEntry(otherSet, entryID);   //get corresponding entry
				//if there is a matching modified entry, write that entry to memory
				//also copy it to the current entry, and then invalide the other entry
				if(otherEntry->state == SHARED) {
					if(debug) printf("      -CORRESPONDING SHARED BLOCK FOUND IN CACHE ID %d! Invalidating and evicting...\n",otherCache->cacheID);
					//if(debug) printf("        -Matching block found in cache %d in SHARED state! Evicting entry from that cache...");
					otherCache->numWritesToMem++;
					// other->numWriteBacksDueToAccessNeed++;
					otherCache->numBlocksInvalidated++;
					otherEntry->state = INVALID; 
					modifiedBlockFound = 1;
					otherEntry->valid = otherEntry->dirty = otherEntry->tag = otherEntry->LRUCounter = 0;
				}
			}
			
			if(!modifiedBlockFound) {
				if(debug) printf("    -No corresponding shared block, so reading from memory store (instead of other cache) to this cache...\n", otherCache->cacheID, c->cacheID);
			}
			
			c->numWriteBacksDueToAccessNeed++;
			c->numWritesToMem++;
 	}
	
	/*** write to memory depending on policy selected (do after handling state so we don't prematurely write to a modified block in another cache) ***/
		
	if(c->writePolicy == WRITE_THRU) {
		if(debug) printf("    -Write-thru policy selected, writing new value to memory...\n");
		c->numWritesToMem++;
		c->NumWritesBacksDueToWriteThruPolicy++;		
	} //check if need to evict valid & dirty block
	 else if(c->writePolicy == 'B' && e->valid && e->dirty) { 
		if(debug) printf("    -Write-back policy selected and dirty block selected, writing old block to memory and evicting from current cache #%d...\n", c->cacheID);
		c->numWriteBacksDueToReadMiss++;
		c->numWritesToMem++;
	} else {
		if(debug) printf("    -Write-back policy selected but non-dirty block selected, writing new value to cache but not to memory...\n");
	}
	
	/*** finally, we reset the current entries LRU counter, set it to valid and dirty, and update its tag  ***/
	
	updateLRUs(s,e); //increment LRU counter for all entries in set except e, which becomes 0
	e->tag = newTag;
	e->state = MODIFIED;
	e->dirty = 1;
	e->valid = 1;	
}

/****************** handle single cache entry ******************/


//return true (1) if valid mode; false (0) otherwise
int handleCacheEntry(MulticoreCache *mcc, int coreID, unsigned int byteAddress, char mode) {
	Cache *c = mcc->caches+coreID;
	Set *s;
	unsigned int blockAddress, index, offsetFull, offsetEntry, offsetByte, tag;
	int setID, entryID;
				
	c->numInstructions++;
	
	/*** make sure we are reading a valid memory operation ***/	
	if(mode != READ_OP && mode != WRITE_OP)
		return 0;
		
	/*** parse address into tag, set index, and offset ***/	
	tag = (byteAddress >> (ADDRESS_LENGTH - c->tagLength)); 	//get the tagLength MSBs (drop the LSB)
	
	blockAddress = byteAddress / c->blockSize; 							//pg 390
	index = blockAddress % c->numSets; 									//set index; pg 404
	//offsetFull = byteAddress % (unsigned int) pow(2, c->offsetLength);	//byte offset
		
	/*** fetch the correct set  ***/	
	s = (c->sets+index); //sweet, sweet pointer arithmetic	
	
	if(mode == READ_OP) { //valid read
		handleRead(mcc, c, s, tag);
	} else { 			
		handleWrite(mcc, c, s, tag);
	} //end mode if
	
	if(debug)printf("\n");
}

/****************** simulate cache fcn ******************/

void calculateFinalValues(MulticoreCache *mcc) {	
	Cache *c;
	for(int i = 0; i != NUM_CORES; i++) {
		c = mcc->caches+i;
		c->hitRatio = (double) c->numHits / (double) c->numInstructions;
		
		double hitTime = (double) NUM_CYCLES_PER_HIT;
		double missPenalty = (double) c->numCyclesPerMiss;
		double missRatio = 1.0 - c->hitRatio; 
		c->avgMemAccessTime = hitTime + missRatio * missPenalty;
	}
}



void simulateCacheFromTraceFile(FILE *file, MulticoreCache* mcc) {
	Cache *c;
	unsigned int binAddress, coreID;	
	char mode;		
	
	printf("\nNow simulating cache from trace file...\n\n");
	
	while (!feof(file)) {
		//read the core/cache ID, data address, and mode (R/W)
		fscanf(file, "%u %x %c\n", &coreID, &binAddress, &mode);		
		if(debug) printf("%u %x %c\n", coreID, binAddress, mode);
		
		handleCacheEntry(mcc, coreID, binAddress, mode);
   }
   
   calculateFinalValues(mcc);
	
	fclose(file);
}

/****************** main ******************/

int main(int argc, char** argv) {
	MulticoreCache mcc;
	// Cache cache;
	FILE* file;
	int code, blockSize = 1, totalNumDataWords = 1024, numCyclesPerMiss = 100, setAssociativity = 1; 
	char writePolicy = 'T';
	
	//open file
	if(!(file = fopen(argv[argc-1], "r"))) {
		printf("Failed to open file\n");
	 	return 2; 
	}	
	
	/*** process program arguments ***/	
	if(code = processProgArgs(argv, argc, &blockSize, &totalNumDataWords, &numCyclesPerMiss, &setAssociativity, &writePolicy))
		return code;
	
	/*** initiate and simulate cache ***/		
	initMulticoreCache(&mcc, (unsigned int) blockSize, (unsigned int) totalNumDataWords, (unsigned int) numCyclesPerMiss, (unsigned int) setAssociativity, writePolicy);
	
	simulateCacheFromTraceFile(file, &mcc);

	/*** print cache statistics and free dynamically allocated memory ***/				
	//if(debug) printCacheInit(mcc.caches);			
	printCacheStats(&mcc); 
		
	freeMCC(&mcc);
	
	return 0;
}
