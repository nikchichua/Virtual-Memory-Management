
all : test
test: ../my_vm.h
	gcc -m32 test.c -L../ -lmy_vm -o test -lm
	gcc -m32 multi_test.c -L../ -lmy_vm -o mtest -lpthread -lm

clean:
	rm -rf test mtest
