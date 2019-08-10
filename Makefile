CC:=gcc
CFLAGS+=-O2 -DUNICODE -D__USE_MINGW_ANSI_STDIO=1 -std=c99 -municode \
       -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DUSE_BACKUPS=0
# -DONLY_CHOCOLATEY=1 -DUSE_BACKUPS=0
LDFLAGS=-ldbghelp -limagehlp

target:=$(shell uname -m)

.PHONY: all
all: clean iat-patcher runtime

clean:
	rm -rf $(target)/*.dll
	rm -rf $(target)/*.exe

iat-patcher: fs.c iat-patcher.c
	$(CC) $(CFLAGS) fs.c iat-patcher.c -o $(target)/iat-patcher.exe $(LDFLAGS)

runtime: $(target)/msvcrt.def fs.c phxcrt.c
	$(CC) -shared phxcrt.c fs.c -o $(target)/muxcrt.dll
	$(CC) -shared $(target)/msvcrt.def -o $(target)/phxcrt.dll