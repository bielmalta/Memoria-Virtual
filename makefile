vm: vm.o
	gcc vm.o -o vm -pthread

vm.o: vm.c
	gcc -Wall -Wextra -std=c11 -pthread -c vm.c -o vm.o

clean:
	rm -f vm.o vm