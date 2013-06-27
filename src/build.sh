/usr/local/cuda/bin/nvcc -arch sm_12  -Xcompiler -O3 --compiler-options -fno-strict-aliasing -I. -I/usr/local/cuda/include -Itre-0.7.5/lib -DUNIX -o dig.cu.o -c dig.cu;
g++ -O3 -fPIC -o scalpel-gpu scalpel.c base_name.c files.c helpers.c prioque.c dig.c syncqueue.c scalpel.h prioque.h syncqueue.h dig.cu.o -L/usr/local/cuda/lib -lcudart -lpthread -lm -ltre;
