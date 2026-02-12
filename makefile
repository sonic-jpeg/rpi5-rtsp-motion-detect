# =======================
# Motion Recorder Makefile
# =======================

CC      := gcc
CFLAGS  := -O3 -march=armv8-a+simd -Wall -Wextra
LDFLAGS := -ljansson -lpthread

TARGET  := motion-recorder

SRCS := \
	main.c \
	cameras.c \
	process.c \
	motion.c

OBJS := $(SRCS:.c=.o)

# ---------- default ----------
all: $(TARGET)

# ---------- link ----------
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# ---------- compile ----------
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- clean ----------
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
