TARGET = skeme
DEPS	= parser.h lexer.h vm.h term_colors.h
OBJS	= main.o parser.o lexer.o vm.o
CFLAGS	+= -I. -g -Wall
LIBS	= -lm -lht
LFLAGS  += -L./hash

all: hash/libht.a $(TARGET) 

$(OBJS): $(DEPS)

hash/libht.a:
	cd hash && $(MAKE)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(LIBS)

test: $(TARGET)
	./$(TARGET) -v test.scm

clean:
	rm $(TARGET) $(OBJS) *.o