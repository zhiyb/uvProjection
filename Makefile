SRC	= conv.cpp
OBJ	= $(subst .c,,$(SRC:.cpp=))

CXXFLAGS	+= -Wall -O2 -lm
#CXXFLAGS	+= -g -pg

all: $(OBJ)

run: conv
	./$^ in.jpg out.bmp

clean:
	rm -f $(OBJ)
