PREFIX=/usr/local

all:
		gcc -g3 -Wall -Werror -pedantic -DMCMC_TEST=1 -o main main.c ../mcmc.c

asan:
		gcc -g -Wall -Werror -pedantic -DMCMC_TEST=1 -fsanitize=address -o main main.c ../mcmc.c

clean:
		rm -f main main.o

dist: clean

distdir:
