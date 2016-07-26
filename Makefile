SRC	= conv.cpp
OBJ	= $(subst .c,,$(SRC:.cpp=))

CXXFLAGS	+= -Wall -O2 -lm

all: $(OBJ)

run: conv
	./$^ in.jpg out.png

clean:
	rm -f $(OBJ)
