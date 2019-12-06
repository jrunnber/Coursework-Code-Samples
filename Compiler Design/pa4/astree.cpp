// $Id: astree.cpp,v 1.9 2017-10-04 15:59:50-07 - - $

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astree.h"
#include "string_set.h"
#include "lyutils.h"

astree::astree (int symbol_, const location& lloc_, const char* info) {
   symbol = symbol_;
   lloc = lloc_;
   lexinfo = string_set::intern (info);
   // vector defaults to empty -- no children
}

astree::~astree() {
   while (not children.empty()) {
      astree* child = children.back();
      children.pop_back();
      delete child;
   }
   if (yydebug) {
      fprintf (stderr, "Deleting astree (");
      astree::dump (stderr, this);
      fprintf (stderr, ")\n");
   }
}

astree* astree::adopt (astree* child1, astree* child2) {
   if (child1 != nullptr) children.push_back (child1);
   if (child2 != nullptr) children.push_back (child2);
   return this;
}

astree* astree::adopt_sym (astree* child, int symbol_) {
   symbol = symbol_;
   return adopt (child);
}

astree* astree::subst_sym (astree* tree, int new_symbol) {
   tree->symbol = new_symbol;
   return tree;
}


void astree::dump_node (FILE* outfile) {
   fprintf (outfile, "%zd  %zd.%zd  %d  %s\t(%s)",
            lloc.filenr, lloc.linenr, -1*(lloc.offset), this->symbol, parser::get_tname(symbol), lexinfo->c_str());
   for (size_t child = 0; child < children.size(); ++child) {
      fprintf (outfile, " %p", children.at(child));
   }
}

void astree::dump_tree (FILE* outfile, int depth) {
   fprintf (outfile, "%*s", depth * 3, "");
   dump_node (outfile);
   fprintf (outfile, "\n");
   for (astree* child: children) child->dump_tree (outfile, depth + 1);
   fflush (nullptr);
}

void astree::dump (FILE* outfile, astree* tree) {
   if (tree == nullptr) fprintf (outfile, "nullptr");
                   else tree->dump_node (outfile);
}

void print_attr(FILE* outfile, int i){
   printf("doop");
   if(i == 0){
      fprintf(outfile, "VOID ");
   } else if(i == 1){
      fprintf(outfile, "INT ");
   } else if(i == 2){
      fprintf(outfile, "NULLX ");
   } else if(i == 3){
      fprintf(outfile, "STRING ");
   } else if(i == 4){
      fprintf(outfile, "STRUCT ");
   } else if(i == 5){
      fprintf(outfile, "ARRAY ");
   } else if(i == 6){
      fprintf(outfile, "FUNCTION ");
   } else if(i == 7){
      fprintf(outfile, "VARIABLE ");
   } else if(i == 8){
      fprintf(outfile, "FIELD ");
   } else if(i == 9){
      fprintf(outfile, "TYPEID ");
   } else if(i == 10){
      fprintf(outfile, "PARAM ");
   } else if(i == 11){
      fprintf(outfile, "LOCAL ");
   } else if(i == 12){
      fprintf(outfile, "LVAL ");
   } else if(i == 13){
      fprintf(outfile, "CONST ");
   } else if(i == 14){
      fprintf(outfile, "VREG ");
   } else if(i == 15){
      fprintf(outfile, "VADDR ");
   } else {
      fprintf(outfile, "BITSET_SIZE ");
   }
}

void print_attrs(FILE* outfile, attr_bitset attributes){
   for(size_t i = 0; i < BITSET_SIZE; i++){
      if(attributes[i])
         print_attr(outfile, i);
   }
   fprintf(outfile, "\n");
}

void astree::print (FILE* outfile, astree* tree, int depth) {
   for(int i = 0; i < depth; i++)
      fprintf(outfile, "|  ");
   //fprintf (outfile, "; %*s", depth * 3, "|");
   fprintf (outfile, "%s \"%s\" %zd.%zd.%zd ",
            parser::get_tname (tree->symbol), tree->lexinfo->c_str(),
            tree->lloc.filenr, tree->lloc.linenr, -1*(tree->lloc.offset));
   print_attrs(outfile, tree->attributes);
   for (astree* child: tree->children) {
      astree::print (outfile, child, depth + 1);
   }
}

void destroy (astree* tree1, astree* tree2) {
   if (tree1 != nullptr) delete tree1;
   if (tree2 != nullptr) delete tree2;
}

void errllocprintf (const location& lloc, const char* format,
                    const char* arg) {
   static char buffer[0x1000];
   assert (sizeof buffer > strlen (format) + strlen (arg));
   snprintf (buffer, sizeof buffer, format, arg);
   errprintf ("%s:%zd.%zd: %s", 
              lexer::filename (lloc.filenr), lloc.linenr, lloc.offset,
              buffer);
}