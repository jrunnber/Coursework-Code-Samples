%{
// $Id: parser.y,v 1.15 2018-04-06 15:14:40-07 - - $
// Dummy parser for scanner project.

#include <cassert>

#include "lyutils.h"
#include "astree.h"

%}

%debug
%defines
%error-verbose
%token-table
%verbose

%token TOK_VOID TOK_CHAR TOK_INT TOK_STRING
%token TOK_IF TOK_ELSE TOK_WHILE TOK_RETURN TOK_STRUCT
%token TOK_NULL TOK_NEW TOK_ARRAY TOK_ARROW TOK_NOT
%token TOK_EQ TOK_NE TOK_LT TOK_LE TOK_GT TOK_GE
%token TOK_IDENT TOK_INTCON TOK_CHARCON TOK_STRINGCON

%token TOK_ROOT TOK_BLOCK TOK_CALL TOK_IFELSE TOK_INITDECL
%token TOK_POS TOK_NEG TOK_NEWARRAY TOK_TYPEID TOK_FIELD

%token TOK_DECLID TOK_INDEX TOK_NEWSTR 
%token TOK_FUNCTION TOK_PARAM TOK_PROTO TOK_FNBODY

//Grammar of OC
%right TOK_IF TOK_ELSE
%right '='
%left  TOK_EQ TOK_NE TOK_LT TOK_LE TOK_GT TOK_GE
%left  '+' '-'
%left  '*' '/' '%'
%right TOK_POS TOK_NEG '!' TOK_NEW
%left  TOK_ARRAY TOK_FIELD TOK_FUNCTION 


/* Asg 2
program : program token | ;
token   : '(' | ')' | '[' | ']' | '{' | '}' | ';' | ','
        | '=' | '+' | '-' | '*' | '/' | '%' | '!'
        | TOK_ROOT TOK_VOID | TOK_CHAR | TOK_INT | TOK_STRING
        | TOK_IF | TOK_ELSE | TOK_WHILE | TOK_RETURN | TOK_STRUCT
        | TOK_NULL | TOK_NEW | TOK_ARRAY | TOK_ARROW | TOK_NOT
        | TOK_EQ | TOK_NE | TOK_LT | TOK_LE | TOK_GT | TOK_GE
        | TOK_IDENT | TOK_INTCON | TOK_CHARCON | TOK_STRINGCON
        ;
*/

%start start

%%
//Config Options
start     : program             { parse_root = $1; }
          ;

program   : program structdef   { $$ = $1->adopt($2); }
          | program function    { $$ = $1->adopt($2); }
          | program globaldeclr { $$ = $1->adopt($2); }
          | program error '}'   { $$ = $1; }
          | program error ';'   { $$ = $1; }
          |                     { $$ = create_root(); }
          ;

basetype    : TOK_VOID   { $$ = $1; }
            | TOK_INT    { $$ = $1; }
            | TOK_STRING { $$ = $1; }
            | TOK_IDENT  { $$ = $1->subst_sym($1, TOK_TYPEID); }
            ;

globaldeclr     : identdeclr '=' constant ';'
              { 
                  destroy($4);
                  $2 = $2->subst_sym($2, TOK_DECLID);
                  $$ = $2->adopt($1, $3);
              }
            ;

localdeclr     : identdeclr '=' expression ';'
              { 
                  destroy($4);
                  $2 = $2->subst_sym($2, TOK_DECLID);
                  $$ = $2->adopt($1, $3);
              }
            ;

identdeclr  : basetype TOK_IDENT
              { 
                  $2 = $2->subst_sym($2, TOK_DECLID);
                  $$ = $1->adopt($2);
              }
            | basetype TOK_ARRAY TOK_IDENT
              { 
                  $3 = $3->subst_sym($3, TOK_DECLID);
                  $$ = $2->adopt($1, $3);
              }
            ;

fielddeclr  : basetype TOK_IDENT
              {
                  $2 = $2->subst_sym($2, TOK_FIELD);
                  $$ = $1->adopt($2);
              }
            | basetype TOK_ARRAY TOK_IDENT
              {
                  $3 = $3->subst_sym($3, TOK_FIELD);
                  $$ = $2->adopt($1, $3);
              }
            ;

structstates: '{' fielddeclr ';'
              {
                  destroy($3);
                  $$ = $1->adopt($2);
              }
            | structstates fielddeclr ';'
              {
                  destroy($3);
                  $$ = $1->adopt($2);
              }
            ;

structdef   : TOK_STRUCT TOK_IDENT structstates '}'
              {
                  destroy($4);
                  $2 = $2->subst_sym($2, TOK_TYPEID);
                  $$ = $1->adopt($2, $3);
              }
            | TOK_STRUCT TOK_IDENT '{' '}'
              {
                  destroy($3, $4);
                  $2 = $2->subst_sym($2, TOK_TYPEID);
                  $$ = $1->adopt($2);
              }
            ;

binop       : TOK_EQ          { $$ = $1; }
            | TOK_NE          { $$ = $1; }
            | TOK_LT          { $$ = $1; }
            | TOK_LE          { $$ = $1; }
            | TOK_GT          { $$ = $1; }
            | TOK_GE          { $$ = $1; }
            | '+'             { $$ = $1; }
            | '-'             { $$ = $1; }
            | '*'             { $$ = $1; }
            | '/'             { $$ = $1; }
            | '='             { $$ = $1; }
            ;

unop        : TOK_POS         { $$ = $1; }
            | TOK_NEG         { $$ = $1; }
            | '!'             { $$ = $1; }
            | TOK_NEW         { $$ = $1; }
            ;

expression  : expression binop expression { $$ = $2->adopt($1, $3); }
            | unop expression             { $$ = $$->adopt($2); }
            | allocation                  { $$ = $1; }
            | call                        { $$ = $1; }
            | '(' expression ')'    
              {
                   destroy($1, $3);
                   $$ = $2;
              }
            | variable        { $$ = $1; }
            | constant        { $$ = $1; }
            ;

variadic    : TOK_IDENT '(' expression
              {
                  $2 = $2->subst_sym($2, TOK_CALL);
                  $$ = $2->adopt($1, $3);
              }
            | variadic ',' expression
              { 
                  destroy($2);
                  $$ = $1->adopt($3);
              }
            ;

call        : variadic ')'
              {
                  destroy($2);
                  $$ = $1;
              }
            | TOK_IDENT '(' ')'
              {
                  destroy($3);
                  $2 = $2->subst_sym($2, TOK_CALL);
                  $$ = $2->adopt($1);
              }        
            ;

allocation  : TOK_NEW TOK_TYPEID
              {
                  $2 = $2->subst_sym($2, TOK_TYPEID);
                  $$ = $1->adopt($2);
              }
            | TOK_NEW TOK_STRING '(' expression ')'
              {
                  destroy($3, $5);
                  $1 = $1->subst_sym($1, TOK_NEWSTR);
                  $$ = $1->adopt($4);
              }
            | TOK_NEW basetype '[' expression ']'
              {
                  destroy($3, $5);
                  $1 = $1->subst_sym($1, TOK_NEWARRAY);
                  $$ = $1->adopt($2, $4);
              }
            ;

variable    : TOK_IDENT          { $$ = $1; }
            | expression '[' expression ']'  
              { 
                  destroy($4);
                  $2 = $2->subst_sym($2, TOK_INDEX);
                  $$ = $2->adopt($1, $3);
              }
            | expression TOK_ARROW TOK_FIELD 
              {
                  $3 = $3->subst_sym($3, TOK_FIELD);
                  $$ = $2->adopt($1, $3);
              }
            ;

constant    : TOK_INTCON         { $$ = $1; }
            | TOK_CHARCON        { $$ = $1; }
            | TOK_STRINGCON      { $$ = $1; }
            | TOK_NULL           { $$ = $1; }
            ;

statement   : block      { $$ = $1; }
            | while      { $$ = $1; }
            | ifelse     { $$ = $1; }
            | return     { $$ = $1; }
            | expression ';' 
              {
                  destroy($2);
                  $$ = $1;
              }
            | ';'      { $$ = $1; }
            ;

while       : TOK_WHILE '(' expression ')' statement
              { 
                  destroy($2, $4);
                  $$ = $1->adopt($3, $5);
              }
            ;

ifelse      : TOK_IF '(' expression ')' statement TOK_ELSE statement
              {
                  destroy($2, $4);
                  $1->subst_sym($1, TOK_IFELSE);
                  $$ = $1->adopt($3, $5);
                  $$ = $$->adopt($7);
              }
            | TOK_IF '(' expression ')' statement
              {
                  destroy($2, $4);
                  $$ = $1->adopt($3, $5);
              }
            ;

return      : TOK_RETURN ';'
              {
                  destroy($2);
                  $$ = $1->subst_sym($1, TOK_RETURN);
              }
            | TOK_RETURN expression ';'
              { 
                  destroy($3);
                  $$ = $1->adopt($2);
              }
            ;

fnbody       :  fn1 fn2 '}'
              {
                  destroy($3);
                  $$ = $1->adopt($2);
              }
            | '{' '}'
              { 
                  destroy($2);
                  $$ = $1->subst_sym($1, TOK_FNBODY);
              }
            ;

fn1         : '{' localdeclr
              { 
                  $1 = $1->subst_sym($1, TOK_FNBODY);
                  $$ = $1->adopt($2);
              }
            | fn1 localdeclr
              { 
                  $$ = $1->adopt($2); 
              }
            ;

fn2         : statement
              { 
                  $$ = $1;
              }
            | fn2 statement
              { 
                  $$ = $1->adopt($2);
              }
            ;


block       :  blockbody '}'
              {
                  destroy($2);
                  $$ = $1->subst_sym($1, TOK_BLOCK);
              }
            | '{' '}'
              { 
                  destroy($2);
                  $$ = $1->subst_sym($1, TOK_BLOCK);
              }
            ;

blockbody        : '{' statement
              { 
                  $1 = $1->subst_sym($1, TOK_BLOCK);
                  $$ = $1->adopt($2);
              }
            | blockbody statement
              { 
                  $$ = $1->adopt($2); 
              }
            ;

parameters  : '(' identdeclr
              {
                  $1 = $1->subst_sym($1, TOK_PARAM);
                  $$ = $1->adopt($2);
              }
            | parameters ',' identdeclr
              {
                  destroy($2);
                  $$ = $1->adopt($3);
              }
            ;

//Double check all cases covered
function    :  identdeclr '(' ')' ';'
              {
                  destroy($3, $4);
                  $2 = $2->subst_sym($2, TOK_PARAM);
                  $$ = new astree(TOK_PROTO, $1->lloc, "");
                  $$ = $$->adopt($1, $2);
              }
            | identdeclr '(' ')' fnbody
              {
                  destroy($3);
                  $2 = $2->subst_sym($2, TOK_PARAM);
                  $$ = new astree(TOK_FUNCTION, $1->lloc, "");
                  $$ = $$->adopt($1, $2);
                  $$ = $$->adopt($4);
               }
            | identdeclr parameters ')' ';'
              {
                  destroy($3, $4);
                  $$ = new astree(TOK_PROTO, $1->lloc, "");
                  $$ = $$->adopt($1, $2);
              }
            | identdeclr parameters ')' fnbody
              {
                  destroy($3);
                  $$ = new astree(TOK_FUNCTION, $1->lloc, "");
                  $$ = $$->adopt($1, $2);
                  $$ = $$->adopt($4);
              }
            ;



%%

const char *parser::get_tname (int symbol) {
   return yytname [YYTRANSLATE (symbol)];
}


bool is_defined_token (int symbol) {
   return YYTRANSLATE (symbol) > YYUNDEFTOK;
}
