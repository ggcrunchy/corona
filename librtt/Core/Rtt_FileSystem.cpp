//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Rtt_FileSystem.h"
#include "Rtt_Assert.h"
#include "Core/Rtt_Build.h"
#include "Core/Rtt_String.h"
#include "Core/Rtt_Data.h"

#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#if defined( Rtt_WIN_ENV )
	#include <io.h>
	#include <share.h>
	#include <windows.h>
	#include <direct.h>
	#ifdef Rtt_WIN_DESKTOP_ENV
		#include <Shlobj.h>
		#include <Shlwapi.h>
		#include <map>
	#endif
#else
	#include <fcntl.h>
	#include <unistd.h>
	#include <sys/mman.h>
	#include <dirent.h>
	#include <sys/stat.h>
	#include <errno.h>

	#ifdef Rtt_MAC_ENV
		#include <copyfile.h>
	#endif

#if defined(_WIN32) /* e.g. MinGW */
	#include <direct.h>   // _mkdir
#endif

#endif // Rtt_WIN_ENV

#ifdef Rtt_MAC_ENV
// These Carbon calls are deprecated as of 10.8 but the only viable replacements are Cocoa only
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <Carbon/Carbon.h>

#endif


// ----------------------------------------------------------------------------
// Private Functions
// ----------------------------------------------------------------------------

#if defined( Rtt_WIN_ENV )

#ifdef Rtt_WIN_DESKTOP_ENV
	// Stores a collection of Win32 related memory mapped file info so that it can be unmapped later.
	// Key:		Pointer to the memory mapped file buffer.
	// Value:	HANDLE returned by the Win32 CreateFileMapping() function.
	typedef std::map<const void*, HANDLE> WindowsMemoryMappedInfoCollection;
	typedef std::pair<const void*, HANDLE> WindowsMemoryMappedInfoPair;
	static WindowsMemoryMappedInfoCollection sWindowsMemoryMappedInfoCollection;
#endif // Rtt_WIN_DESKTOP_ENV

static void ConvertUtf16ToUtf8(const wchar_t* utf16String, char* utf8String, size_t utf8StringLength)
{
	int conversionLength;

	if (utf16String && utf8String && (utf8StringLength > 0))
	{
		conversionLength = WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, NULL, 0, NULL, NULL);
		if ((conversionLength > 0) && (conversionLength <= (int)utf8StringLength))
		{
			WideCharToMultiByte(CP_UTF8, 0, utf16String, -1, utf8String, conversionLength, NULL, NULL);
		}
	}
}

static void ConvertUtf8ToUtf16(const char* utf8String, wchar_t* utf16String, size_t utf16StringLength)
{
	size_t utf8StringLength;
	int conversionLength;

	utf8StringLength = strlen(utf8String);
	if (utf16String && (utf8StringLength > 0))
	{
		conversionLength = MultiByteToWideChar(CP_UTF8, 0, utf8String, utf8StringLength + 1, NULL, 0);
		if ((conversionLength > 0) && (conversionLength <= (int)utf16StringLength))
		{
			MultiByteToWideChar(CP_UTF8, 0, utf8String, utf8StringLength + 1, utf16String, conversionLength);
		}
	}
}

/* IDEA (to replace Create/DestroyUTF16String (and prefer stack)
	maybe templatize the length, with PATH_MAX as default? (generally these are paths, but mode can be much smaller)

class UTF16String {
public:
	UTFString( const char *utf8String )
	:	fPointer( NULL )
	{
		int conversionLength = MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, NULL, 0);
		if (conversionLength > sizeof(fArray))
		{
			fPointer = (wchar_t*)Rtt_MALLOC(NULL, (size_t)conversionLength * sizeof(wchar_t));
		}
		else
		{
			fPointer = fArray;
		}
			
		if (conversionLength > 0)
		{
			MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, fPointer, conversionLength);
		}
		else
		{
			*fPointer = L'\0';
		}
	}
	
	~UTFString()
	{
		if ( fPointer != fArray )
		{
			Rtt_FREE( fPointer );
		}
	}
	
	wchar_t* Get() const { return fPointer; }
	
private:
	wchar_t *fPointer; 
	wchar_t fArray[PATH_MAX];
};

*/

static wchar_t* CreateUtf16StringFrom(const char* utf8String)
{
	wchar_t *utf16String = NULL;
	int conversionLength;

	if (utf8String)
	{
		conversionLength = MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, NULL, 0);
		if (conversionLength > 0)
		{
			utf16String = (wchar_t*)malloc((size_t)conversionLength * sizeof(wchar_t));
			MultiByteToWideChar(CP_UTF8, 0, utf8String, -1, utf16String, conversionLength);
		}
	}
	return utf16String;
}

static void DestroyUtf16String(wchar_t *utf16String)
{
	if (utf16String)
	{
		free(utf16String);
	}
}

#endif // Rtt_WIN_ENV


// ----------------------------------------------------------------------------
// Public Functions
// ----------------------------------------------------------------------------

Rtt_EXPORT FILE* Rtt_FileOpen(const char *filePath, const char *mode)
{
	FILE *fileHandle = NULL;

#if defined( Rtt_WIN_ENV )
	// TODO: UTF16String utf16Filename(filePath); etc.
	wchar_t *utf16Filename = CreateUtf16StringFrom(filePath);
	wchar_t *utf16Mode = CreateUtf16StringFrom(mode);
	errno_t errorCode = _wfopen_s(&fileHandle, utf16Filename, utf16Mode);
	DestroyUtf16String(utf16Filename);
	DestroyUtf16String(utf16Mode);
#else
	fileHandle = fopen(filePath, mode);
#endif // Rtt_WIN_ENV

	return fileHandle;
}

Rtt_EXPORT int Rtt_FileFlush(FILE* fileHandle)
{
	return fflush(fileHandle);
}

Rtt_EXPORT size_t Rtt_FileRead(void* ptr, size_t size, size_t count, FILE* fileHandle)
{
	return fread(ptr, size, count, fileHandle);
}

Rtt_EXPORT int Rtt_FileEof(FILE* fileHandle)
{
	return feof(fileHandle);
}

Rtt_EXPORT int Rtt_FileClose(FILE *fileHandle)
{
	return fclose(fileHandle);
}

Rtt_EXPORT int Rtt_FileError(FILE *fileHandle)
{
	return ferror(fileHandle);
}

Rtt_EXPORT int Rtt_FileGetC(FILE *fileHandle)
{
	return getc(fileHandle);
}

Rtt_EXPORT int Rtt_FileUngetC(int c, FILE *fileHandle)
{
	return ungetc(c, fileHandle);
}

Rtt_EXPORT void Rtt_FileClearerr(FILE *fileHandle)
{
	clearerr(fileHandle);
}

Rtt_EXPORT long int Rtt_FileTell(FILE *fileHandle)
{
	return ftell(fileHandle);
}

Rtt_EXPORT int Rtt_FileSeek(FILE *fileHandle, long int offset, int origin)
{
	return fseek(fileHandle, offset, origin);
}

Rtt_EXPORT void Rtt_FileRewind(FILE *fileHandle)
{
	rewind(fileHandle);
}

Rtt_EXPORT int Rtt_FileDescriptorOpen(const char *filePath, int oflags, int pmode)
{
	int fileDescriptor = -1;

#if defined(Rtt_WIN_ENV)
	wchar_t *utf16Filename = CreateUtf16StringFrom(filePath);
	errno_t errorCode = _wsopen_s(&fileDescriptor, utf16Filename, oflags, SH_DENYWR, pmode);
	DestroyUtf16String(utf16Filename);
#else
	fileDescriptor = open(filePath, oflags, pmode);
#endif // Rtt_WIN_ENV

	return fileDescriptor;
}

Rtt_EXPORT void Rtt_FileDescriptorClose(int fileDescriptor)
{
#if defined(Rtt_WIN_ENV)
	_close(fileDescriptor);
#else
	close(fileDescriptor);
#endif
}

#ifdef Rtt_WIN_ENV
Rtt_EXPORT int Rtt_FileIsHidden(const char *filePath)
{
	int isHidden = 0;
	if (filePath && (filePath[0] != '\0'))
	{
		WIN32_FILE_ATTRIBUTE_DATA attributes;
		wchar_t *utf16FilePath = CreateUtf16StringFrom(filePath);
		BOOL wasFound = GetFileAttributesExW(utf16FilePath, GetFileExInfoStandard, &attributes);
		if (wasFound && (attributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
		{
			isHidden = 1;
		}
		DestroyUtf16String(utf16FilePath);
	}
	return isHidden;
}
#endif

Rtt_EXPORT int Rtt_FileStatus(const char *filePath, struct stat *buffer)
{
#if defined( Rtt_WIN_ENV )
	wchar_t *utf16FilePath = CreateUtf16StringFrom(filePath);
	int result = _wstat(utf16FilePath, (struct _stat*)buffer);
	DestroyUtf16String(utf16FilePath);
#else
	int result = lstat(filePath, buffer);
#endif // Rtt_WIN_ENV

	return result;
}

Rtt_EXPORT int Rtt_FileExists(const char *filePath)
{
#ifdef Rtt_WIN_ENV
	WIN32_FILE_ATTRIBUTE_DATA attributes;
	wchar_t *utf16Filename = CreateUtf16StringFrom(filePath);
	BOOL wasFound = GetFileAttributesExW(utf16Filename, GetFileExInfoStandard, &attributes);
	DestroyUtf16String(utf16Filename);
	return wasFound ? 1 : 0;
#else
	struct stat buffer;

	return (int(stat (filePath, &buffer) == 0));
#endif // Rtt_WIN_ENV
}

//
// Determine if any part of the file path specified in the Lua code differs
// in case from the actual path on disk.  This is important because filesystems on
// devices are typically case-sensitive while those on desktops are not.  If the
// file on disk does not have the same case, then "actualPath" is set to an allocated
// copy of the actual name (the caller should free this)
//
Rtt_EXPORT int Rtt_FileExistsWithSameCase(const char *filePath, const char *rootPath, char **actualPath )
{
	int result = 1;

#ifdef Rtt_WIN_DESKTOP_ENV

	wchar_t *wRootPath = NULL;
	char filesystemName[MAX_PATH];

	if (rootPath == NULL)
	{
		// Due to the way Windows handles path elements like "Shared Folders" (which has no "filename") 
		// a rootPath must be given so we know when to stop iterating over the filePath.  Default to
		// the filePath minus the last component
		wRootPath = CreateUtf16StringFrom(filePath);
		PathRemoveFileSpecW(wRootPath);
	}
	else
	{
		wRootPath = CreateUtf16StringFrom(rootPath);
	}

	if (filePath == NULL)
	{
		// Degenerate case, assume success
		return result;
	}

	wchar_t *wFilePath = CreateUtf16StringFrom(filePath);

	// Normalize path separators (required to use certain Win32 APIs)
	wchar_t *ptr = wFilePath;
	while ((ptr = wcschr(ptr, L'/')) != NULL)
	{
		*ptr = L'\\';
	}

	// Loop backwards through the elements of the path until we reach the root (typically the project's resource directory)
	while (wcscmp(wRootPath, wFilePath) != 0)
	{
		// Separate the filename from its parent directory's path
		LPWSTR wFileName = ::PathFindFileNameW(wFilePath);
		WIN32_FIND_DATAW data = { };
		SetLastError(ERROR_SUCCESS); // needed to ensure subsequent check of GetLastError() is reliable
		HANDLE hFind = FindFirstFileW(wFilePath, &data);

		FindClose(hFind); // we don't need the iterator

		// If we can't find the file at all, return failure
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			result = 0;
			break;
		}
		else if (!data.cFileName || (L'\0' == data.cFileName[0]))
		{
			// The string is empty if we've reached the end of the path.
			// This means that the casing of the filename and its subdirectories are correct.
			// Note: We'll only end up here if argument "filePath" is not prefixed with "rootPath".
			break;
		}
		else if ((wcscmp(wFileName, L".") == 0) || (wcscmp(wFileName, L"..") == 0))
		{
			// Skip over a "." or ".." in the path.
			// We only need to validate directory and file names.
		}
		else
		{
			// The magic happens here: the filename returned by FindFirstFile() will have the actual case
			// used in the filesystem, if it doesn't match what was in the path we have a case difference
			if (wcscmp(wFileName, data.cFileName) != 0)
			{
				result = 0;
				ConvertUtf16ToUtf8(data.cFileName, filesystemName, MAX_PATH);
				break;
			}
		}

		// PathRemoveFileSpec is deprecated in favor of PathCchRemoveFileSpec but that isn't in XP
		if (! ::PathRemoveFileSpecW(wFilePath))
		{
			break;
		}
	}

	DestroyUtf16String(wFilePath);
	DestroyUtf16String(wRootPath);

	if (result == 0 && actualPath != NULL)
	{
		*actualPath = strdup(filesystemName);
	}

#elif Rtt_MAC_ENV

	FSRef ref;
	OSStatus status;
	unsigned char filesystemName[PATH_MAX + 1] = { 0 };

	// first get an FSRef for the path
	status = FSPathMakeRef((unsigned char *)filePath, &ref, NULL);

	if (status == 0)
	{
		// then get a path from the FSRef
		status = FSRefMakePath(&ref, filesystemName, PATH_MAX);

		if (status != 0 || strcmp((char *)filesystemName, filePath) != 0)
		{
			result = 0;
		}
	}

	if (result == 0 && status == 0 && actualPath != NULL)
	{
		*actualPath = strdup((char *)filesystemName);
	}

#else
	// Just determine file existence on other platforms
	result = Rtt_FileExists(filePath);

	if (result == 0 && actualPath != NULL)
	{
		*actualPath = strdup(filePath);
	}

#endif // Rtt_WIN_ENV

	return result;
}

// file wrapper for NX, it's needs to flush data
#if defined(Rtt_NXS_ENV)

nnFile::nnFile(const char* path, const char* mode)
	: fHandle({})
	, fPos(0)
{
	nn::Result result;
	int m;
	if (mode[0] == 'w')
	{
		result = nn::fs::DeleteFile(path);
		result = nn::fs::CreateFile(path, 0);
		m = nn::fs::OpenMode::OpenMode_Write | nn::fs::OpenMode::OpenMode_AllowAppend;
	}
	else if (mode[0] == 'r')
		m = nn::fs::OpenMode::OpenMode_Read;
	else
		// todo
		Rtt_ASSERT_NOT_REACHED();

	result = nn::fs::OpenFile(&fHandle, path, m);
	if (result.IsFailure())
	{
		int err = result.GetDescription();
		Rtt_Log("Failed to open %s, err=%d\n", path, err);
	}
}

nnFile::~nnFile()
{
	close();
}

bool nnFile::isOpen() const
{
	return fHandle.handle != NULL;
}

int nnFile::write(void* data, int size)
{
	nn::Result result;
	
	// Specify this flag to flush immediately after writing
	nn::fs::WriteOption wo = nn::fs::WriteOption::MakeValue(nn::fs::WriteOptionFlag_Flush);

	result = nn::fs::WriteFile(fHandle, fPos, data, size, wo);
	if (result.IsFailure())
	{
		int err = result.GetDescription();
		Rtt_Log("Write failed, err=%d\n", err);
		return -1;
	}

	nn::fs::FlushFile(fHandle);
	fPos += size;
	return size;
}

void nnFile::close()
{
	if (isOpen())
	{
		nn::fs::FlushFile(fHandle);
		nn::fs::CloseFile(fHandle);
		fHandle = {};
	}
}

#endif

#ifndef Rtt_WIN_PHONE_ENV
Rtt_EXPORT void* Rtt_FileMemoryMap(int fileDescriptor, size_t byteOffset, size_t byteCount, int canWrite)
{
	void *memoryMapPointer = NULL;
	if (byteCount > 0)
	{
#if defined( Rtt_WIN_DESKTOP_ENV )
		HANDLE fileHandle = (HANDLE)_get_osfhandle(fileDescriptor);
		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			DWORD protectionMode = canWrite ? PAGE_READWRITE : PAGE_READONLY;
			HANDLE mappingHandle = ::CreateFileMapping(fileHandle, NULL, protectionMode, 0, 0, NULL);
			if (mappingHandle)
			{
				DWORD accessMode = canWrite ? FILE_MAP_WRITE : FILE_MAP_READ;
				memoryMapPointer = ::MapViewOfFile(
					mappingHandle, accessMode, HIWORD(byteOffset), LOWORD(byteOffset), byteCount);
				if (memoryMapPointer)
				{
					sWindowsMemoryMappedInfoCollection.insert(WindowsMemoryMappedInfoPair(memoryMapPointer, mappingHandle));
				}
				else
				{
					::CloseHandle(mappingHandle);
				}
			}
		}
#elif defined(Rtt_NXS_ENV)
		return NULL;
#else
		int accessFlags = PROT_READ;
		if (canWrite)
		{
			accessFlags |= PROT_WRITE;
		}
		memoryMapPointer = mmap(NULL, byteCount, accessFlags, MAP_SHARED, fileDescriptor, byteOffset);
		if (MAP_FAILED == memoryMapPointer)
		{
			memoryMapPointer = NULL;
		}
#endif // Rtt_WIN_DESKTOP_ENV
	}

	return memoryMapPointer;
}

Rtt_EXPORT void Rtt_FileMemoryUnmap(const void *memoryMapPointer, size_t byteCount)
{
	if (memoryMapPointer)
	{
#if defined( Rtt_WIN_DESKTOP_ENV )
		WindowsMemoryMappedInfoCollection::iterator iter = sWindowsMemoryMappedInfoCollection.find(memoryMapPointer);
		if (iter != sWindowsMemoryMappedInfoCollection.end())
		{
			::UnmapViewOfFile(memoryMapPointer);
			::CloseHandle((*iter).second);
			sWindowsMemoryMappedInfoCollection.erase(iter);
		}
#else
		munmap((void*)memoryMapPointer, byteCount);
#endif // Rtt_WIN_DESKTOP_ENV
	}
}

Rtt_EXPORT void Rtt_FileMemoryFlush(const void *memoryMapPointer, size_t byteCount)
{
	if (memoryMapPointer != NULL)
	{
#if defined( Rtt_WIN_DESKTOP_ENV )
		FlushViewOfFile(memoryMapPointer, byteCount);
#else
		msync((void *)memoryMapPointer, byteCount, MS_SYNC);
#endif // Rtt_WIN_DESKTOP_ENV
	}
}

Rtt_EXPORT int Rtt_CopyFile(const char *srcFilePath, const char *dstFilePath)
{
	int result = 1;

#if defined( Rtt_WIN_ENV )
	wchar_t *utf16SrcFilePath = CreateUtf16StringFrom(srcFilePath);
	wchar_t *utf16DstFilePath = CreateUtf16StringFrom(dstFilePath);

	result = CopyFileW(utf16SrcFilePath, utf16DstFilePath, FALSE);

	DestroyUtf16String(utf16SrcFilePath);
	DestroyUtf16String(utf16DstFilePath);
#elif defined( Rtt_MAC_ENV )
	result = copyfile(srcFilePath, dstFilePath, NULL, COPYFILE_DATA | COPYFILE_STAT) == 0;
#else
	FILE *inFp = NULL;
	FILE *outFp = NULL;
	
	if ((inFp = Rtt_FileOpen(srcFilePath, "rb")) == NULL)
	{
		printf("Rtt_CopyFile: failed to open '%s' for reading\n", srcFilePath);

		result = 0;
	}
	
	if (result && (outFp = Rtt_FileOpen(dstFilePath, "wb")) == NULL)
	{
		printf("Rtt_CopyFile: failed to open '%s' for writing\n", dstFilePath);

		result = 0;
	}

	char buf[BUFSIZ];

	while (result)
	{
		ssize_t bytesRead = read(fileno(inFp), buf, BUFSIZ);
		if ( 0 == bytesRead )
		{
			break;
		}
		else if ( bytesRead < 0 )
		{
			if (EINTR == errno) /* interrupted by signal? */
 			{
				continue;
			}
			else
			{
				// n.b. previously this just ended the loop, i.e. a truncated copy "succeeded"
				printf("Rtt_CopyFile: failed to read '%s': %s\n", srcFilePath, strerror(errno));

				result = 0;

				break;
			}
		}
		
		for ( ssize_t totalWritten = 0; totalWritten < bytesRead; )
		{
			int bytesWritten = write(fileno(outFp), buf + totalWritten, bytesRead - totalWritten);
			if ( bytesWritten < 0 && EINTR == errno ) /* interrupted by signal? */
			{
				continue;
 			}
			else if ( bytesWritten <= 0 )
			{
				printf("Rtt_CopyFile: failed to write %s: %s\n", dstFilePath, strerror(errno));
				
				result = 0;
				
				break;
			}
			else
			{
				totalWritten += bytesWritten;
			}
		}
	}

	struct stat source_stat;

	if (result && fstat(fileno(inFp), &source_stat) == -1)
	{
		printf("Rtt_CopyFile: failed to fstat '%s'\n", srcFilePath);
		
		result = 0;
	}

	if (result && fchmod(fileno(outFp), ( source_stat.st_mode & 0777 )) == -1)
	{
		printf("Rtt_CopyFile: failed to fchmod '%s'\n", dstFilePath);
		
		result = 0;
	}

	if ( inFp )
	{
		Rtt_FileClose(inFp);
	}
	
	if ( outFp && 0 != Rtt_FileClose(outFp) ) // n.b. the close can be what reports a failed (buffered / NFS) write
 	{
		printf("Rtt_CopyFile: failed to close '%s': %s\n", dstFilePath, strerror(errno));

		result = 0;
 	}
#endif

	return result;
}

// Adapted from https://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
Rtt_EXPORT int Rtt_MakeDirectory(const char *dirPath)
{
	int result = false;
	std::string path = dirPath;

#ifdef Rtt_WIN_ENV
		// n.b. _mkdir() takes the ANSI code page, so non-ASCII (UTF-8) paths would fail or go astray
		wchar_t *wpath = CreateUtf16StringFrom(path.c_str());
		int ret = wpath ? _wmkdir(wpath) : -1;
		DestroyUtf16String(wpath);
#elif defined(_WIN32)
		int ret = _mkdir(path.c_str());
#else
		mode_t mode = 0755;
		int ret = mkdir(path.c_str(), mode);
#endif
		if (ret == 0)
		{
			return true;
		}

		switch (errno)
		{
			case ENOENT:  // parent doesn't exist, try to create it
			{
#if defined(_WIN32)
				size_t pos = path.find_last_of("/\\"); // n.b. whichever is last, not '/' first
#else
				size_t pos = path.find_last_of('/');
#endif

				if (pos == std::string::npos)
				{
					return false;
				}
				if (!Rtt_MakeDirectory( path.substr(0, pos).c_str() ))
				{
					return false;
				}
			}
				// now, try to create again
#if defined( Rtt_WIN_ENV )
			{
				wchar_t *wpath = CreateUtf16StringFrom(path.c_str());
				int created = wpath && (0 == _wmkdir(wpath));
				DestroyUtf16String(wpath);

				return created;
			}
#elif defined(_WIN32)
				return 0 == _mkdir(path.c_str());
#else
				return 0 == mkdir(path.c_str(), mode);
#endif

			case EEXIST: // done!
				return true;

			default:
				return false;
		}

		return result;
}

Rtt_EXPORT int Rtt_DeleteFile(const char *filePath)
{
	int result = 0;

#ifdef Rtt_WIN_ENV
	wchar_t *path = CreateUtf16StringFrom(filePath);
	if (path)
	{
		result = DeleteFileW(path);

		if (!result && ERROR_ACCESS_DENIED == GetLastError())
		{
			// read-only files (e.g. from git checkouts or copied resources) refuse deletion
			DWORD attribs = GetFileAttributesW(path);
			if (INVALID_FILE_ATTRIBUTES != attribs && (attribs & FILE_ATTRIBUTE_READONLY))
			{
				SetFileAttributesW(path, attribs & ~FILE_ATTRIBUTE_READONLY);
				result = DeleteFileW(path);
			}
		}

		DestroyUtf16String(path);
	}
#else
	result = unlink(filePath) == 0;
#endif

	return result;
}

Rtt_EXPORT int Rtt_IsDirectory(const char *dirPath)
{
	bool result = false;

	#ifdef Rtt_WIN_ENV
		wchar_t *path = CreateUtf16StringFrom(dirPath);
		result = ::PathIsDirectoryW(path) ? true : false;
		DestroyUtf16String(path);
	#else
		DIR *subdp = opendir(dirPath);
		if ( subdp )
		{
			closedir( subdp );
			subdp = NULL;
			result = true;
		}
	#endif

	return result;
}

enum DeleteEntryKind
{
	kDeleteEntryMissing,
	kDeleteEntryFile,
	kDeleteEntryDirectory,
	kDeleteEntryLink, // symlink or reparse point
};

static DeleteEntryKind ClassifyForDelete( const char *path )
{
	#ifdef Rtt_WIN_ENV
		wchar_t *wpath = CreateUtf16StringFrom(path);
		DWORD attribs = wpath ? GetFileAttributesW(wpath) : INVALID_FILE_ATTRIBUTES;
		DestroyUtf16String(wpath);

		if ( INVALID_FILE_ATTRIBUTES == attribs )
		{
			return kDeleteEntryMissing;
		}
		else if ( attribs & FILE_ATTRIBUTE_REPARSE_POINT )
		{
			return kDeleteEntryLink;
 		}
		else
		{
			return ( attribs & FILE_ATTRIBUTE_DIRECTORY ) ? kDeleteEntryDirectory : kDeleteEntryFile;
		}
	#else
		struct stat s;

		if ( 0 != lstat( path, &s ) )
		{
			return kDeleteEntryMissing;
		}
		else if ( S_ISLNK( s.st_mode ) )
		{
			return kDeleteEntryLink;
		}
		else
		{
			return S_ISDIR( s.st_mode ) ? kDeleteEntryDirectory : kDeleteEntryFile;
		}
	#endif
}

static bool RemoveEmptyDirectory( const char *dirPath )
{
	#ifdef Rtt_WIN_ENV
		// n.b. not the CRT rmdir(), which takes the ANSI code page rather than UTF-8
		wchar_t *wpath = CreateUtf16StringFrom(dirPath);
		bool result = wpath && ::RemoveDirectoryW(wpath);
		DestroyUtf16String(wpath);

		return result;
	#else
		return 0 == rmdir(dirPath);
	#endif
}

static bool RemoveLink( const char *path ) // n.b. does not remove link target
{
	#ifdef Rtt_WIN_ENV
		return RemoveEmptyDirectory( path ) /* or junction / symlink */ || Rtt_DeleteFile( path ) /* or file symlink */;
	#else
		return 0 == unlink( path );
 	#endif
 }

Rtt_EXPORT int Rtt_DeleteDirectory(const char *dirPath)
{
	if ( kDeleteEntryDirectory != ClassifyForDelete( dirPath ) )
 	{
		fprintf(stderr, "Rtt_DeleteDirectory: '%s' is not a directory, or is a link\n", dirPath);

 		return 0;
 	}

	int result = 1;
	std::vector<std::string> fileList = Rtt_ListFiles(dirPath); // n.b. will filter "." and ".."

	for (std::vector<std::string>::iterator it = fileList.begin(); it != fileList.end(); ++it)
	{
		const char* path = it->c_str();

		switch (ClassifyForDelete(path))
		{
		case kDeleteEntryDirectory:
			result = Rtt_DeleteDirectory(path);
			break;
		case kDeleteEntryLink:
			result = RemoveLink(path);
			break;
		case kDeleteEntryFile:
 			result = Rtt_DeleteFile(path);
			break;
		default: // vanished in the meantime
			result = 1;
		}

		if (! result)
		{
			fprintf(stderr, "Rtt_DeleteDirectory: failed for '%s'\n", path);

			break;
		}
	}

	if ( 0 != result )
	{
		result = RemoveEmptyDirectory(dirPath);
	}

	return result;
}

std::vector<std::string> Rtt_ListFiles(const char *directoryName)
{
	std::vector<std::string> result;
#if defined( Rtt_WIN_ENV )
	wchar_t *path = CreateUtf16StringFrom(directoryName);
	std::wstring search_path = std::wstring(path) + L"/*.*";
	WIN32_FIND_DATA fd;

	HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd); 
	if(hFind != INVALID_HANDLE_VALUE)
	{ 
		do
		{ 
			char filename[MAX_PATH];
			ConvertUtf16ToUtf8(fd.cFileName, filename, MAX_PATH);

			if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
			{
				continue;
			}

			std::string path = directoryName;
			path.append("\\");
			path.append(filename);
			result.push_back(path);
	} while(::FindNextFile(hFind, &fd));

		::FindClose(hFind); 
	} 

	DestroyUtf16String(path);

#else
	DIR *dir;
	struct dirent *ent;

	if ((dir = opendir(directoryName)) == NULL)
	{
		return result;
	}

	while ((ent = readdir(dir)) != NULL)
	{
		std::string entry = ent->d_name;

		if (entry == "." || entry == "..")
		{
			continue;
		}

		std::string path = directoryName;

		path.append("/");
		path.append(entry);

		result.push_back( path );
	}

	closedir(dir);
#endif // Rtt_WIN_ENV

	return result;
}

size_t
Rtt_WriteDataToFile(const char *filename, Rtt::Data<const unsigned char> &data)
{
	size_t charsWritten = 0;

	FILE *fp = Rtt_FileOpen(filename, "wb");

	if (fp != NULL)
	{
		charsWritten = fwrite(data.GetData(), 1, data.GetLength(), fp);

		Rtt_ASSERT(charsWritten == (size_t)data.GetLength());

		Rtt_FileClose(fp);
	}
	
	return charsWritten;
}

Rtt_EXPORT char *Rtt_MakeTempDirectory(char *tmpDirTemplate)
{
#if defined(Rtt_WIN_ENV)
	if ( !tmpDirTemplate )
 	{
		errno = EINVAL;
 
		return NULL;
 	}

	const char kXs[] = "XXXXXX";
	const size_t kNumXs = sizeof( kXs ) - 1;
	size_t len = strlen( tmpDirTemplate );

	if ( ( len < kNumXs ) || 0 != strcmp( tmpDirTemplate + len - kNumXs, kXs ) )
	{
		errno = EINVAL;
 
		return NULL;
	}

	char *xs = tmpDirTemplate + len - kNumXs;
 
	// Mix a few varying sources to get a reasonably unique state.
	LARGE_INTEGER counter;
	QueryPerformanceCounter( &counter );
	
	ULONGLONG state = (ULONGLONG)counter.QuadPart ^ ( (ULONGLONG)GetCurrentProcessId() << 32 ) ^ (ULONGLONG)(size_t)tmpDirTemplate;

	const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";

	for ( int attempt = 0; attempt < 100; attempt++ )
	{
		for ( size_t i = 0; i < kNumXs; i++ )
 		{
			// splitmix64 step
			state += 0x9E3779B97F4A7C15ULL;

			ULONGLONG z = state;

			z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
			z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
			z ^= z >> 31;

			xs[i] = kChars[z % ( sizeof( kChars ) - 1 )];
		}

		wchar_t *wpath = CreateUtf16StringFrom( tmpDirTemplate );
		BOOL created = wpath && ::CreateDirectoryW( wpath, NULL );
		DWORD err = created ? ERROR_SUCCESS : GetLastError();

		DestroyUtf16String( wpath );

		if ( created )
		{
			return tmpDirTemplate;
 		}
		else if ( ERROR_ALREADY_EXISTS != err && ERROR_FILE_EXISTS != err )
		{
			break; // e.g. the parent is missing or not writable: retrying won't help
		}
	}

	strcpy( xs, kXs );
	
	errno = EEXIST;

	return NULL;
#else
	return mkdtemp(tmpDirTemplate);
#endif
}

Rtt_EXPORT const char *Rtt_GetSystemTempDirectory()
{
#ifdef Rtt_WIN_ENV

	return getenv("TEMP");

#else

	return "/tmp";

#endif
}



extern "C" size_t nFileTmpfile(void* ptr, size_t size, size_t count, FILE* fileHandle)
{
	Rtt_ASSERT(0 && "TODO");
	return -1;
}

#endif // ! Rtt_WIN_PHONE_ENV
