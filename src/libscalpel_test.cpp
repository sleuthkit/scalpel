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

// Test driver for scalpel library API

int main(int argc, char ** argv) {
	if (argc < 4) {
		printf("usage: libscalpel_test confFilePath outDirPath inputFilePath ...\n");
		exit(-1);
	}

	printf("Testing scalpel lib with args %s %s %s\n", argv[1], argv[2], argv[3]);
    
    scalpelState * pScalpelState = NULL;
    scalpelState options;
	options.generateHeaderFooterDatabase = FALSE;
	options.handleEmbedded = FALSE;
	options.organizeSubdirectories = TRUE;
	options.previewMode = FALSE;
	options.carveWithMissingFooters = FALSE;
	options.noSearchOverlap = FALSE;

    if (libscalpel_initialize(&pScalpelState, argv[1], argv[2], options) != SCALPEL_OK)
    {
        printf("libscalpel initialization failed.\n");
        exit(1);
    }
    
    for (int i = 3; i < argc; ++i)
    {
        ScalpelInputReader * inputReader = scalpel_createInputReaderFile(argv[i]);
        if (!inputReader) {
            printf("Error creating inputReader for input file %s\n", argv[3]);
            return 1;
        }

        try {
            int scalpErr = libscalpel_carve_input(pScalpelState, inputReader);
            
            printf("Done, libscalp result: %d\n", scalpErr);
        }
        catch (std::runtime_error & e) {
            fprintf(stderr, "Error during carving: %s\n", e.what());
        }
        catch (...) {
            fprintf(stderr, "Unexpected error during carving\n");
        }

        scalpel_freeInputReaderFile(inputReader);
    }

    libscalpel_finalize(&pScalpelState);
    
	return 0;
}


