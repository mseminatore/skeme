TARGET = skeme
DEPS	= parser.h lexer.h vm.h
OBJS	= main.o parser.o lexer.o vm.o
CFLAGS	= -I. -g -Wall
LIBS	= -lm

all: $(TARGET)

.c.o: $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
	
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

test: $(TARGET)
	./$(TARGET) -v test.sk

clean:
	rm $(TARGET) $(OBJS) *.o