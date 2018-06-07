CROSS_COMPILE	?=
ARCH		?= x86
KERNEL_DIR	?= /usr/src/linux

CC		:= $(CROSS_COMPILE)gcc
KERNEL_INCLUDE	:= -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/$(ARCH)/include
CFLAGS		:= -W -Wall -g $(KERNEL_INCLUDE)
LDFLAGS		:= -g

OBJS		:= \
			configfs.o \
			events.o \
			main.o \
			stream.o \
			uvc.o \
			v4l2.o

all: uvc-gadget

uvc-gadget: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o
	rm -f uvc-gadget

