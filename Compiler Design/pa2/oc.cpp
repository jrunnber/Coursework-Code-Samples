// $Id: main.cpp,v 1.2 2016-08-18 15:13:48-07 - - $

#include <string>
using namespace std;

#include <iostream>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "auxlib.h"
#include "lyutils.h"
#include "astree.h"

#include "string_set.h"

string CPP = "/usr/bin/cpp -nostdinc";
const string cpp_name = "/usr/bin/cpp";
string cpp_command;
constexpr size_t LINESIZE = 1024;
//FILE *yyin;

void cpp_popen (const char* filename) {
   cpp_command = cpp_name + " " + filename;
   yyin = popen (cpp_command.c_str(), "r");
   if (yyin == nullptr) {
      syserrprintf (cpp_command.c_str());
      exit (exec::exit_status);
   }else {
      if (yy_flex_debug) {
         fprintf (stderr, "-- popen (%s), fileno(yyin) = %d\n",
                  cpp_command.c_str(), fileno (yyin));
      }
      lexer::newfilename (cpp_command);
   }
}

void cpp_pclose() {
   int pclose_rc = pclose (yyin);
   eprint_status (cpp_command.c_str(), pclose_rc);
   if (pclose_rc != 0) exec::exit_status = EXIT_FAILURE;
}

void scan_opts (int argc, char** argv) {
   opterr = 0;
   yy_flex_debug = 0;
   yydebug = 0;
   lexer::interactive = isatty (fileno (stdin))
                    and isatty (fileno (stdout));
   for(;;) {
      int opt = getopt (argc, argv, "@:ly");
      if (opt == EOF) break;
      switch (opt) {
         case '@': set_debugflags (optarg);   break;
         case 'l': yy_flex_debug = 1;         break;
         case 'y': yydebug = 1;               break;
         default:  errprintf ("bad option (%c)\n", optopt); break;
      }
   }
   if (optind > argc) {
      errprintf ("Usage: %s [-ly] [filename]\n",
                 exec::execname.c_str());
      exit (exec::exit_status);
   }
   const char* filename = optind == argc ? "-" : argv[optind];
   cpp_popen (filename);
}

// Chomp the last character from a buffer if it is delim.
void chomp (char* string, char delim) {
   size_t len = strlen (string);
   if (len == 0) return;
   char* nlpos = string + len - 1;
   if (*nlpos == delim) *nlpos = '\0';
}

void get_toks(char* filename){ 
   chomp(filename, 'c');
   chomp(filename, 'o');
   char *tokName = strcat(filename, "tok");
   FILE* tokFile;
   const char *tokName2 = tokName;
   tokFile = fopen(tokName2, "w");
   
   int token = 1;
   while(token != 0){
      int token = yylex();
      //printf("<<<<<<<<<<<<<<<<<<<< %d >>>>>>>>>>>>>>>>>>>>>>>>>\n", token);
      if(token == 0) break;
      //printf("%s\n", yylval);
      astree::dump(tokFile, yylval);
      fprintf(tokFile, "\n");
   }
   
}

// Run cpp against the lines of the file.
void cpplines (FILE* pipe, const char* filename) {
   int linenr = 1;
   char inputname[LINESIZE];
   strcpy (inputname, filename);
   for (;;) {
      char buffer[LINESIZE];
      char* fgets_rc = fgets (buffer, LINESIZE, pipe);
      if (fgets_rc == nullptr) break;
      chomp (buffer, '\n');
      sscanf (buffer, "# %d \"%[^\"]\"",
                              &linenr, inputname);
      char* savepos = nullptr;
      char* bufptr = buffer;
      for (int tokenct = 1;; ++tokenct) {
         char* token = strtok_r (bufptr, " \t\n", &savepos);
         bufptr = nullptr;
         if (token == nullptr) break;
         string_set::intern(token);
      }
      ++linenr;
   }
}


int main (int argc, char** argv) {
   int exit_status;
   char* filename = argv[1];
  
   scan_opts (argc, argv);

   /* while((c = getopt(argc, argv, "@:D:ly")) != -1)
    {
       switch(c) // The switch runs through cases based off of the input letter and adjusts flags and other variables accordingly
       {
             case '@':
              printf("@"); break;
             case 'l': 
              flex_debug_flag = 1; break;
             case 'y':
              debug_flag = 1; break;
             case 'D':
              CPP = CPP + " -D " + optarg; filename = argv[2]; break;
             default: printf("Wrong args"); exit_status = EXIT_FAILURE; break;
        }
    }
*/
   const char* execname = basename (argv[0]);
  
  

      string command = CPP + " " + filename;
      FILE* pipe = popen (command.c_str(), "r");
      if (pipe == nullptr) {
         exit_status = EXIT_FAILURE;
         fprintf (stderr, "%s: %s: %s\n",
             execname, command.c_str(), strerror (errno));
      }else {
         cpplines(pipe, filename);
         int pclose_rc = pclose (pipe);
         //eprint_status (command.c_str(), pclose_rc);
         if (pclose_rc != 0) exit_status = EXIT_FAILURE;
      }

     cpp_popen(filename);
     char dupName[strlen(filename) + 1];
     strcpy(dupName, filename);
     get_toks(dupName);
     cpp_pclose();
     //printf(yylval);

   chomp(filename, 'c');
   chomp(filename, 'o');
   char *inName = strcat(filename, "str");
   FILE* outFile;
   const char *outName = inName;
   outFile = fopen(outName, "w");
   string_set::dump (outFile);
   return EXIT_SUCCESS;
}
