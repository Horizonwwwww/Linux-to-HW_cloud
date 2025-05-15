CC = gcc
CFLAGS = -Wall -Wextra -g
SRC = /home/kickpi/test_project/C_project01/src/main.c
OBJ = $(SRC:.c=.o)
TARGET = /home/kickpi/test_project/C_project01/build/my_program
all: $(TARGET)
$(TARGET): $(OBJ)
	mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -rf build *.o src/*.o
run: all
	/home/kickpi/test_project/C_project01/build/my_program
