SRC =   ./udt.c \
		./sure.c 

OTHER_SRC = ./copy_file.c \
			./receive_file.c

OBJ = $(SRC:.c=.o) 
OTHER_OBJ = $(OTHER_SRC:.c=.o)

CFLAGS += -Wall -Wextra -DDEBUG_EMULATION=1
CFLAGS += -D_GNU_SOURCE -D_REENTRANT

LDLIBS += -lpthread 

copy: $(OBJ) $(OTHER_OBJ)
	gcc -o copy_file copy_file.o $(OBJ)

receive: $(OBJ) $(OTHER_OBJ)
	gcc -o receive_file receive_file.o $(OBJ)

all: copy receive

clean:
	rm -rf $(OBJ) copy_file receive_file

.PHONY: all clean

