
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <bitset>

#include "auxlib.h"
#include "lyutils.h"
#include "astree.h"
#include "symtable.h"

extern symbol_table* globTable;
extern FILE* symOutfile;
size_t next_block = 1;

symbol_table* globTable = new symbol_table;

const string to_string (attr attribute) {
	static const unordered_map<attr,string> hash {
		{attr::VOID 		, "void" },
		{attr::INT 			, "int" },
		{attr::NULLX 		, "null" },
		{attr::STRING 		, "string" },
		{attr::STRUCT 		, "struct" },
		{attr::ARRAY 		, "array" },
		{attr::FUNCTION 	, "function" },
		{attr::VARIABLE 	, "variable" },
		{attr::FIELD 		, "field" },
		{attr::TYPEID 		, "typeid" },
		{attr::PARAM 		, "param" },
		{attr::LVAL 		, "lval" },
		{attr::CONST 		, "const" },
		{attr::VREG 		, "vreg" },
		{attr::VADDR 		, "vaddr" },
		{attr::BITSET_SIZE 	, "bitset_size"},
	};
	auto str = hash.find (attribute);
	if(str == hash.end()) throw invalid_argument (__PRETTY_FUNCTION__);
	return str->second;
}

symbol* new_symbol (astree* node) {
    symbol* symbol;// = new symbol();
    symbol->attributes = node->attributes;
    symbol->fields = nullptr;
    symbol->lloc = node->lloc;
    symbol->block_nr = node->blocknr;
    symbol->parameters = nullptr;
    return symbol;
}

void print_symbol (astree* node) {
    attr_bitset attributes = node->attributes;
    
    char* str = new char[node->lexinfo->size() + 1];
	std::copy(node->lexinfo->begin(), node->lexinfo->end(), str);
	str[node->lexinfo->size()] = '\0';

    if (node->attributes[STRUCT]) {
        fprintf (symOutfile, "\n");
    } else {
        fprintf (symOutfile, "    ");
    }

    if (node->attributes[FIELD]) {
        fprintf (symOutfile, "%s (%zu.%zu.%zu) field {%s} ",
            str,
            node->lloc.linenr, node->lloc.filenr, -1*node->lloc.offset,
            str);
    } else {
        fprintf (symOutfile, "%s (%zu.%zu.%zu) {%zu} ",
            str,
            node->lloc.linenr, node->lloc.filenr, -1*node->lloc.offset,
            node->blocknr);
    }

    if (node->attributes[STRUCT]) {
        fprintf (symOutfile, "struct \"%s\" ", str);
    }

    print_attrs(symOutfile, node->attributes);
}

/*void insert_symbol (symbol_table* table, astree* node) {
    symbol* symbol = new_symbol(node);
    if((node != NULL) && (table != NULL)) {
        table->insert(symbol_entry(node->lexinfo, symbol));
    }
}
*/
/*symbol* find_ident (symbol_table* table, astree* node) {
    if(table->count(node->lexinfo) == 0) {
        return nullptr;
    }
    return (table->find(node->lexinfo))->second;
}*/

int is_primitive (astree* left, astree* right) {
    for(size_t i = 0; i < attr::FUNCTION; i++) {
        if(left->attributes[i] == 1 && right->attributes[i] == 1) {
            return 1;
        }
    }
    return 0;
}

void passUp_prim (astree* parent, astree* child) {
    for(size_t i = 0; i < FUNCTION; i++) {
        if(child->attributes[i] == 1) {
            parent->attributes[i] = 1;
        }
    }
}

void passUp_any (astree* parent, astree* child) {
    for(size_t i = 0; i < BITSET_SIZE; i++) {
        if(child->attributes[i] == 1) {
            parent->attributes[i] = 1;
        }
    }
}

/*void check_blocks (astree* node){
	if(node->symbol == TOK_BLOCK) next_block++;
	node->blocknr = (next_block - 1);
	for(auto child : node->children){
		check_blocks(child);
	}
}
*/
/*enum class attr {
   VOID, INT, NULLX, STRING, STRUCT, ARRAY, FUNCTION, VARIABLE, FIELD,
   TYPEID, PARAM, LOCAL, LVAL, CONST, VREG, VADDR, BITSET_SIZE,
};*/

void typecheck (astree* node) {
	astree* leftChild;
	astree* rightChild;
	symbol* symbol;

	if(node->children.size() > 0)
		leftChild = node->children[0];
	if(node->children.size() > 1)
		rightChild = node->children[1];

	switch(node->symbol){
		case TOK_VOID: leftChild->attributes[VOID] = 1;
			break;
		case TOK_CHAR: 
		case TOK_INT: 
			if(leftChild == nullptr)
				break;
			leftChild->attributes[INT] = 1;
			passUp_prim(node, leftChild);
			break;
		case TOK_STRING:
			if(leftChild == nullptr)
				break;
			leftChild->attributes[STRING] = 1;
			passUp_prim(node, leftChild);
			break;
		case TOK_IF:
		case TOK_IFELSE:
		case TOK_WHILE:
		case TOK_RETURN:
			break;
		case TOK_STRUCT:
			leftChild->attributes[STRUCT] = 1;
			//insert_symbol(globTable, leftChild);
			print_symbol(leftChild);	
			//symbol* symbol = find_ident(globTable, leftChild);
			//symbol->fields = new symbol_table;
			for(uint i = 0; i < node->children.size(); i++){
				astree* nChild = node->children[i];
				//insert_symbol(symbol->fields, nChild);
				print_symbol(nChild->children[0]);
			}
			break;
		case TOK_NULL:
			node->attributes[NULLX] = 1;
			node->attributes[CONST] = 1;
			break;
		case TOK_NEW:
			passUp_any(node, leftChild);
			break;
		case TOK_ARRAY:
			leftChild->attributes[ARRAY] = 1;
			if(leftChild == nullptr || leftChild->children.size() == 0)
				break;
			leftChild->children[0]->attributes[ARRAY] = 1;
			break;
		case TOK_LE:
		case TOK_LT:
		case TOK_GE:
		case TOK_GT:
		case TOK_EQ:
		case TOK_NE:
			node->attributes[VREG] = 1;
			passUp_prim(node, leftChild);
			break;
		case TOK_IDENT:
			// search local symbol = find_ident(node);
			// if not local go global if(symbol == nullptr)
				//find_ident(globTable, node);
			/*if(symbol == nullptr){
				printf("myabe local");
				break; 
			}/
			node->attributes = symbol->attributes;
			break;*/
		case TOK_INTCON:
		case TOK_CHARCON:
			node->attributes[INT] = 1;
			node->attributes[CONST] = 1;
			break;
		case TOK_STRINGCON:
			node->attributes[STRING] = 1;
			node->attributes[CONST] = 1;
			break;
		case TOK_BLOCK:
			//next_block++;
			break;
		case TOK_CALL:
			/*symbol = find_ident(globTable, node);
			if(symbol == nullptr){
				printf("call fialed");
				break;
			}
			for(size_t i = 0; i < FUNCTION; i++){
				if(symbol->attributes[i] == 1)
					node->attributes[i] = 1;
			}*/
			break;
		case TOK_NEWARRAY:
			node->attributes[VREG] = 1;
			node->attributes[ARRAY] = 1;
			passUp_prim(node, leftChild);
			break;
		case TOK_TYPEID:
			node->attributes[TYPEID] = 1;
			break;
		case TOK_FIELD:
			node->attributes[FIELD] = 1;
			if(leftChild != nullptr){
				leftChild->attributes[FIELD] = 1;
				passUp_prim(node, leftChild);
			}
			break;
		case TOK_ROOT:
		case TOK_PARAM:
		case TOK_DECLID:
			break;
		case TOK_PROTO:
		case TOK_FUNCTION:
			leftChild->children[0]->attributes[FUNCTION] = 1;
			//insert_symbol(globTable, leftChild->children[0]);
			for(uint i = 0; i < rightChild->children.size(); i++){
				astree* nChild = rightChild->children[i];
				nChild->children[0]->attributes[VARIABLE] = 1;
				nChild->children[0]->attributes[LVAL] = 1;
				nChild->children[0]->attributes[PARAM] = 1;
				nChild->children[0]->blocknr = next_block;
				node->attributes[FUNCTION] = 1;
				//passUp_any(node, nChild->children[0]);
				print_symbol(node);
			}
			break;
		case TOK_INDEX:
			node->attributes[LVAL] = 1;
			node->attributes[VADDR] = 1;
			break;
		case TOK_NEWSTR:
			node->attributes[VREG] = 1;
			node->attributes[STRING] = 1;
			break;
		case TOK_INITDECL:
			leftChild->children[0]->attributes[LVAL];
			leftChild->children[0]->attributes[VARIABLE];
			passUp_any(node, leftChild);
			print_symbol(leftChild->children[0]);
			break;
		case '=':
			if(leftChild == nullptr)
				break;
			if(leftChild->attributes[LVAL] && rightChild->attributes[VREG]){
				passUp_prim(node, leftChild);
				node->attributes[VREG] = 1;
			} else {
				//printf("= error or somethin");
			}
			break;
		case '-':
		case '+':
			node->attributes[VREG] = 1;
			node->attributes[INT] = 1;
			if(rightChild == nullptr){
				if(leftChild == nullptr)
					break;
				if(!(leftChild->attributes[INT])){
					//printf("-/+ error");
				}
			} else {
				if(!(leftChild->attributes[INT]) || !(rightChild->attributes[INT])){
					//printf("-/+ error 2");
				}
			}
			break;
		case '*':
		case '/':
		case '%':
			node->attributes[VREG] = 1;
			node->attributes[INT] = 1;
			if(!(leftChild->attributes[INT]) || !(rightChild->attributes[INT])){
				//printf("*/ error");
			}
			break;
		case '!':
			node->attributes[VREG] = 1;
			node->attributes[INT] = 1;
			if(!(leftChild->attributes[INT])){
				//printf("! error");
			}
			break;
		default:
			//printf("DEFAULT OH NO");
	}
}

void postOrder_typecheck(astree* root){
	for(uint i = 0; i < root->children.size(); i++){
		astree* nChild = root->children[i];
		postOrder_typecheck(nChild);
	}
	typecheck(root);
} 