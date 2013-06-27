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


#ifndef TYPES_H_
#define TYPES_H_

//printf conversion macros
#ifndef PRIu64

#ifdef _WIN32
#define PRIu64 "I64u"
#else
#define PRIu64 "llu"
#endif //_WIN32

#endif //PRIu64

#ifndef PRI64
#ifdef _WIN32
#define PRI64 "I64d"
#else
#define PRI64 "lld"
#endif //_WIN32
#endif //PRI64

#endif /* TYPES_H_ */
