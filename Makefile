TARGET = skeme
TEST_TARGET = skeme_tests
DEPS	= parser.h lexer.h vm.h term_colors.h platform.h error.h sk.h modules.h
OBJS	= main.o parser.o lexer.o vm.o builtins.o io.o platform.o error.o sk.o modules.o mod_math.o
TEST_OBJS = tests/test_main.o tests/test_vm.o tests/test_gc.o tests/test_lexer.o \
            tests/test_parser.o tests/test_builtins.o tests/test_error.o \
            parser.o lexer.o vm.o builtins.o io.o platform.o error.o sk.o modules.o mod_math.o \
            hash/testy/test_main.o
CFLAGS	+= -I. -I./hash/testy -g -Wall
LIBS	= -lm -lht
TEST_LIBS = -lm -lht
LFLAGS  += -L./hash -L./hash/testy

all: hash/libht.a $(TARGET) 

$(OBJS): $(DEPS)

hash/libht.a: hash/hash.c hash/hash.h
	cd hash && $(MAKE)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(LIBS)

hash/testy/test_main.o: hash/testy/test_main.c hash/testy/test.h
	cd hash/testy && $(CC) $(CFLAGS) -c test_main.c -o test_main.o

tests/%.o: tests/%.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_TARGET): hash/libht.a hash/testy/test_main.o $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LFLAGS) $(TEST_LIBS)

.phony: test unit-test all-tests

test: $(TARGET)
	./$(TARGET) -v test.scm

unit-test: $(TEST_TARGET)
	./$(TEST_TARGET)

all-tests: test unit-test

.phony: clean

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) tests/*.o *.o
	cd hash && $(MAKE) clean
	cd hash/testy && $(MAKE) clean