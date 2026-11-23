# myshell Makefile
CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -g -O2
SRCDIR  = src
OBJDIR  = obj
SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
TARGET  = myshell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(wildcard $(SRCDIR)/*.h) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

test: $(TARGET)
	bash tests/run_tests.sh
	python3 tests/signal_test.py

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all test clean
