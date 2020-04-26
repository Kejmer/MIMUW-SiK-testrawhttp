SRCPTH = kh406160
SRC = $(notdir $(wildcard $(SRCPTH)/*.c))
OBJ = $(addprefix $(SRCPTH)/,$(SRC:.cpp=.o))
CC = gcc
CFLAGS = -Wall
TARGET = testhttp_raw

all: $(TARGET)

$(SRCPTH)/%.o: $(SRCPTH)/%.c
	gcc -c $< -o $(SRCPTH)/$(basename $(notdir $<)).o

testhttp_raw:
	gcc $(OBJ) -o $(TARGET)

clean:
	rm -f *.o *~ $(TARGET)
