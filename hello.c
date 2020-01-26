#include <stdio.h>
#include <io.h>
#include <windows.h>
#include "fs.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

int main ()
{
  //volatile int f = sopen ("E:\\ghc-dev\\msys64\\tmp\\ccSGClgk.s", 769, 0, 0);
  //volatile int g = FS(sopen) ("E:\\ghc-dev\\msys64\\tmp\\ccSGClgk.s", 769, 0, 0);

/*
  volatile int f = sopen ("E:\\ghc-dev\\msys64\\tmp\\ccSGClgk.s", 33154, 64, 438);
  volatile DWORD x1 = GetLastError ();
  errno_t err;
  _get_errno( &err );
  volatile int g = FS(sopen) ("E:\\ghc-dev\\msys64\\tmp\\ccSGClgk.s", 33154, 64, 438);
  volatile DWORD x2 = GetLastError ();

  printf ("Hello Worldsa!%d (%d) %d\n", f, err, g);
*/
  //rename ("R:\\foo.c", "R:\\bar.c");
  //remove ("R:\\bar.c");

  volatile struct _stati64 old, new;
  _stati64 ( "r://ghc-8.6.5-old//mingw//bin/../lib/gcc/i686-w64-mingw32/7.2.0/lto-wrapper.exe", &old );
  FS(_stati64) ( "r://ghc-8.6.5-old//mingw//bin/../lib/gcc/i686-w64-mingw32/7.2.0/lto-wrapper.exe", &new );

  DebugBreak ();
  return 0;
}