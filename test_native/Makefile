CC = gcc
FUZZ_CC = clang
CFLAGS = -Wall -Wextra -Werror -g -I. -I../include

TESTS = test_hid_layout test_hid_gamepad test_hid_keyboard
SANITIZERS = -fsanitize=address,undefined -fno-sanitize-recover=all

# How long the fuzzer runs when the tests ask for it rather than a person
FUZZ_SECONDS ?= 60

all: $(TESTS)

test_hid_layout: test_hid_layout.o hid_layout.o
	$(CC) -o $@ $^

test_hid_gamepad: test_hid_gamepad.o hid_gamepad.o hid_layout.o
	$(CC) -o $@ $^

test_hid_keyboard: test_hid_keyboard.o hid_keyboard.o hid_layout.o
	$(CC) -o $@ $^

test_hid_layout.o: test_hid_layout.c test_descriptors.h test_reports.h ../include/hid_layout.h esp_log.h

test_hid_gamepad.o: test_hid_gamepad.c test_descriptors.h test_reports.h ../include/hid_gamepad.h \
                    ../include/hid_layout.h esp_log.h

test_hid_keyboard.o: test_hid_keyboard.c test_descriptors.h ../include/hid_keyboard.h \
                     ../include/hid_layout.h esp_log.h

hid_layout.o: ../src/hid_layout.c ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

hid_gamepad.o: ../src/hid_gamepad.c ../include/hid_gamepad.h ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

hid_keyboard.o: ../src/hid_keyboard.c ../include/hid_keyboard.h ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all test clean
test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

# The same tests, watched for reads past the end of a buffer and for undefined behaviour
SOURCES = ../src/hid_layout.c ../src/hid_gamepad.c ../src/hid_keyboard.c
HEADERS = ../include/hid_layout.h ../include/hid_gamepad.h ../include/hid_keyboard.h test_descriptors.h \
          test_reports.h esp_log.h

test_hid_layout_san: test_hid_layout.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SANITIZERS) -o $@ test_hid_layout.c $(SOURCES)

test_hid_gamepad_san: test_hid_gamepad.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SANITIZERS) -o $@ test_hid_gamepad.c $(SOURCES)

test_hid_keyboard_san: test_hid_keyboard.c $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SANITIZERS) -o $@ test_hid_keyboard.c $(SOURCES)

.PHONY: test-sanitized
test-sanitized: test_hid_layout_san test_hid_gamepad_san test_hid_keyboard_san
	./test_hid_layout_san
	./test_hid_gamepad_san
	./test_hid_keyboard_san

# A report descriptor comes off a device, so the parser is fed random ones as well
fuzz_hid_layout: fuzz_hid_layout.c $(SOURCES) $(HEADERS)
	$(FUZZ_CC) $(CFLAGS) -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
		-o $@ fuzz_hid_layout.c $(SOURCES)

dump_corpus: dump_corpus.c test_descriptors.h
	$(CC) $(CFLAGS) -o $@ $<

corpus: dump_corpus
	mkdir -p corpus
	./dump_corpus

.PHONY: fuzz
fuzz: fuzz_hid_layout corpus
	./fuzz_hid_layout corpus -max_len=1024

.PHONY: fuzz-ci
fuzz-ci: fuzz_hid_layout corpus
	./fuzz_hid_layout corpus -max_len=1024 -max_total_time=$(FUZZ_SECONDS) -print_final_stats=1

clean:
	rm -rf *.o $(TESTS) test_hid_layout_san test_hid_gamepad_san test_hid_keyboard_san \
		fuzz_hid_layout dump_corpus corpus \
		crash-* leak-* timeout-*
