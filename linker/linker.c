#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAXLEN 1000
#define MAXMEM 512
#define MAXSYMB 16
#define MAXDEF 16

static const char delims[] = " \t\n";
static const char instType[] = "IAER";
static int numTokens = 0;
static int totalSymbCount = 0;

// tar -xZvf lab1samples.tz

struct token {

	char val[100];
	int lineoffset;
	int linenum;
};

struct symbol {

	char errorMsg[70];
	char val[MAXSYMB+1];
	int addr;
	int modDef; // module the symbol is defined it
	char used; // whether symbol is used in an E instruction
};

struct module {

	char warningMsg[80];
	int val;
	int addr;	
	int size;
};

struct instruction {

	char errorMsg[80];
	int val;
	int addr;
	int modDef; // module the instruction is defined it
};

struct pass1Data {

	struct symbol* symbolTable;
	struct module* moduleTable;
};

void readSymbol(struct token tok);
void readInt(struct token tok);
void parseerror(int errcode, struct token tok);
void readIAER(struct token tok);
void printPass1(struct module* moduleTable, struct symbol* symbolTable, int modCount, int symbCount);
void printPass2(struct module* moduleTable, struct instruction* instructionTable, int modCount, int instructionCount);
void pass2(struct pass1Data* data1, char* filename);
void checkAllSymbolsUsed(struct symbol* symbolTable);
void checkSymbolAddr(struct module* moduleTable, struct symbol* defList, struct symbol* symbolTable, int modCount, int defCount, int symbCount);
void checkUseList(struct symbol* useList, int useCount, struct module* moduleTable, int modCount);
void setSymbolUsed(struct symbol* symbolTable, struct symbol use);
void deletePass1Data(struct pass1Data* data1);
int getSymbolAddr(struct symbol* symbolTable, struct symbol use);
int alreadyDefined(struct token tok, struct symbol* symbolTable, int symbCount);
int notDefined(struct symbol* symbolTable, struct symbol use);
struct symbol* createSymbolArray();
struct module* createModuleTable();
struct instruction* createInstructionTable();
struct pass1Data* createPass1Data();
struct token* createTokens();
struct token* tokenize(char* filename);
struct pass1Data* pass1(char* filename);


int main(int argc, char* argv[]) {

	struct pass1Data* data1 = pass1(argv[1]);
	pass2(data1, argv[1]);
	deletePass1Data(data1);

	return 0;
}


// stores absolute addresses of symbols and base addresses of modules
struct pass1Data* pass1(char* filename) {

	struct token* tokens = tokenize(filename);
	struct symbol* symbolTable = createSymbolArray(MAXLEN);
	struct module* moduleTable = createModuleTable();
	struct pass1Data* data1 = createPass1Data();

	int defCount = 0, symbCount = 0, modCount = 0, baseAddr = 0, i = 0, useCount = 0, instructionCount = 0;
	// i is an index into the tokens array
	while(i < numTokens) {

		moduleTable[modCount].val = modCount + 1;
		moduleTable[modCount].addr = baseAddr;

		// read defList
		struct symbol* defList = createSymbolArray(MAXDEF);

		readInt(tokens[i]);
		defCount = atoi(tokens[i].val);

		if(defCount > MAXDEF) {
			parseerror(4, tokens[i]);
		}

		i++;
		for(int j = 0; j < defCount; j++) {

			// read variable name part
			readSymbol(tokens[i]);

			if(alreadyDefined(tokens[i], symbolTable, symbCount)) {
				i += 2;
				continue;			
			}

			symbolTable[symbCount].modDef = moduleTable[modCount].val;
			strcpy(symbolTable[symbCount].val, tokens[i].val);
			strcpy(defList[j].val, tokens[i].val);

			i++;

			// read relative address part
			readInt(tokens[i]);
			symbolTable[symbCount].addr = atoi(tokens[i].val) + moduleTable[modCount].addr; // plus the module's base addr...this gives absolute addr for symbol
			defList[j].addr = atoi(tokens[i].val); //defList contains only relative addr

			symbCount++;
			i++;
		}


		// readUseList
		readInt(tokens[i]);
		useCount = atoi(tokens[i].val); 

		if(useCount > MAXDEF) {
			parseerror(5, tokens[i]);
		}

		i++;
		for(int k = 0; k < useCount; k++) {
			readSymbol(tokens[i]);
			i++;
		}
	

		// progText
		readInt(tokens[i]);
		int modInstrCount = atoi(tokens[i].val);
		// increment total instruction count over all modules
		instructionCount += modInstrCount; 

		if(instructionCount > MAXMEM) {
			parseerror(6, tokens[i]);
		}

		i++;
		for(int l = 0; l < modInstrCount; l++) {

			readIAER(tokens[i]);
			i++;
			readInt(tokens[i]);
			baseAddr++;
			i++;
		}

		moduleTable[modCount].size = modInstrCount;
		checkSymbolAddr(moduleTable, defList, symbolTable, modCount, defCount, symbCount);

		free(defList);
		modCount++;
	}

	printPass1(moduleTable, symbolTable, modCount, symbCount);
	data1->symbolTable = symbolTable;
	data1->moduleTable = moduleTable;
	totalSymbCount = symbCount;
	free(tokens);

	return data1;
}


// takes metadata (symbol and module tables) from pass1 as input and parses instructions
void pass2(struct pass1Data* data1, char* filename) {

	// open file and read in tokens again, and retrieve metadata from pass1
	struct token* tokens = tokenize(filename);
	struct symbol* symbolTable = data1->symbolTable;
	struct module* moduleTable = data1->moduleTable;
	struct instruction* instructionTable = createInstructionTable();

	int defCount = 0, symbCount = 0, modCount = 0, baseAddr = 0, i = 0, useCount = 0, instructionCount = 0;
	// i is an index into the tokens array
	while(i < numTokens) {

		// reset module table warning messages
		strcpy(moduleTable[modCount].warningMsg, "");

		// skip over defList. No need to check parse errors here
		defCount = atoi(tokens[i].val);
		i += (2 * defCount) + 1;
		symbCount += defCount;


		// readUseList
		struct symbol* useList = createSymbolArray(MAXDEF);
		useCount = atoi(tokens[i].val); 
		i++;

		for(int k = 0; k < useCount; k++) {
			strcpy(useList[k].val, tokens[i].val);
			i++;
		}
	

		// progText
		int modInstrCount = atoi(tokens[i].val);
		i++;

		for(int l = 0; l < modInstrCount; l++) {

			instructionTable[instructionCount + l].addr = baseAddr;
			instructionTable[instructionCount + l].modDef = moduleTable[modCount].val;
			// rule 10, 11. 
			if(strlen(tokens[i+1].val) > 4) {

				instructionTable[instructionCount + l].val = 9999;

				if(!strcmp(tokens[i].val, "I")) {
					strcpy(instructionTable[instructionCount + l].errorMsg, "Error: Illegal immediate value; treated as 9999");
				} else {
					strcpy(instructionTable[instructionCount + l].errorMsg, "Error: Illegal opcode; treated as 9999");
				}

			} else {

				char type[2];
				strcpy(type, tokens[i].val);
				int opcode = atoi(tokens[i+1].val) / 1000;
				int operand = atoi(tokens[i+1].val) % 1000;

				if(!strcmp(type, "I")) {
					instructionTable[instructionCount + l].val = atoi(tokens[i+1].val);
				} else if(!strcmp(type, "A")) {
					// rule 8
					if(operand > MAXMEM) {
						strcpy(instructionTable[instructionCount + l].errorMsg, "Error: Absolute address exceeds machine size; zero used");
						instructionTable[instructionCount + l].val = opcode * 1000;
					} else {
						instructionTable[instructionCount + l].val = atoi(tokens[i+1].val);
					}
				} else if(!strcmp(type, "R")) {
					// rule 9. 
					if(operand > moduleTable[modCount].size - 1) {
						strcpy(instructionTable[instructionCount + l].errorMsg, "Error: Relative address exceeds module size; zero used");
						instructionTable[instructionCount + l].val = (opcode * 1000) + moduleTable[modCount].addr;
					} else {
						instructionTable[instructionCount + l].val = (opcode * 1000) + operand + moduleTable[modCount].addr;
					}
				} else if(!strcmp(type, "E")) {
					// rule 6
					if(operand > useCount - 1) {
						strcpy(instructionTable[instructionCount + l].errorMsg, "Error: External address exceeds length of uselist; treated as immediate");
						instructionTable[instructionCount + l].val = atoi(tokens[i+1].val);
					} 
					// rule 3
					else if(notDefined(symbolTable, useList[operand])) {
						sprintf(instructionTable[instructionCount + l].errorMsg, "Error: %s is not defined; zero used", useList[operand].val);
						instructionTable[instructionCount + l].val = opcode * 1000;
						useList[operand].used = 'y';
					} 
					// for correct E instructions, replace relative addr with external addr from symbolTable
					else {
						instructionTable[instructionCount + l].val = (opcode * 1000) + getSymbolAddr(symbolTable, useList[operand]);						
						useList[operand].used = 'y';
						setSymbolUsed(symbolTable, useList[operand]);
					}
				}
			}

			baseAddr++;
			i += 2;
		}

		// increment total instruction count over all modules
		instructionCount += modInstrCount; 
		checkUseList(useList, useCount, moduleTable, modCount);
		free(useList);
		modCount++;
	}

	printPass2(moduleTable, instructionTable, modCount, instructionCount);
	checkAllSymbolsUsed(symbolTable);
	free(instructionTable);
	free(tokens);
}

		
struct token* tokenize(char* filename) {

	struct token* tokens = createTokens();
	numTokens = 0;

	FILE* fp = fopen(filename,"r");
	if(!fp) {
		printf("Error: Could not open file\n");
		exit(1);
	}

	char line[MAXLEN];
	int linenum = 1;

	while(fgets(line, MAXLEN, fp)) {

		char copy[MAXLEN];
		strcpy(copy,line);
		char* token = strtok(copy, delims);
		int len = strlen(line);
		
		// i is the lineoffset
		int i = 0;

		while(i < len) {

			if(line[i] == ' ' || line[i] == '\t' || line[i] == '\n' || line[i] == '\r') {
				i++;
			} else {

				if(token) {
					numTokens++;
				}

				strcpy(tokens[numTokens-1].val, token);
				tokens[numTokens-1].linenum = linenum;
				tokens[numTokens-1].lineoffset = i + 1;

				int a = strlen(token);
				// next token
				token = strtok(NULL, delims);
				i += a;
			}
		}
	
		linenum++;
	}

	// The last token is empty by default and has the last linenum and rightmost offset on that line i.e. the length of that line
	tokens[numTokens].linenum = linenum - 1;
	tokens[numTokens].lineoffset = strlen(line);
	fclose(fp);

	return tokens;
}


struct symbol* createSymbolArray(int len) {

	struct symbol* symbolTable = (struct symbol*) malloc(sizeof(struct symbol) * len); 
	
	for(int i = 0; i < len; i++) {
		strcpy(symbolTable[i].errorMsg, "");
		strcpy(symbolTable[i].val, "");
		symbolTable[i].addr = 0;
		symbolTable[i].modDef = 0;
		symbolTable[i].used = 'n';
	}

	return symbolTable;
}


struct module* createModuleTable() {

	struct module* moduleTable = (struct module*) malloc(sizeof(struct module) * MAXLEN); 
	
	for(int i = 0; i < MAXLEN; i++) {
		strcpy(moduleTable[i].warningMsg, "");
		moduleTable[i].val = 0;
		moduleTable[i].addr = 0;
		moduleTable[i].size = 0;
	}

	return moduleTable;
}


struct instruction* createInstructionTable() {

	struct instruction* instructionTable = (struct instruction*) malloc(sizeof(struct instruction) * MAXMEM);
	
	for(int i = 0; i < MAXMEM; i++) {
		strcpy(instructionTable[i].errorMsg, "");
		instructionTable[i].val = 0;
		instructionTable[i].addr = 0;
		instructionTable[i].modDef = MAXMEM;
	}

	return instructionTable;
}


struct pass1Data* createPass1Data() {

	struct pass1Data* data1 = (struct pass1Data*) malloc(sizeof(struct pass1Data));
	data1->symbolTable = NULL;
	data1->moduleTable = NULL;

	return data1;
}


struct token* createTokens() {

	struct token* tokens = (struct token*) malloc(sizeof(struct token) * MAXLEN); 
	for(int i = 0; i < MAXLEN; i++) {
		strcpy(tokens[i].val, "");
		tokens[i].lineoffset = 0;
		tokens[i]. linenum = 0;
	}

	return tokens;
}


void printPass1(struct module* moduleTable, struct symbol* symbolTable, int modCount, int symbCount) {

	for(int i = 0; i < modCount; i++) {
		if(strcmp(moduleTable[i].warningMsg, "")) {
			printf("%s", moduleTable[i].warningMsg);
		}
	}

	printf("Symbol Table\n");
	for(int i = 0; i < symbCount; i++) {
		printf("%s=%d %s\n", symbolTable[i].val, symbolTable[i].addr, symbolTable[i].errorMsg);
	}

}


void printPass2(struct module* moduleTable, struct instruction* instructionTable, int modCount, int instructionCount) {

	printf("\nMemory Map\n");

	int curMod = instructionTable[0].modDef;
	for(int i = 0; i < instructionCount; i++) {
		printf("%03d: %04d %s\n", instructionTable[i].addr, instructionTable[i].val, instructionTable[i].errorMsg);
	
		if(instructionTable[i+1].modDef != curMod) {
			if(strcmp(moduleTable[instructionTable[i].modDef - 1].warningMsg, "")) {
				printf("%s\n", moduleTable[instructionTable[i].modDef - 1].warningMsg);
			}
			curMod = instructionTable[i].modDef;
		}
	}
	printf("\n");
}


// rule 4
void checkAllSymbolsUsed(struct symbol* symbolTable) {

	for(int i = 0; i < totalSymbCount; i++) {
		if(symbolTable[i].used == 'n') {
			printf("Warning: Module %d: %s was defined but never used\n", symbolTable[i].modDef, symbolTable[i].val);
		}
	}	
}


// rule 7. 
void checkUseList(struct symbol* useList, int useCount, struct module* moduleTable, int modCount) {

	for(int i = 0; i < useCount; i++) {
		if(useList[i].used == 'n') {
			strcpy(moduleTable[modCount].warningMsg, "");
			sprintf(moduleTable[modCount].warningMsg, 
				"Warning: Module %d: %s appeared in the uselist but was not actually used", 
				modCount + 1, useList[i].val);
		}
	}
}


// rule 5
void checkSymbolAddr(struct module* moduleTable, 
					struct symbol* defList, 
					struct symbol* symbolTable,
					int modCount, int defCount, int symbCount) {

	for(int i = 0; i < defCount; i++) {
		if(defList[i].addr > moduleTable[modCount].size - 1) {
			sprintf(moduleTable[modCount].warningMsg, 
				  "Warning: Module %d: %s too big %d (max=%d) assume zero relative\n", 
				  modCount + 1, defList[i].val, symbolTable[symbCount - defCount + i].addr, moduleTable[modCount].size - 1);
			defList[i].addr = moduleTable[modCount].addr;
			symbolTable[symbCount - defCount + i].addr = moduleTable[modCount].addr;
		}
	}
} 


// rule 2
int alreadyDefined(struct token tok, struct symbol* symbolTable, int symbCount) {

	for(int i = 0; i < symbCount; i++) {
		if(!strcmp(tok.val, symbolTable[i].val)) {
			strcpy(symbolTable[i].errorMsg, "Error: This variable is multiple times defined; first value used");
			return 1;
		} 
	}

	return 0;
}


// rule 3
int notDefined(struct symbol* symbolTable, struct symbol use) {

	for(int i = 0; i < totalSymbCount; i++) {
		if(!strcmp(symbolTable[i].val, use.val)) {
			return 0;
		}
	}
	return 1;
}


void setSymbolUsed(struct symbol* symbolTable, struct symbol use) {

	for(int i = 0; i < totalSymbCount; i++) {
		if(!strcmp(symbolTable[i].val, use.val)) {
			symbolTable[i].used = 'y';
		}
	}
}


int getSymbolAddr(struct symbol* symbolTable, struct symbol use) {

	for(int i = 0; i < totalSymbCount; i++) {
		if(!strcmp(symbolTable[i].val, use.val)) {
			return symbolTable[i].addr;
		}
	}
	return 0;
}


// rule 1
void parseerror(int errcode, struct token tok) {

	static char* errstr[] = {
		"NUM_EXPECTED", // Number expect
		"SYM_EXPECTED", // Symbol Expected
		"ADDR_EXPECTED", // Addressing Expected which is A/E/I/R
		"SYM_TOO_LONG", // Symbol Name is too long
		"TOO_MANY_DEF_IN_MODULE", // > 16
		"TOO_MANY_USE_IN_MODULE", // > 16
		"TOO_MANY_INSTR", // total num_instr exceeds memory size (512)
	};

	printf("Parse Error line %d offset %d: %s\n", tok.linenum, tok.lineoffset, errstr[errcode]);
	exit(1);
}


void readInt(struct token tok) {

	int len = strlen(tok.val);

	if(len == 0) {
		parseerror(0, tok);
	}

	for(int i = 0; i < len; i++) {
		if(!isdigit(tok.val[i])) {
			parseerror(0, tok);
		}
	}
}


void readSymbol(struct token tok) {

	int len = strlen(tok.val);

	if(len > MAXSYMB) {
		parseerror(3, tok);
	}

	if(isdigit(tok.val[0]) || len == 0) {
		parseerror(1, tok);
	}

	for(int i = 1; i < len; i++) {
		if(!isalnum(tok.val[i])) {
			parseerror(1, tok);
		}
	}
}


void readIAER(struct token tok) {

	int len = strlen(tok.val);
	if(len != 1) {
		parseerror(2, tok);
	}

	if(!strchr(instType, tok.val[0])) {
		parseerror(2, tok);
	}
}


void deletePass1Data(struct pass1Data* data1) {

	free(data1->symbolTable);
	free(data1->moduleTable);
	free(data1);

}
