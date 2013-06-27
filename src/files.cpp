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

#include "scalpel.h"

// Returns TRUE if the directory exists and is empty. 
// If the directory does not exist, an attempt is made to 
// create it.  On error, returns FALSE 
int outputDirectoryOK(char *dir) {

  DIR *temp;
  struct dirent *entry;
  int i;
  mode_t newDirectoryMode;

  if((temp = opendir(dir)) == NULL) {
    // If the directory doesn't exist (ENOENT), we will create it
    if(errno == ENOENT) {
      // The directory mode values come from the chmod(2) man page 
#ifdef  __MINGW32__
      newDirectoryMode = 0;
      if(mkdir(dir)) {
#else
	newDirectoryMode = (S_IRUSR | S_IWUSR | S_IXUSR |
			    S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH);
	if(mkdir(dir, newDirectoryMode)) {
#endif

	  fprintf(stderr,
		  "An error occured while trying to create %s - %s (mode: %hu)\n",
		  dir, strerror(errno), newDirectoryMode);
	  return FALSE;
	}

	// try to open directory
	if((temp = opendir(dir)) == NULL) {
	  fprintf(stderr,
		  "An error occured while trying to open %s - %s\n", dir,
		  strerror(errno));
	  return FALSE;
	}
      }
      else {
	fprintf(stderr, "An error occured while trying to open %s - %s\n",
		dir, strerror(errno));
	return FALSE;
      }
    }

    // verify directory is empty--there should be only two entries,
    // "." and ".." 
    i = 0;
    while ((entry = readdir(temp))) {
      if(i > 1) {
	return FALSE;
      }
      i++;
    }
    closedir(temp);
    return TRUE;
  }

  // open audit file and add initial entries
  int openAuditFile(struct scalpelState *state) {
    time_t now = time(NULL);
    char *timestring = ctime(&now);
    char fn[MAX_STRING_LENGTH];
    char *buf;
    int err = SCALPEL_OK;

    buf = (char *)malloc(NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH);
    checkMemoryAllocation(state, buf, __LINE__, __FILE__, "buf");

    if(!outputDirectoryOK(state->outputdirectory)) {
      err = SCALPEL_ERROR_NONEMPTY_DIRECTORY;
      goto out;
    }

    snprintf(fn, MAX_STRING_LENGTH, "%s/audit.txt", state->outputdirectory);

    FILE *f;

    if(!(state->auditFile = fopen(fn, "w"))) {
      fprintf(stderr, "Couldn't open audit file\n%s -- %s\n", fn,
	      strerror(errno));
      err = SCALPEL_ERROR_FATAL_READ;
      goto out;
    }

    fprintf(state->auditFile,
	    "\nScalpel version %s audit file\n"
	    "Started at %sCommand line:\n%s\n\n"
	    "Output directory: %s\n"
	    "Configuration file: %s\n",
	    SCALPEL_VERSION, timestring, state->invocation,
	    state->outputdirectory, state->conffile);

    // copy config file into audit log

    f = fopen(state->conffile, "r");
    if(f == NULL) {
      fprintf(stderr,
	      "ERROR: Couldn't open configuration file:\n%s -- %s\n",
	      state->conffile, strerror(errno));
      err = SCALPEL_ERROR_FATAL_READ;
      goto out;
    }

    fprintf(state->auditFile, "\n------ BEGIN COPY OF CONFIG FILE USED ------\n");

    while (fgets(buf, NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH, f)) {
      fprintf(state->auditFile, "%s", buf);
    }

    fprintf(state->auditFile, "------ END COPY OF CONFIG FILE USED ------\n\n");
    fclose(f);

  out:
    free(buf);
    return err;
  }

  // write final completion message and close the audit file, if it's
  // open
  int closeAuditFile(FILE * f) {

    time_t now = time(NULL);
    char *timestring = ctime(&now);

    if(f) {
      fprintf(f, "\n\nCompleted at %s", timestring);
      fclose(f);
    }

    return SCALPEL_OK;
  }


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


  // Return the remaining size, in bytes, of an open file stream. On
  // error, return -1.  Handling raw device files is substantially more
  // complicated than image files.  For Linux, an ioctl() is used.  For
  // other operating systems, a "binary search" technique similar to
  // that in e2fsprogs getsize.c is used.
  long long measureOpenFile(FILE * f, struct scalpelState *state) {

    unsigned long long total = 0, original = ftello(f);
    int descriptor = 0;
    struct stat *info;
    unsigned long long numsectors = 0;

    if((fseeko(f, 0, SEEK_END))) {
      if(state->modeVerbose) {
	fprintf(stdout, "fseeko() call failed on image file.\n");
	fprintf(stdout, "Diagnosis: %s\n", strerror(errno));
      }
      return -1;
    }
    total = ftello(f);

    // for block devices (e.g., raw disk devices), calculating size by 
    // seeking the end of the opened stream doesn't work.  For Linux, we use 
    // an ioctl() call.  For others (e.g., OS X), we use binary search.

    // is it a block device?
    descriptor = fileno(f);
    info = (struct stat *)malloc(sizeof(struct stat));
    checkMemoryAllocation(state, info, __LINE__, __FILE__, "info");
    fstat(descriptor, info);
    if(S_ISBLK(info->st_mode)) {

#if defined (__linux)
      if(ioctl(descriptor, BLKGETSIZE, &numsectors) < 0) {
	if(state->modeVerbose) {
	  fprintf(stdout, "Using ioctl() call to measure block device size.\n");
	}
#if defined(__DEBUG)
	perror("BLKGETSIZE failed");
#endif
      }
#else // non-Linux, use binary search

      {
	unsigned long long low, high, mid;

	fprintf(stdout, "Using binary search to measure block device size.\n");
	low = 0;
	for(high = 512; valid_offset(descriptor, high); high *= 2) {
	  low = high;
	}

	while (low < high - 1) {
	  mid = (low + high) / 2;
	  if(valid_offset(descriptor, mid)) {
	    low = mid;
	  }
	  else {
	    high = mid;
	  }
	}
	numsectors = (low + 1) >> 9;
      }
#endif

      // assume device has 512 byte sectors
      total = numsectors * 512;

      free(info);

    }

    // restore file position

    if((fseeko(f, original, SEEK_SET))) {
      if(state->modeVerbose) {
	fprintf(stdout,
		"fseeko() call to restore file position failed on image file.\n");
      }
      return -1;
    }

    return (total - original);
  }
