#include <stdio.h>
#include "fs.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <dbghelp.h>
#include <stdlib.h>
#include <psapi.h>
#include <wchar.h>

#define USE_BACKUPS 0

static void
show_usage (void)
{
  printf ("(c) Tamar Christina 2018-2019.\n");
  printf (" \n");
  printf ("IAT patcher will patch the C runtime used by the GCC and binutils\n");
  printf ("binaries.  In essense it will redirect calls to posix functions\n");
  printf ("such as fopen to native Windows API calls.  It uses lazy binding\n");
  printf ("semantics to minimize the overhead of the redirection but it allows\n");
  printf ("these programs to work in a modern way.\n");
  printf ("\n");
  printf ("Usage:\n\n");
  printf ("\tiat-patcher.exe <install|uninstall> <exe>\n\n");
}

static bool
rewrite_import_address_table (void* ptr);

int wmain (int argc, wchar_t *argv[], wchar_t *envp[])
{
  if (argc != 3)
    {
      show_usage ();
      return 1;
    }

  bool install = false;

  if (wcsncmp (L"install", argv[1], 7) == 0 && wcslen (argv[1]) == 7)
    install = true;
  else if (wcsncmp (L"uninstall", argv[1], 9) == 0 && wcslen (argv[1]) == 9)
    install = false;
  else
    {
      show_usage ();
      return 1;
    }

  int retcode = 0;

  wchar_t* filepath = __hs_create_device_name (argv[2]);
  if (wcslen (filepath) == 0)
    goto fail;
  int flen = wcslen (filepath)+5;
  wchar_t* bak_filepath = malloc (flen * sizeof (wchar_t));
  if (swprintf (bak_filepath, flen, L"%ls.bak", filepath) < 0)
    goto end;
  wprintf (L"%ls => %ls\n", argv[2], filepath);

  if (!install)
    goto restore;

#if USE_BACKUPS
  wprintf (L"creating backup...\n");
  if (!CopyFileW (filepath, bak_filepath, false))
    goto end;
#endif

  HANDLE fHwnd
    = CreateFile (filepath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
                  FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_WRITE_THROUGH, NULL);

  if (fHwnd == INVALID_HANDLE_VALUE)
    {
      wprintf (L"unable to open binary %ls.  aborting.\n", filepath);
      goto fail;
    }

  HANDLE mHwnd
    = CreateFileMapping (fHwnd, NULL, PAGE_READWRITE, 0, 0, NULL);

  if (mHwnd == NULL)
    {
      wprintf (L"cannot create mapping object for %ls.  aborting.\n", filepath);
      CloseHandle (fHwnd);
      goto fail;
    }

  void* ptr = MapViewOfFile (mHwnd, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0);
  if (ptr == NULL)
    {
      wprintf (L"cannot create view mapping for %ls.  aborting.\n", filepath);
      CloseHandle (fHwnd);
      CloseHandle (mHwnd);
      goto fail;
    }

  if (!rewrite_import_address_table (ptr))
    {
      printf ("oops, something went wrong. bailing out...\n");
      UnmapViewOfFile (ptr);
      CloseHandle (fHwnd);
      CloseHandle (mHwnd);
      goto fail;
    }

  printf ("import description table rewritten. Good to go!\n");

fail:
  retcode = 1;
  DWORD code = GetLastError ();
  if (code != ERROR_SUCCESS)
    {
      wchar_t buf[256];
      FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL, code, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
                     buf, sizeof(buf), NULL);
      wprintf(L"Error: %ls\n", buf);
    }
restore:
#if USE_BACKUPS
  printf ("restoring backup..\n");
  if (!MoveFileEx (bak_filepath, filepath, MOVEFILE_REPLACE_EXISTING))
    wprintf (L"could not restore backup.  Keeping file %ls.\n", bak_filepath);
  else
    DeleteFileW (bak_filepath);
#endif
end:
  free (filepath);
  free (bak_filepath);
  return retcode;
}

static bool
rewrite_import_address_table (void* ptr)
{
  PIMAGE_NT_HEADERS ntHeader = ImageNtHeader (ptr);
  IMAGE_FILE_HEADER imgHeader = ntHeader->FileHeader;
  if (imgHeader.Machine != IMAGE_FILE_MACHINE_I386
      && imgHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
    return false;

  IMAGE_OPTIONAL_HEADER optHeader = ntHeader->OptionalHeader;
  if (optHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
      && optHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    return false;

  IMAGE_DATA_DIRECTORY importDir
    = optHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

  if (importDir.Size == 0)
    {
      printf ("no import headers.  Done.");
      return true;
    }

  PIMAGE_IMPORT_DESCRIPTOR importDesc
    = (PIMAGE_IMPORT_DESCRIPTOR) ImageRvaToVa (ntHeader, ptr,
                                               importDir.VirtualAddress, NULL);

  while (true)
    {
      const char* name
        = (char*)ImageRvaToVa(ntHeader, ptr, importDesc->Name, NULL);
      if (name == NULL)
        {
          printf ("did not find C runtime, nothing to do.\n");
          break;
        }

      if (strncmp ("msvcrt.dll", name, 10) == 0)
        {
          printf ("found C runtime entry (%s). Rewriting..\n", name);
          *name = "phxcrt.dll";
          printf ("installed new C runtime entry (%s=>phxcrt.dll)..\n", name);
          return true;
        }
      importDesc++;
    }

  return true;
}
