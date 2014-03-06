/*
Copyright (C) 2013, Basis Technology Corp.
Copyright (C) 2007-2011, Golden G. Richard III and Vico Marziale.
Copyright (C) 2005-2007, Golden G. Richard III.
*
Written by Golden G. Richard III and Vico Marziale.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
*
http://www.apache.org/licenses/LICENSE-2.0
*
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
Thanks to Kris Kendall, Jesse Kornblum, et al for their work
on Foremost. Foremost 0.69 was used as the starting point for
Scalpel, in 2005.
*/

#ifndef __INPUT_READER_H__
#define __INPUT_READER_H__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>

#if defined ( _WIN32)
#define ftello   ftello64
#define fseeko   fseeko64
#endif



extern int inputReaderVerbose;

//seek relative offset
typedef enum {
    SCALPEL_SEEK_SET,
    SCALPEL_SEEK_CUR,
    SCALPEL_SEEK_END
} scalpel_SeekRel;

//abstraction for the input source to scalpel
typedef struct _ScalpelInputReader {

    //unsigned char isDefault; //if using a default FILE implementation
    void * dataSource;  //pointer to the underlying content object, such as FileDataSource for FILE *
    unsigned char isOpen;
    char * id;         //id of the underlying data source, such as file path

    //abstract methods, pointer to these need be provided by concrete impl.
    int (* open)(struct _ScalpelInputReader * const reader);
    void (* close)(struct _ScalpelInputReader * const reader);
    int (* getError) (struct _ScalpelInputReader * const reader);
    long long (* getSize) (struct _ScalpelInputReader * const reader);
    int (* seeko)(struct _ScalpelInputReader * const reader, long long offset, scalpel_SeekRel whence);
    unsigned long long (* tello)(struct _ScalpelInputReader * const reader);
    int (* read)(struct _ScalpelInputReader * const reader, void * buf, size_t size, size_t count);
} ScalpelInputReader;

/********** generic IO methods *********/

//abstract methods
int scalpelInputRead (ScalpelInputReader * const reader, void * buf, size_t size, size_t count);
int scalpelInputSeeko (ScalpelInputReader * const reader, long long offset, scalpel_SeekRel whence);
unsigned long long scalpelInputTello (ScalpelInputReader * const reader);
int scalpelInputOpen (ScalpelInputReader * const reader);
void scalpelInputClose (ScalpelInputReader * const reader);
long long scalpelInputGetSize (ScalpelInputReader *const reader);
//0 on non-error. @@@ we should be unifying / abstracting out the error codes.
int scalpelInputGetError (ScalpelInputReader * const reader);

//non-abstract methods
const char* scalpelInputGetId (ScalpelInputReader * const reader);
const char scalpelInputIsOpen(ScalpelInputReader * const reader);


/********************* FILE implementation of ScalpelInputReader **********************/

typedef struct FileDataSource {
    FILE * fileHandle;
} FileDataSource;


//creates a ScalpelInputReader with FILE implementation
extern ScalpelInputReader * scalpel_createInputReaderFile(const char * const filePath);
//frees a ScalpelInputReader with FILE implementation
extern void scalpel_freeInputReaderFile(ScalpelInputReader * const fileReader);

#endif
