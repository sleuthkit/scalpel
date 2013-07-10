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

#include "input_reader.h"
#include "scalpel.h"
#include "common.h"


/*********  generic IO API implementation *************/

//TODOs:
//unify return codes, use SCALPEL_OK more, etc
//cleanup
//


static void printVerbose(const char * const format, ...) {
	if (inputReaderVerbose) {
		va_list args;
		va_start(args, format);
		fprintf(stderr, format, args);
	    va_end(args);
	}
	return;
}


int scalpelInputRead(ScalpelInputReader * const reader, void * buf, size_t size,
		size_t count) {
	printVerbose("scalpelInputRead()\n");
	return reader->read(reader, buf, size, count);
}

int scalpelInputSeeko(ScalpelInputReader * const reader, long long offset,
		scalpel_SeekRel whence) {
	printVerbose("scalpelInputSeeko()\n");
	return reader->seeko(reader, offset, whence);
}

unsigned long long scalpelInputTello(ScalpelInputReader * const reader) {
	printVerbose("scalpelInputTello()\n");
	return reader->tello(reader);
}

int scalpelInputOpen(ScalpelInputReader * const reader) {
	printVerbose("scalpelInputOpen()\n");
	return reader->open(reader);
}

void scalpelInputClose(ScalpelInputReader * const reader) {
	printVerbose("scalpelInputClose()\n");
	if (!reader->isOpen) {
		fprintf(stderr, "scalpelInputClose() -- ERROR -- reader for %s is not open, can't close\n", reader->id);
		return;
	}
	reader->close(reader);
	reader->isOpen = 0;
	return;
}

long long scalpelInputGetSize(ScalpelInputReader * const reader) {
	printVerbose("scalpelInputGetSize()\n");
	if (!reader->open) {
		fprintf(stderr, "scalpelInputGetSize() - ERROR trying to get size on closed reader\n");
		return -1;
	}
	return reader->getSize(reader);
}

int scalpelInputGetError(ScalpelInputReader * const reader) {
	printVerbose("scalpelInputGetError()\n");
	return reader->getError(reader);
}

const char * scalpelInputGetId (ScalpelInputReader * const reader) {
	printVerbose("scalpelInputGetId()\n");
	return reader->id;
}

const char scalpelInputIsOpen (ScalpelInputReader * const reader) {
	printVerbose("scalpelInputIsOpen()\n");
	return reader->isOpen;
}

/********** FILE IO implementation ***********/


static inline FileDataSource* castFileDataSource(
		ScalpelInputReader * reader) {
	void * dataSource = reader->dataSource;
	if (!dataSource) {
		//error
		return NULL ;
	}
	FileDataSource* fileSource = ((FileDataSource*) dataSource);
	return fileSource;
}



static int fileDataSourceRead(ScalpelInputReader * const reader, void * buf,
		size_t size, size_t count) {
	const FileDataSource* fileSource = castFileDataSource(reader);
	return fread(buf, size, count, fileSource->fileHandle);
}

static unsigned long long fileDataSourceTellO(ScalpelInputReader * const reader) {
	const FileDataSource* fileSource = castFileDataSource(reader);
	return ftello(fileSource->fileHandle);
}

static void fileDataSourceClose(ScalpelInputReader * const reader) {
	FileDataSource* fileSource = castFileDataSource(reader);
	fclose((FILE*) fileSource->fileHandle);
	fileSource->fileHandle = NULL;
	return;
}

static int fileDataSourceGetError(ScalpelInputReader * const reader) {
	//return errno;
	//TODO we should be really unifying the error code somehow
	const FileDataSource* fileSource = castFileDataSource(reader);
	return ferror((FILE*) fileSource->fileHandle);
}


//valid_offset() copied from files.c
#if ! defined(__linux)

  // helper function for measureOpenFile(), based on e2fsprogs utility
  // function valid_offset()

  static int valid_offset(int fd, off64_t offset) {
    char ch;
    if(lseek(fd, offset, SEEK_SET) < 0) {
      return 0;
    }
    if(read(fd, &ch, 1) < 1) {
      return 0;
    }
    return 1;
  }

#endif

//slightly modified version of measureOpenFile() from files.c, without usage of state struct
static long long getSizeOpenFile(FILE * f) {

	unsigned long long total = 0, original = ftello(f);
	int descriptor = 0;
	struct stat *info;
	unsigned long long numsectors = 0;

	if ((fseeko(f, 0, SEEK_END))) {
		if(inputReaderVerbose) {
			fprintf(stderr, "fseeko() call failed on input file.\n");
			fprintf(stderr, "Diagnosis: %s\n", strerror(errno));
		}

		return -1;
	}
	total = ftello(f);

	// for block devices (e.g., raw disk devices), calculating size by
	// seeking the end of the opened stream doesn't work.  For Linux, we use
	// an ioctl() call.  For others (e.g., OS X), we use binary search.

	// is it a block device?
	descriptor = fileno(f);
	info = (struct stat *) malloc(sizeof(struct stat));
	if (!info) {
		fprintf(stderr, "getSizeOpenFile() - ERROR can't allocate stat info\n");
		return -1;
	}
#ifdef OFF
	checkMemoryAllocation(state, info, __LINE__, __FILE__, "info");
#endif
	fstat(descriptor, info);
	if (S_ISBLK(info->st_mode)) {

#if defined (__linux)
		if(ioctl(descriptor, BLKGETSIZE, &numsectors) < 0) {
			if(inputReaderVerbose) {
				fprintf(stderr, "Using ioctl() call to measure block device size.\n");
			}
#if defined(__DEBUG)
			perror("BLKGETSIZE failed");
#endif
		}
#else // non-Linux, use binary search
		{
			unsigned long long low, high, mid;

			fprintf(stderr, "Using binary search to measure block device size.\n");
			low = 0;
			for (high = 512; valid_offset(descriptor, high); high *= 2) {
				low = high;
			}

			while (low < high - 1) {
				mid = (low + high) / 2;
				if (valid_offset(descriptor, mid)) {
					low = mid;
				} else {
					high = mid;
				}
			}
			numsectors = (low + 1) >> 9;
		}
#endif

		// assume device has 512 byte sectors
		total = numsectors * 512;

	}

	free(info);

	// restore file position

	if ((fseeko(f, original, SEEK_SET))) {
#ifdef OFF
		if(inputReaderVerbose) {
			fprintf(stderr,
					"fseeko() call to restore file position failed on image file.\n");
		}
#endif
		return -1;
	}

	return (total - original);
}

//return size, or -1 on error
static long long fileDataSourceGetSize(ScalpelInputReader * const reader) {
	if (!reader->isOpen) {
		fprintf(stderr, "Error: Input Reader for file %s not open, can't get size\n", reader->id);
		return -1;
	}

	const FileDataSource* fileSource = castFileDataSource(reader);

	FILE * fh = fileSource->fileHandle;
	if (!fh) {
		fprintf(stderr, "fileDataSourceGetSize() - ERROR - not file handle set, can't get size\n");
		return -1;
	}

	return getSizeOpenFile(fh);

}

static int fileDataSourceOpen(ScalpelInputReader * const reader) {
	if (reader->isOpen) {
		//OK, reuse it
		fprintf(stderr, "fileDataSourceOpen -- WARNING -- Input Reader for file %s already open, will reuse it\n", reader->id);
		return 0;
	}

	FileDataSource* fileSource = (FileDataSource*) castFileDataSource(reader);

	fileSource->fileHandle = fopen(reader->id, "rb");
	if (!fileSource->fileHandle) {
		fprintf(stderr, "fileDataSourceOpen -- ERROR -- Can't open Input Reader for %s\n", reader->id);
		return errno;
	}

#ifdef OFF
	#ifdef _WIN32
	// set binary mode for Win32
	setmode(fileno(fileSource->fileHandle), O_BINARY);
	#endif
	#ifdef __linux
	fcntl(fileno(fileSource->fileHandle), F_SETFL, O_LARGEFILE);
	#endif
#endif

	reader->isOpen = 1;

	return 0;
}

static int fileDataSourceSeekO(ScalpelInputReader * const reader, long long offset,
		scalpel_SeekRel whence) {
	const FileDataSource* fileSource = castFileDataSource(reader);
	int fWhence = 0;
	switch (whence) {
	case SCALPEL_SEEK_SET:
		fWhence = SEEK_SET;
		break;
	case SCALPEL_SEEK_CUR:
		fWhence = SEEK_CUR;
		break;
	case SCALPEL_SEEK_END:
		fWhence = SEEK_END;
		break;
	default:

		break;
	}

	return fseeko(fileSource->fileHandle, offset, fWhence);

}

ScalpelInputReader * scalpel_createInputReaderFile(const char * const filePath) {
	printVerbose("createInputReaderFile()\n");

	ScalpelInputReader * fileReader = (ScalpelInputReader *) malloc(
			sizeof(ScalpelInputReader));
	if (!fileReader) {
		fprintf(stderr, "createInputReaderFile() - malloc() ERROR fileReader not created\n ");
		return NULL ;
	}

	//setup data

	size_t pathLen = strlen(filePath);
	fileReader->id = (char*) malloc((pathLen + 1) * sizeof(char));
	strncpy(fileReader->id, filePath, pathLen);
	fileReader->id[pathLen] = '\0';

	fileReader->dataSource = (void*) malloc(sizeof (FileDataSource) );
	if (!fileReader->dataSource) {
		fprintf(stderr, "createInputReaderFile() - malloc() ERROR dataSource not created\n ");
		return NULL ;
	}

	FileDataSource * fileSource = (FileDataSource *) fileReader->dataSource;
	fileReader->isOpen = 0;
	fileSource->fileHandle = NULL;

	//set up functions
	fileReader->open = fileDataSourceOpen;
	fileReader->close = fileDataSourceClose;
	fileReader->getError = fileDataSourceGetError;
	fileReader->getSize = fileDataSourceGetSize;
	fileReader->seeko = fileDataSourceSeekO;
	fileReader->tello = fileDataSourceTellO;
	fileReader->read = fileDataSourceRead;

	printVerbose("createInputReaderFile -- input reader created\n");

	return fileReader;
}

void scalpel_freeInputReaderFile(ScalpelInputReader * fileReader) {
	printVerbose("freeInputReaderFile()\n");
	if (!fileReader) {
		return;
	}

	if (!fileReader->dataSource) {
		fprintf(stderr, "freeInputReaderFile() - ERROR dataSource not set, can't free\n ");
		return; //ERROR
	}

	FileDataSource * fileSource = (FileDataSource *) fileReader->dataSource;
	FILE * fileHandle = fileSource->fileHandle;

	if (fileReader->isOpen) {
		if (fileHandle) {
			fclose(fileHandle);
			fileReader->isOpen = 0;
			fileHandle = NULL;
		} else {
			//ERROR
			fprintf(stderr, "freeInputReaderFile() - WARNING reader open, but handle not set\n");
		}

	}

	if (fileReader->id) {
		free(fileReader->id);
		fileReader->id = NULL;
	}

	free(fileReader->dataSource);
	fileReader->dataSource = NULL;
	free(fileReader);
}

