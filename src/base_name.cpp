/*
 * Copyright (C) 2013, Basis Technology Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>

/**
 * Parses a string and returns the final file name in the path. 
 * Assumes that there is not a trailing slash (i.e. a folder).
 * @param name Path to parse
 * @returns Pointer to start of file name or NULL if name ended in slash
 */
char *
base_name (char const *name)
{
    char *base;

    // try unix-style
    base = strrchr(name, '/');
    if (base != NULL) {
        base++;
        if (*(base) == '\0')
            return NULL;
        else
            return base;
    }

    // try Windows-style
    base = strrchr(name, '\\');
    if (base != NULL) {
        if (*(base) == '\0')
            return NULL;
        else
            return base;
    }
    return (char *) name;
}

