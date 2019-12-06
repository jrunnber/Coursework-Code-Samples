/* $Id: scanner.l,v 1.4 2015-11-13 13:50:39-08 - - $ */

%{
#include "lyutils.h"
%}

%option 8bit
%option debug
%option nodefault
%option nounput
%option noyywrap
%option verbose
%option warn

LETTER          [A-Za-z_]
DIGIT           [0-9]

INTEGER         ({DIGIT}+)
CHAR            ('([^\\'\n]|\\[\\'"0nt])')
STRING          (\"([^\\"\n]|\\[\\'"0nt])*\")
IDENT           ({LETTER}({LETTER}|{DIGIT})*)


%%

"#".*           { lexer::include(); }
[ \t]+          { }
\n              { lexer::newline(); }

"arrow"         { return yylval_token(TOK_ARROW); }
"array"         { return yylval_token(TOK_ARRAY); }
"char"          { return yylval_token(TOK_CHAR); }
"root"          { return yylval_token(TOK_ROOT); }
"void"          { return yylval_token(TOK_VOID); }
"int"           { return yylval_token(TOK_INT); }
"string"        { return yylval_token(TOK_STRING); }
"struct"        { return yylval_token(TOK_STRUCT); }
"if"            { return yylval_token(TOK_IF); }
"else"          { return yylval_token(TOK_ELSE); }
"while"         { return yylval_token(TOK_WHILE); }
"return"        { return yylval_token(TOK_RETURN); }
"null"          { return yylval_token(TOK_NULL); }
"not"           { return yylval_token(TOK_NOT); }
"new"           { return yylval_token(TOK_NEW); }

"=="            { return yylval_token(TOK_EQ); }
"!="            { return yylval_token(TOK_NE); }
"<="            { return yylval_token(TOK_LE); }
">="            { return yylval_token(TOK_GE); }
"<"             { return yylval_token(TOK_LT); }
">"             { return yylval_token(TOK_GT); }

{CHAR}          { return yylval_token(TOK_CHARCON); }
{STRING}        { return yylval_token(TOK_STRINGCON); }
{INTEGER}       { return yylval_token(TOK_INTCON); }
{IDENT}         { return yylval_token(TOK_IDENT); }

"[]"            { return yylval_token('[]'); }
"("             { return yylval_token('('); }
")"             { return yylval_token(')'); }
"["             { return yylval_token('['); }
"]"             { return yylval_token(']'); }
"{"             { return yylval_token('{'); }
"}"             { return yylval_token('}'); }
";"             { return yylval_token(';'); }
","             { return yylval_token(','); }
"."             { return yylval_token('.'); }
"="             { return yylval_token('='); }
"+"             { return yylval_token('+'); }
"-"             { return yylval_token('-'); }
"*"             { return yylval_token('*'); }
"/"             { return yylval_token('/'); }
"%"             { return yylval_token('%'); }
"!"             { return yylval_token('!'); }

.               { lexer_badchar(); }

