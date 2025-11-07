TARGET = skeme
DEPS	= parser.h lexer.h vm.h term_colors.h
OBJS	= main.o parser.o lexer.o vm.o builtins.o
CFLAGS	+= -I. -g -Wall
LIBS	= -lm -lht
LFLAGS  += -L./hash

all: hash/libht.a $(TARGET) 

$(OBJS): $(DEPS)

hash/libht.a: hash/hash.c hash/hash.h
	cd hash && $(MAKE)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(LIBS)

.phony: test

test: $(TARGET)
	./$(TARGET) -v test.scm

.phony: clean

clean:
	rm $(TARGET) $(OBJS) *.o