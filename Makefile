TARGET = skeme
TEST_TARGET = skeme_tests
DEPS	= parser.h lexer.h vm.h term_colors.h platform.h error.h sk.h
OBJS	= main.o parser.o lexer.o vm.o builtins.o io.o platform.o error.o sk.o
TEST_OBJS = tests/test_main.o tests/test_vm.o tests/test_gc.o tests/test_lexer.o \
            tests/test_parser.o tests/test_builtins.o tests/test_helpers.o tests/test_error.o \
            parser.o lexer.o vm.o builtins.o io.o platform.o error.o sk.o
CFLAGS	+= -I. -I./hash/testy -g -Wall
LIBS	= -lm -lht -ltesty
LFLAGS  += -L./hash -L./hash/testy

all: hash/libht.a $(TARGET) 

$(OBJS): $(DEPS)

hash/libht.a: hash/hash.c hash/hash.h
	cd hash && $(MAKE)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(LIBS)

hash/testy/libtesty.a: hash/testy/test_main.c hash/testy/test.h
	cd hash/testy && $(MAKE)

$(TEST_TARGET): hash/libht.a hash/testy/libtesty.a $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LFLAGS) $(LIBS)

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