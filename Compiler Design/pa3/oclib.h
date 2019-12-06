// $Id: oclib.h,v 1.16 2018-05-18 16:24:12-07 - - $

#ifndef __OCLIB_H__
#define __OCLIB_H__

#ifdef __OCLIB_C__

void* xcalloc (int nelem, int size);
void putint (int);
void putstr (const char*);
char* getword (void);
char* getln (void);
void endl();

#else

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EOF (-1)
#define char int
#define bool int
#define true 1
#define false 0
#define assert(expr) { \
           if (not (expr)) { \
              __assert_fail (#expr, __FILE__, __LINE__, __FUNC__); \
           } \
        }
void __assert_fail (string expr, string file, int line, string func);
void putchar (char c);
void putint (int i);
void putstr (string s);
int getchar();
string getword();
string getln();
void endl();
void exit (int status);

#endif

#endif

