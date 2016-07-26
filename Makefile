SRC	= conv.c
OBJ	= $(SRC:.c=)

all: $(OBJ)

run: conv
	./$^

clean:
	rm -f $(OBJ)
