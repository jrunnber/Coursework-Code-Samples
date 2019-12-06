// $Id: astree.h,v 1.7 2016-10-06 16:13:39-07 - - $

#ifndef __ASTREE_H__
#define __ASTREE_H__

#include <string>
#include <vector>
#include <bitset>
#include <unordered_map>

using namespace std;

#include "auxlib.h"

struct symbol;
using symbol_table = unordered_map<string*,symbol*>;

enum attr {
   VOID, INT, NULLX, STRING, STRUCT, ARRAY, FUNCTION, VARIABLE, FIELD,
   TYPEID, PARAM, LOCAL, LVAL, CONST, VREG, VADDR, BITSET_SIZE,
};

using attr_bitset = bitset<unsigned(attr::BITSET_SIZE)>;

struct location {
   size_t filenr;
   size_t linenr;
   size_t offset;
};

struct astree {

   // Fields.
   int symbol;                 // token code
   location lloc;              // source location
   const string* lexinfo;      // pointer to lexical information
   vector<astree*> children;   // children of this n-way node
   attr_bitset attributes;     // attributes
   size_t blocknr;             // block number
   symbol_table* struct_table; // struct table node

   // Functions.
   astree (int symbol, const location&, const char* lexinfo);
   ~astree();
   astree* adopt (astree* child1, astree* child2 = nullptr);
   astree* adopt_sym (astree* child, int symbol);
   astree* subst_sym (astree* tree, int new_symbol);
   void dump_node (FILE*);
   void dump_tree (FILE*, int depth = 0);
   static void dump (FILE* outfile, astree* tree);
   static void print (FILE* outfile, astree* tree, int depth = 0);
};

void destroy (astree* tree1, astree* tree2 = nullptr);

void errllocprintf (const location&, const char* format, const char*);

void print_attrs(FILE* outfile, attr_bitset attributes);

#endif
