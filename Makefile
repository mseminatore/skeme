TARGET = skeme
DEPS	= parser.h lexer.h vm.h
OBJS	= main.o parser.o lexer.o vm.o
CFLAGS	= -I. -g -Wall
LIBS	= -lm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm $(TARGET) $(OBJS) *.o