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

#include "string_set.h"

string CPP = "/usr/bin/cpp -nostdinc";
constexpr size_t LINESIZE = 1024;

// Print the meaning of a signal.
static void eprint_signal (const char* kind, int signal) {
   fprintf (stderr, ", %s %d", kind, signal);
   const char* sigstr = strsignal (signal);
   if (sigstr != nullptr) fprintf (stderr, " %s", sigstr);
}

// Print the status returned from a subprocess.
void eprint_status (const char* command, int status) {
   if (status == 0) return; 
   fprintf (stderr, "%s: status 0x%04X", command, status);
   if (WIFEXITED (status)) {
      fprintf (stderr, ", exit %d", WEXITSTATUS (status));
   }
   if (WIFSIGNALED (status)) {
      eprint_signal ("Terminated", WTERMSIG (status));
      #ifdef WCOREDUMP
      if (WCOREDUMP (status)) fprintf (stderr, ", core dumped");
      #endif
   }
   if (WIFSTOPPED (status)) {
      eprint_signal ("Stopped", WSTOPSIG (status));
   }
   if (WIFCONTINUED (status)) {
      fprintf (stderr, ", Continued");
   }
   fprintf (stderr, "\n");
}


// Chomp the last character from a buffer if it is delim.
void chomp (char* string, char delim) {
   size_t len = strlen (string);
   if (len == 0) return;
   char* nlpos = string + len - 1;
   if (*nlpos == delim) *nlpos = '\0';
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
      int sscanf_rc = sscanf (buffer, "# %d \"%[^\"]\"",
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
   int exit_status = EXIT_SUCCESS;
   int flex_debug_flag = 0;
   int debug_flag = 0;
   int c;
   char* filename = argv[1];

    while((c = getopt(argc, argv, "@:D:ly")) != -1)
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


   chomp(filename, 'c');
   chomp(filename, 'o');
   char *inName = strcat(filename, "str");
   FILE* outFile;
   const char *outName = inName;
   outFile = fopen(outName, "w");
   string_set::dump (outFile);
   return EXIT_SUCCESS;
}
