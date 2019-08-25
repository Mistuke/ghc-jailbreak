/* -----------------------------------------------------------------------------
 *
 * (c) Tamar Christina 2018-2019
 *
 * Windows I/O routines for file opening.
 *
 * NOTE: Only modify this file in utils/fs/ and rerun configure. Do not edit
 *       this file in any other directory as it will be overwritten.
 *
 * ---------------------------------------------------------------------------*/
#include "fs.h"
#include <stdio.h>

#if defined(_WIN32)

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <share.h>
#include <errno.h>

/* This function converts Windows paths between namespaces. More specifically
   It converts an explorer style path into a NT or Win32 namespace.
   This has several caveats but they are caviats that are native to Windows and
   not POSIX. See
   https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx.
   Anything else such as raw device paths we leave untouched.  The main benefit
   of doing any of this is that we can break the MAX_PATH restriction and also
   access raw handles that we couldn't before.  */
wchar_t* FS(create_device_name) (const wchar_t* filename) {
  const wchar_t* win32_dev_namespace  = L"\\\\.\\";
  const wchar_t* win32_file_namespace = L"\\\\?\\";
  const wchar_t* nt_device_namespace  = L"\\Device\\";
  const wchar_t* unc_prefix           = L"UNC\\";
  const wchar_t* network_share        = L"\\\\";

  wchar_t* result = _wcsdup (filename);
  wchar_t ns[10] = {0};

  /* If the file is already in a native namespace don't change it.  */
  if (   wcsncmp (win32_dev_namespace , filename, 4) == 0
      || wcsncmp (win32_file_namespace, filename, 4) == 0
      || wcsncmp (nt_device_namespace , filename, 8) == 0)
    return result;

  /* Since we're using the lower level APIs we must normalize slashes now.  The
     Win32 API layer will no longer convert '/' into '\\' for us.  */
  for (size_t i = 0; i < wcslen (result); i++)
    {
      if (result[i] == L'/')
        result[i] = L'\\';
    }

  /* We need to expand dos short paths as well.  */
  DWORD nResult = GetLongPathNameW (result, NULL, 0) + 1;
  wchar_t* temp = NULL;
  if (nResult > 1)
    {
      temp = _wcsdup (result);
      result = malloc (nResult * sizeof (wchar_t));
      if (GetLongPathNameW (temp, result, nResult) == 0)
        {
          result = memcpy (result, temp, wcslen (temp));
          goto cleanup;
        }
      free (temp);
    }

  /* Now resolve any . and .. in the path or subsequent API calls may fail since
     Win32 will no longer resolve them.  */
  nResult = GetFullPathNameW (result, 0, NULL, NULL) + 1;
  temp = _wcsdup (result);
  result = malloc (nResult * sizeof (wchar_t));
  if (GetFullPathNameW (temp, nResult, result, NULL) == 0)
    {
      result = memcpy (result, temp, wcslen (temp));
      goto cleanup;
    }

  free (temp);

  if (wcsncmp (network_share, result, 2) == 0)
    {
      if (swprintf (ns, 10, L"%ls%ls", win32_file_namespace, unc_prefix) <= 0)
        {
          goto cleanup;
        }
    }
  else if (swprintf (ns, 10, L"%ls", win32_file_namespace) <= 0)
    {
      goto cleanup;
    }

  /* Create new string.  */
  int bLen = wcslen (result) + wcslen (ns) + 1;
  temp = _wcsdup (result);
  result = malloc (bLen * sizeof (wchar_t));
  if (swprintf (result, bLen, L"%ls%ls", ns, temp) <= 0)
    {
      goto cleanup;
    }

  free (temp);

  return result;

cleanup:
  free (temp);
  free (result);
  return NULL;
}

static int setErrNoFromWin32Error (void);
static
int setErrNoFromWin32Error () {
  switch (GetLastError()) {
    case ERROR_SUCCESS:
      errno = 0;
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_FILE_READ_ONLY:
      errno = EACCES;
      break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      errno = ENOENT;
      break;
    case ERROR_FILE_EXISTS:
      errno = EEXIST;
      break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      return ENOMEM;
      break;
    case ERROR_INVALID_HANDLE:
      return EBADF;
      break;
    case ERROR_INVALID_FUNCTION:
      return EFAULT;
      break;
    default:
      errno = EINVAL;
      break;
  }
  return -1;
}


#define HAS_FLAG(a,b) (((a) & (b)) == (b))

int FS(swopen) (const wchar_t* filename, int oflag, int shflag, int pmode)
{
  /* Construct access mode.  */
  DWORD dwDesiredAccess = 0;
  if (HAS_FLAG (oflag, _O_RDONLY))
    dwDesiredAccess |= GENERIC_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES;
  if (HAS_FLAG (oflag, _O_RDWR))
    dwDesiredAccess |= GENERIC_WRITE | GENERIC_READ | FILE_READ_DATA |
                       FILE_WRITE_DATA | FILE_READ_ATTRIBUTES |
                       FILE_WRITE_ATTRIBUTES;
  if (HAS_FLAG (oflag,  _O_WRONLY))
    dwDesiredAccess|= GENERIC_WRITE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

  /* Construct shared mode.  */
  DWORD dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
  if (HAS_FLAG (shflag, _SH_DENYRW))
    dwShareMode &= ~(FILE_SHARE_READ | FILE_SHARE_WRITE);
  if (HAS_FLAG (shflag, _SH_DENYWR))
    dwShareMode &= ~FILE_SHARE_WRITE;
  if (HAS_FLAG (shflag, _SH_DENYRD))
    dwShareMode &= ~FILE_SHARE_READ;
  if (HAS_FLAG (pmode, _S_IWRITE))
    dwShareMode |= FILE_SHARE_READ | FILE_SHARE_WRITE;
  if (HAS_FLAG (pmode, _S_IREAD))
    dwShareMode |= FILE_SHARE_READ;

  /* Override access mode with pmode if creating file.  */
  if (HAS_FLAG (oflag, _O_CREAT))
    {
      if (HAS_FLAG (pmode, _S_IWRITE))
        dwDesiredAccess |= FILE_GENERIC_WRITE;
      if (HAS_FLAG (pmode, _S_IREAD))
        dwDesiredAccess |= FILE_GENERIC_READ;
    }

  /* Create file disposition.  */
  DWORD dwCreationDisposition = 0;
  if (HAS_FLAG (oflag, (_O_CREAT | _O_EXCL)))
    dwCreationDisposition |= CREATE_NEW;
  else if (HAS_FLAG (oflag, _O_TRUNC | _O_CREAT))
    dwCreationDisposition |= CREATE_ALWAYS;
  else if (HAS_FLAG (oflag, _O_TRUNC) && !HAS_FLAG (oflag, O_RDONLY))
    dwCreationDisposition |= TRUNCATE_EXISTING;
  else if (HAS_FLAG (oflag, _O_APPEND))
    dwCreationDisposition |= OPEN_EXISTING;
  else if (HAS_FLAG (oflag, _O_CREAT))
    dwCreationDisposition |= OPEN_ALWAYS;
  else
    dwCreationDisposition |= OPEN_EXISTING;

  /* Set file access attributes.  */
  DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
  if (HAS_FLAG (oflag, _O_RDONLY))
    dwFlagsAndAttributes |= 0; /* No special attribute.  */
  if (HAS_FLAG (oflag, _O_TEMPORARY))
    dwFlagsAndAttributes |= FILE_FLAG_DELETE_ON_CLOSE;
  if (HAS_FLAG (oflag, _O_SHORT_LIVED))
    dwFlagsAndAttributes |= FILE_ATTRIBUTE_TEMPORARY;
  if (HAS_FLAG (oflag, _O_RANDOM))
    dwFlagsAndAttributes |= FILE_FLAG_RANDOM_ACCESS;
  if (HAS_FLAG (oflag, _O_SEQUENTIAL))
    dwFlagsAndAttributes |= FILE_FLAG_SEQUENTIAL_SCAN;
  /* Flag is only valid on it's own.  */
  if (dwFlagsAndAttributes != FILE_ATTRIBUTE_NORMAL)
    dwFlagsAndAttributes &= ~FILE_ATTRIBUTE_NORMAL;

  /* Ensure we have shared read for files which are opened read-only. */
  if (HAS_FLAG (dwCreationDisposition, OPEN_EXISTING)
      && ((dwDesiredAccess & (GENERIC_WRITE|GENERIC_READ)) == GENERIC_READ))
    dwShareMode |= FILE_SHARE_READ;

  /* Set security attributes.  */
  SECURITY_ATTRIBUTES securityAttributes;
  ZeroMemory (&securityAttributes, sizeof(SECURITY_ATTRIBUTES));
  securityAttributes.bInheritHandle       = !(oflag & _O_NOINHERIT);
  securityAttributes.lpSecurityDescriptor = NULL;
  securityAttributes.nLength              = sizeof(SECURITY_ATTRIBUTES);

  wchar_t* _filename = FS(create_device_name) (filename);
  if (!_filename)
    return -1;

  HANDLE hResult
    = CreateFileW (_filename, dwDesiredAccess, dwShareMode, &securityAttributes,
                   dwCreationDisposition, dwFlagsAndAttributes, NULL);

  free (_filename);
  if (INVALID_HANDLE_VALUE == hResult)
    return setErrNoFromWin32Error ();

  /* Now we have a Windows handle, we have to convert it to an FD and apply
     the remaining flags.  */
  const int flag_mask = _O_APPEND | _O_RDONLY | _O_TEXT | _O_WTEXT;
  int fd = _open_osfhandle ((intptr_t)hResult, oflag & flag_mask);
  if (-1 == fd)
    return setErrNoFromWin32Error ();

  /* Finally we can change the mode to the requested one.  */
  const int mode_mask = _O_TEXT | _O_BINARY | _O_U16TEXT | _O_U8TEXT | _O_WTEXT;
  if ((oflag & mode_mask) && (-1 == _setmode (fd, oflag & mode_mask)))
    return setErrNoFromWin32Error ();

  return fd;
}

int FS(translate_mode) (const wchar_t* mode)
{
  int oflag = 0;
  int len = wcslen (mode);
  int i;
  #define IS_EXT(X) ((i < (len - 1)) && mode[i+1] == X)

  for (i = 0; i < len; i++)
    {
      switch (mode[i])
        {
          case L'a':
            if (IS_EXT (L'+'))
              oflag |= _O_RDWR | _O_CREAT | _O_APPEND;
            else
              oflag |= _O_WRONLY | _O_CREAT | _O_APPEND;
            break;
          case L'r':
            if (IS_EXT (L'+'))
              oflag |= _O_RDWR;
            else
              oflag |= _O_RDONLY;
            break;
          case L'w':
            if (IS_EXT (L'+'))
              oflag |= _O_RDWR | _O_CREAT | _O_TRUNC;
            else
              oflag |= _O_WRONLY | _O_CREAT | _O_TRUNC;
            break;
          case L'b':
            oflag |= _O_BINARY;
            break;
          case L't':
            oflag |= _O_TEXT;
            break;
          case L'c':
          case L'n':
            oflag |= 0;
            break;
          case L'S':
            oflag |= _O_SEQUENTIAL;
            break;
          case L'R':
            oflag |= _O_RANDOM;
            break;
          case L'T':
            oflag |= _O_SHORT_LIVED;
            break;
          case L'D':
            oflag |= _O_TEMPORARY;
            break;
          default:
            if (wcsncmp (mode, L"ccs=UNICODE", 11) == 0)
              oflag |= _O_WTEXT;
            else if (wcsncmp (mode, L"ccs=UTF-8", 9) == 0)
              oflag |= _O_U8TEXT;
            else if (wcsncmp (mode, L"ccs=UTF-16LE", 12) == 0)
              oflag |= _O_U16TEXT;
            else continue;
        }
    }
  #undef IS_EXT

  return oflag;
}

FILE *FS(fwopen) (const wchar_t* filename, const wchar_t* mode)
{
  int shflag = 0;
  int pmode  = 0;
  int oflag  = FS(translate_mode) (mode);

  int fd = FS(swopen) (filename, oflag, shflag, pmode);
  if (fd < 0)
    return NULL;

  FILE* file = _wfdopen (fd, mode);
  return file;
}

FILE *FS(fopen) (const char* filename, const char* mode)
{
  size_t len = mbstowcs (NULL, filename, 0);
  wchar_t *w_filename = malloc (sizeof (wchar_t) * (len + 1));
  mbstowcs (w_filename, filename, len);
  w_filename[len] = L'\0';

  len = mbstowcs (NULL, mode, 0);
  wchar_t *w_mode = malloc (sizeof (wchar_t) * (len + 1));
  mbstowcs (w_mode, mode, len);
  w_mode[len] = L'\0';

  FILE *result = FS(fwopen) (w_filename, w_mode);
  free (w_filename);
  free (w_mode);

  return result;
}

int FS(sopen) (const char* filename, int oflag, int shflag, int pmode)
{
  size_t len = mbstowcs (NULL, filename, 0);
  wchar_t *w_filename = malloc (sizeof (wchar_t) * (len + 1));
  mbstowcs (w_filename, filename, len);
  w_filename[len] = L'\0';

  int result = FS(swopen) (w_filename, oflag, shflag, pmode);
  free (w_filename);

  return result;
}

int FS(_stat) (const char *path, struct _stat *buffer)
{
  size_t len = mbstowcs (NULL, path, 0);
  wchar_t *w_path = malloc (sizeof (wchar_t) * (len + 1));
  mbstowcs (w_path, path, len);
  w_path[len] = L'\0';

  int result = FS(_wstat) (w_path, buffer);
  free (w_path);

  return result;
}

int FS(_stat64) (const char *path, struct __stat64 *buffer)
{
  size_t len = mbstowcs (NULL, path, 0);
  wchar_t *w_path = malloc (sizeof (wchar_t) * (len + 1));
  mbstowcs (w_path, path, len);
  w_path[len] = L'\0';

  int result = FS(_wstat64) (w_path, buffer);
  free (w_path);

  return result;
}

static __time64_t ftToPosix(FILETIME ft)
{
  // takes the last modified date
  LARGE_INTEGER date, adjust;
  date.HighPart = ft.dwHighDateTime;
  date.LowPart = ft.dwLowDateTime;

  // 100-nanoseconds = milliseconds * 10000
  adjust.QuadPart = 11644473600000 * 10000;

  // removes the diff between 1970 and 1601
  date.QuadPart -= adjust.QuadPart;

  // converts back from 100-nanoseconds to seconds
  return (__time64_t)date.QuadPart / 10000000;
}

int FS(_wstat) (const wchar_t *path, struct _stat *buffer)
{
  ZeroMemory (buffer, sizeof (struct _stat));
  wchar_t* _path = FS(create_device_name) (path);
  if (!_path)
    return -1;

    /* Construct shared mode.  */
  DWORD dwShareMode = FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE;
  DWORD dwDesiredAccess = FILE_READ_ATTRIBUTES;
  DWORD dwFlagsAndAttributes = FILE_FLAG_BACKUP_SEMANTICS;
  DWORD dwCreationDisposition = OPEN_EXISTING;

  SECURITY_ATTRIBUTES securityAttributes;
  ZeroMemory (&securityAttributes, sizeof(SECURITY_ATTRIBUTES));
  securityAttributes.bInheritHandle       = false;
  securityAttributes.lpSecurityDescriptor = NULL;
  securityAttributes.nLength              = sizeof(SECURITY_ATTRIBUTES);

  HANDLE hResult
    = CreateFileW (_path, dwDesiredAccess, dwShareMode, &securityAttributes,
                   dwCreationDisposition, dwFlagsAndAttributes, NULL);

  if (INVALID_HANDLE_VALUE == hResult)
    {
      free (_path);
      return setErrNoFromWin32Error ();
    }

  WIN32_FILE_ATTRIBUTE_DATA finfo;
  ZeroMemory (&finfo, sizeof (WIN32_FILE_ATTRIBUTE_DATA));
  if(!GetFileAttributesExW (_path, GetFileExInfoStandard, &finfo))
    {
      free (_path);
      CloseHandle (hResult);
      return setErrNoFromWin32Error ();
    }

  unsigned short mode = _S_IREAD;

  if (finfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    mode |= (_S_IFDIR | _S_IEXEC);
  else
  {
    mode |= _S_IFREG;
    DWORD type;
    if (GetBinaryTypeW (_path, &type))
      mode |= _S_IEXEC;
  }

  if (!(finfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
    mode |= _S_IWRITE;

  buffer->st_mode  = mode;
  buffer->st_nlink = 1;
  buffer->st_size  = ((uint64_t)finfo.nFileSizeHigh << 32) + finfo.nFileSizeLow;
  buffer->st_atime = ftToPosix (finfo.ftLastAccessTime);
  buffer->st_mtime = buffer->st_ctime = ftToPosix (finfo.ftLastWriteTime);
  free (_path);
  CloseHandle (hResult);
  return 0;
}

int FS(_wstat64) (const wchar_t *path, struct __stat64 *buffer)
{
  struct _stat buf;
  ZeroMemory (buffer, sizeof (struct __stat64));

  int result = FS(_wstat) (path, &buf);

  buffer->st_mode = buf.st_mode;
  buffer->st_nlink = 1;
  buffer->st_size = buf.st_size;
  buffer->st_atime = buf.st_atime;
  buffer->st_mtime = buf.st_mtime;

  return result;
}

#else
FILE *FS(fopen) (const char* filename, const char* mode)
{
  return fopen (filename, mode);
}
#endif
