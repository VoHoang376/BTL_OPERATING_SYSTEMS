CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

SRCS = $(SRC_DIR)/os.c \
       $(SRC_DIR)/sched.c \
       $(SRC_DIR)/queue.c \
       $(SRC_DIR)/cpu.c \
       $(SRC_DIR)/timer.c \
       $(SRC_DIR)/loader.c \
       $(SRC_DIR)/mm.c \
       $(SRC_DIR)/mm64.c \
       $(SRC_DIR)/mm-vm.c \
       $(SRC_DIR)/mm-memphy.c \
       $(SRC_DIR)/dma.c \
       $(SRC_DIR)/syscall.c \
       $(SRC_DIR)/sys_xxxhandler.c \
       $(SRC_DIR)/libmem.c \
       $(SRC_DIR)/libstd.c \
       $(SRC_DIR)/mem.c \
       $(SRC_DIR)/paging.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

TARGET = os

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe

.PHONY: all clean
