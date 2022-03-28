SRC =   ./udt.c \
		./sure.c 

OTHER_SRC = ./copy_file.c \
			./receive_file.c \
			./test_init_sure.c \
			./test_init_sure_reveiver.c

OBJ = $(SRC:.c=.o) 
OTHER_OBJ = $(OTHER_SRC:.c=.o)

CFLAGS += -g -Wall -Wextra 
# CFLAGS += -DDEBUG_EMULATION=1 -D_GNU_SOURCE -D_REENTRANT

LDLIBS += -lpthread 

copy: $(OBJ) $(OTHER_OBJ)
	gcc $(CFLAGS)  $(LDLIBS)  -o copy_file copy_file.o $(OBJ) $(LDLIBS) 

receive: $(OBJ) $(OTHER_OBJ)
	gcc $(LDLIBS) -o receive_file receive_file.o $(OBJ) $(LDLIBS)

test_init_sure: $(OBJ) $(OTHER_OBJ)
	gcc $(LDLIBS) -o test_init_sure test_init_sure.o $(OBJ) $(LDLIBS)

test_init_sure_reveiver: $(OBJ) $(OTHER_OBJ)
	gcc $(LDLIBS) -o test_init_sure_reveiver test_init_sure_reveiver.o $(OBJ) $(LDLIBS)



all: copy receive

clean:
	rm -rf $(OBJ) copy_file receive_file

.PHONY: all clean

