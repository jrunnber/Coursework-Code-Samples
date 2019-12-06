#ifndef __SYMTABLE_H__
#define __SYMTABLE_H__

#include <string>
#include <vector>
#include <bitset>
using namespace std;

#include "auxlib.h"
#include "lyutils.h"
#include "astree.h"

extern size_t next_block;

using symbol_entry = pair<const string*,symbol*>;//symbol_table::value_type;

struct symbol {
	attr_bitset attributes;
	size_t sequence;
	symbol_table* fields;
	location lloc;
	size_t block_nr;
	vector<symbol*>* parameters;
};

void postOrder_typecheck(astree* root);

#endif