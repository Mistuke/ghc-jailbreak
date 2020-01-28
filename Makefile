CC:=gcc
# Disable the last line here when building patcher for inclusion into GHC tree
# as we don't want backups in that case and we don't want to restrict it to
# chocolatey.
CFLAGS+=-O2 -DUNICODE -D__USE_MINGW_ANSI_STDIO=1 -std=c99 -municode \
       -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DUSE_BACKUPS=0 \
       -DONLY_CHOCOLATEY=0
LDFLAGS=-ldbghelp -limagehlp

target:=$(shell uname -m)

.PHONY: all
all: clean iat-patcher runtime

clean:
	rm -rf $(target)/*.dll
	rm -rf $(target)/*.exe

iat-patcher: fs.c iat-patcher.c
	$(CC) $(CFLAGS) fs.c iat-patcher.c -o $(target)/iat-patcher.exe $(LDFLAGS)

runtime: $(target)/msvcrt.def fs.c
	$(CC) -shared fs.c -o $(target)/muxcrt.dll
	$(CC) -shared $(target)/msvcrt.def -o $(target)/phxcrt.dll