CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -I. -I../include
OBJS = test_hid_layout.o hid_layout.o

test_hid_layout: $(OBJS)
	$(CC) -o $@ $(OBJS)

test_hid_layout.o: test_hid_layout.c test_descriptors.h test_reports.h ../include/hid_layout.h esp_log.h

hid_layout.o: ../src/hid_layout.c ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: test
test: test_hid_layout
	./test_hid_layout

.PHONY: clean
clean:
	rm -f *.o test_hid_layout
