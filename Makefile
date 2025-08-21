CC = gcc
# CC = cosmocc
# OPT = -O2 -fomit-frame-pointer -DNDEBUG=1
OPT = -g
MATH = -frounding-math
WARN = -Wno-error=long-long -Wno-long-long -Wno-error=format-overflow -Wno-format-overflow
# OS = -DWIN32
CFLAGS = -ansi -pedantic -Wall -Wextra -Werror $(WARN) $(OS) $(OPT) $(MATH)
LDFLAGS = -g
SRCS = curterm.c pdp5.c
OBJS = $(SRCS:.c=.o)

TARGET = pdp5.exe

all: $(TARGET)

$(TARGET): version.h $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lm

version.h: $(SRCS)
	echo "#define VERSION \"`date +\"%Y-%m-%d %H:%M:%S\"`\"" >version.h

fpexptab: fpexptab.o
	$(CC) $(LDFLAGS) -o $@ fpexptab.o -lm

clean:
	$(RM) $(TARGET) version.h
	$(RM) $(OBJS)

# vim:set ts=4 sw=4 noet:
