//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_PlatformAppPackager.h"

#include "Rtt_Archive.h"
#include "Rtt_DeviceBuildData.h"
#include "Rtt_Lua.h"
#include "Rtt_LuaFile.h"
#include "Rtt_MPlatform.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_MPlatformServices.h"
#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
#	include "Core/Rtt_FileSystem.h"
#	include "stdafx.h"
#	include "Interop/Ipc/CommandLine.h"
#	include "WinString.h"
#	include <Shlobj.h>
#if !defined( Rtt_NO_GUI )
#	include "Simulator.h"
#endif
#elif defined(Rtt_LINUX_ENV)
//  #include <uuid/uuid.h>
	#include <unistd.h>
#else
#	include <copyfile.h>
#	include <fnmatch.h>
#endif

#if !defined( Rtt_NO_GUI )
#	include "Rtt_LuaContext.h"
#	include "Rtt_Runtime.h"
#else
#   ifdef Rtt_WIN_ENV
#	    include "Rtt_WinConsolePlatform.h"
#   endif
#endif

#include "Rtt_MCrypto.h"
#include "Rtt_FileSystem.h"

#include <string>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

#include <fstream>
#include <sstream>
#ifdef Rtt_MAC_ENV
#include <uuid/uuid.h>
#elif Rtt_WIN_ENV
#endif

// ----------------------------------------------------------------------------

extern "C" {
	#if !defined(Rtt_NO_GUI)
		int luaopen_coronabaselib(lua_State *L);
	#endif

	int luaopen_lfs(lua_State *L);
}

namespace Rtt
{

int luaload_json(lua_State* L);

static const char *kUserPreferenceCustomBuildID = "userPreferenceCustomBuildID";
static const char *kUserPreferenceCustomDailyBuild = "userPreferenceCustomDailyBuild";
static const char *kCustomId = "customBuildId";
static const char *kAppSettingsLuaFile = "AppSettings.lua";
	
Rtt_EXPORT int Rtt_LuaCompile( lua_State *L, int numSources, const char** sources, const char* dstFile, int stripDebug );

#if defined(Rtt_WIN_ENV) && ( _MSC_VER >= 1800 ) && !defined(Rtt_LINUX_ENV)
/// <remarks>
///  On Windows, we export a lua_compile_files() function that calls the equivalent Rtt_LuaCompile() function in "luac.c".
///  This allows us to share the same "lua.dll" for both the Corona Simulator and Corona built Windows apps.
/// </remarks>
Rtt_EXPORT int Rtt_LuaCompile(lua_State *L, int numSources, const char** sources, const char* dstFile, int stripDebug)
{
	return lua_compile_files(L, numSources, sources, dstFile, stripDebug);
}
#endif

// ----------------------------------------------------------------------------

#define kDefaultNumBytes 128

AppPackagerParams::AppPackagerParams( const char* appName, 
	const char* version,
	const char* identity,
	const char* provisionFile,
	const char* srcDir,
	const char* dstDir,
	const char* sdkRoot,
	TargetDevice::Platform targetPlatform,
	const char * targetAppStoreName,
	S32 targetVersion,
	S32 targetDevice,
	const char * customBuildId,
	const char * productId,
	const char * appPackage,
	bool isDistributionBuild )
:	fTargetPlatform( targetPlatform ),
	fTargetVersion( targetVersion ),
	fTargetDevice( targetDevice ),
	fIsStripDebug( true ),
	fDeviceBuildData( NULL ),
	fIncludeBuildSettings( false ),
	fBuildCallbacksRef( LUA_NOREF )
,   fLiveBuild( false )
{
	fAppName.Set(appName);
	fVersion.Set(version);
	fIdentity.Set(identity);
	fProvisionFile.Set(provisionFile); 
	fSrcDir.Set(srcDir); 
	fDstDir.RTrim(LUA_DIRSEP);
	fDstDir.Set(dstDir);
	fDstDir.RTrim(LUA_DIRSEP);
	fSdkRoot.Set(sdkRoot); 
	fSdkRoot.RTrim(LUA_DIRSEP);
	fCustomBuildId.Set( customBuildId );
	if ( productId )
	{
		fProductId.Set( productId );
	}
	else
	{
		InitializeProductId( 0 );
	}
	fAppPackage.Set( appPackage );
	fCertType.Set( isDistributionBuild ? "distribution" : "developer" );
	fTargetAppStoreName.Set( targetAppStoreName );
	fIncludeFusePlugins = false;
	fUsesMonetization = false;
}

AppPackagerParams::~AppPackagerParams()
{
	delete fDeviceBuildData;
}

DeviceBuildData&
AppPackagerParams::GetDeviceBuildData( const MPlatform& platform, const MPlatformServices& services ) const
{
	if ( ! fDeviceBuildData )
	{
		Rtt_Allocator *pAllocator = & platform.GetAllocator();
		const char *clientPlatformName = platform.GetDevice().GetPlatformName();

		const char *deviceId = platform.GetDevice().GetUniqueIdentifier( MPlatformDevice::kDeviceIdentifier );
		
//		String filePath( & platform.GetAllocator() );
//		const char kBuildSettings[] = "build.settings";
//		platform.PathForFile( kBuildSettings, MPlatform::kResourceDir, MPlatform::kTestFileExists, filePath );
		const char *buildSettingsPath = fBuildSettingsPath.GetString();
		
		String appSettingsPath( pAllocator );
		platform.PathForFile( kAppSettingsLuaFile, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, appSettingsPath );

		fDeviceBuildData = new DeviceBuildData(
			pAllocator,
			fAppName.GetString(),
			fTargetDevice,
			(TargetDevice::Platform)fTargetPlatform,
			fTargetVersion,
			fTargetAppStoreName.GetString(),
			fCertType.GetString(),
			deviceId,
			fProductId.GetString(), // TODO: What is this?
			clientPlatformName );

		String debugBuildProcessPref;
		int debugBuildProcess = 0;
		services.GetPreference( "debugBuildProcess", &debugBuildProcessPref );

		if (! debugBuildProcessPref.IsEmpty())
		{
			debugBuildProcess = (int) strtol(debugBuildProcessPref.GetString(), (char **)NULL, 10);
		}
		else
		{
			debugBuildProcess = 0;
		}


		fDeviceBuildData->Initialize(
			appSettingsPath.GetString(),
			buildSettingsPath,
			fIncludeFusePlugins,
			fUsesMonetization,
			fLiveBuild,
			debugBuildProcess);
	}

	return * fDeviceBuildData;
}

void
AppPackagerParams::Print()
{
	fprintf( stderr,
		"Building app '%s'\n"
		"\tVersion '%s'\n"
		"\tPlatform '%s'\n"
		"\tPlatformVersion '%d'\n"
		"\tProject: '%s'\n"
		"\tDst: '%s'\n"
		"\tCustom build id: '%s'\n"
		"\tBuild type: '%s'\n",
		GetAppName(), 
		GetVersion(),
		TargetDevice::StringForPlatform( GetTargetPlatform() ),
		(GetTargetPlatform() == TargetDevice::kAndroidPlatform ? GetTargetVersion() - 100000: GetTargetVersion()),
		GetSrcDir(),
		GetDstDir(),
		GetCustomBuildId() ? GetCustomBuildId() : "none",
		GetCertType() );
}

void
AppPackagerParams::InitializeProductId( U32 modules )
{
	typedef enum
	{
		kMinimalProductType = 1,
		kDefaultProductType = 2,
		kAllProductType = 3
	}
	ProductType;

	ProductType product = kMinimalProductType;

	switch ( product )
	{
		case kMinimalProductType:
			fProductId.Set( "basic" );
			break;
		case kDefaultProductType:
			fProductId.Set( "basic" );
			break;
		case kAllProductType:
			fProductId.Set( "all" );
			break;
	}
}

// ----------------------------------------------------------------------------

PlatformAppPackager::PlatformAppPackager( const MPlatformServices& services,
										  TargetDevice::Platform targetPlatform )
:	fServices( services ),
	fTargetPlatform( targetPlatform ),
	fVM( Lua::New( true ) ),
	fCustomBuildId( & fServices.Platform().GetAllocator() ),
	fGlobalCustomBuildId( & fServices.Platform().GetAllocator() ),
	fAppSettingsCustomBuildId( & fServices.Platform().GetAllocator() ),
    fNeverStripDebugInfo( false ),
	fBuildCallbacksRef( LUA_NOREF )
{
	fCustomBuildId.Set( "" );
	fGlobalCustomBuildId.Set( "" );
	fAppSettingsCustomBuildId.Set( "" );
}

PlatformAppPackager::~PlatformAppPackager()
{
	Lua::Delete( fVM );
}

bool
PlatformAppPackager::mkdir( const char *sDir )
{
	bool hasSucceeded = false;

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
	WinString stringConverter;
	stringConverter.SetUTF8( sDir );
	int result = ::SHCreateDirectoryEx( NULL, stringConverter.GetTCHAR(), NULL );
	if (ERROR_SUCCESS == result)
	{
		hasSucceeded = true;
	}
#else
	const char kCmdFormat[] = "mkdir -p \"%s\"";
	char cmd[kDefaultNumBytes]; 
	Rtt_ASSERT( kDefaultNumBytes > ( sizeof( kCmdFormat ) + strlen( sDir ) ) );
	snprintf( cmd, kDefaultNumBytes, kCmdFormat, sDir );
    int result = system( cmd );
	if (0 == result)
	{
		hasSucceeded = true;
	}
#endif

    return hasSucceeded;
}

bool
PlatformAppPackager::rmdir( const char *sDir )
{
	int result;

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
	// Convert the given directory path to UTF-16.
	WinString directoryPath;
	directoryPath.SetUTF8(sDir);

	// Remove the read-only and hidden attributes from all files and subdirectories under the given directory.
	// We must do this first because the "rmdir" command line tool won't delete them.
	std::wstring utf16CommandLine(L"attrib -R -H \"");
	utf16CommandLine.append(directoryPath.GetUTF16());
	utf16CommandLine.append(L"\" /S /D");
#ifdef Rtt_NO_GUI
	result = WinConsolePlatform::RunSystemCommand(utf16CommandLine);
#else
	result = Interop::Ipc::CommandLine::RunShellCommandUntilExit(utf16CommandLine.c_str()).GetExitCode();
#endif
	// Delete the directory tree.
	utf16CommandLine = L"rmdir /s /q \"";
	utf16CommandLine.append(directoryPath.GetUTF16());
	utf16CommandLine.append(L"\"");
#ifdef Rtt_NO_GUI
	result = WinConsolePlatform::RunSystemCommand(utf16CommandLine);
#else
	result = Interop::Ipc::CommandLine::RunShellCommandUntilExit(utf16CommandLine.c_str()).GetExitCode();
#endif
#else
	char cmd[kDefaultNumBytes];
	snprintf( cmd, kDefaultNumBytes, "rm -rf \"%s\"", sDir );
    result = system( cmd );
#endif

	return (0 == result);
}

int
PlatformAppPackager::Build( AppPackagerParams * params, const char* tmpDirBase )
{
	return PlatformAppPackager::kBuildError;
}

bool
PlatformAppPackager::VerifyConfiguration() const
{
	// Add code to check existence of various utilities
	return true;
}

char* 
PlatformAppPackager::Prepackage( AppPackagerParams * params, const char* tmpDir )
{
	char* result = NULL;

	if (! Rtt_StringIsEmpty(GetSplashImageFile()))
	{
		// Note: this logic (copying the splash screen image to the root of the tmp directory) needs
		// to match the logic in buildsys-worker/tools/build3_output_ios.sh on the build server
		String splashImageFilename, tmpSplashPath, tmpFilename, tmpDstFilename;
		splashImageFilename.Set(params->GetSrcDir());
		splashImageFilename.AppendPathComponent(GetSplashImageFile());
		tmpSplashPath.Set(GetSplashImageFile());
		tmpFilename.Set(tmpSplashPath.GetLastPathComponent());
		tmpDstFilename.Set(tmpDir);
		tmpDstFilename.AppendPathComponent(tmpFilename);

		if (! Rtt_CopyFile( splashImageFilename, tmpDstFilename ))
		{
			String tmpString;
			tmpString.Set("ERROR: failed to copy splashScreen.image file: ");
			tmpString.Append(GetSplashImageFile());
			Rtt_LogException("%s", tmpString.GetString());
			params->SetBuildMessage( tmpString );

			return NULL;
		}
	}

	params->SetBuildCallbacksRef( fBuildCallbacksRef );

	// Build *.lu into tmpDir
	if ( CompileScripts( params, tmpDir ) )
	{
		// Compress files in tmpDir into a file kDstName and place it in tmpDir
		const char kDstName[] = "input.zip";
		size_t tmpDirLen = strlen( tmpDir );
		size_t dstFileLen = tmpDirLen + sizeof(kDstName) + sizeof( LUA_DIRSEP );
		char* dstFile = (char*)malloc( dstFileLen );
		snprintf( dstFile, dstFileLen, "%s" LUA_DIRSEP "%s", tmpDir, kDstName );

#ifdef Rtt_MAC_ENV
		const char kCmdFormat[] = "zip -q -rj \"%s\" \"%s\"";
		char* cmd = (char*)malloc( sizeof( kCmdFormat ) + dstFileLen + tmpDirLen );
		snprintf( cmd, (sizeof( kCmdFormat ) + dstFileLen + tmpDirLen), kCmdFormat, dstFile, tmpDir );

		if ( Rtt_VERIFY( 0 == system( cmd ) ) )
		{
			result = dstFile;
		}
		else
		{
			free( dstFile );
		}
		free( cmd );
#elif defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
		WinString dstFileconverter;
		dstFileconverter.SetUTF8( dstFile );

		WinString tmpDirConverter;
		tmpDirConverter.SetUTF8( tmpDir );

		CString cmdIssue;
		CString cmdPrepath = _wgetenv(_T("CORONA_PATH"));
		cmdIssue.Format(
				L"\"%s\\7za\" a -tzip \"%s\" \"%s\\*\"",
				cmdPrepath, dstFileconverter.GetTCHAR(), tmpDirConverter.GetTCHAR());
#ifdef Rtt_NO_GUI
		if (WinConsolePlatform::RunSystemCommand(cmdIssue.GetString()) == 0)
#else
		if ( Rtt_VERIFY( Interop::Ipc::CommandLine::RunUntilExit(cmdIssue).GetExitCode() == 0 ) )
#endif
		{
			result = dstFile;
		}
		else
		{
			free( dstFile );
		}
#elif defined(Rtt_LINUX_ENV)

#else
#		error This source file does not support the platform it was compiled on.
#endif
			
	}
	return result;
}

static bool
HasSuffix( const char *path, const char *suffix, size_t suffixLen )
{
	if ( 0 == suffixLen ) { suffixLen = strlen( suffix ); }

	size_t pathLen = strlen( path );

	bool result = pathLen >= suffixLen;
	if ( result )
	{
		path += pathLen - suffixLen;
		result = 0 == strncmp( path, suffix, suffixLen );
	}

	return result;
}

static void
ReplaceChar( char *str, int strlen, char oldChar, char newChar )
{
	for ( int i = 0; i < strlen; i++ )
	{
		if ( oldChar == str[i] )
		{
			str[i] = newChar;
		}
	}
}

static bool
IsDirectory( const char *path )
{
	Rtt_ASSERT( path );

	bool result = false;

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
	WinString stringConverter;
	stringConverter.SetUTF8( path );
	result = ::PathIsDirectoryW( stringConverter.GetUTF16() ) ? true : false;
#else
	DIR *subdp = opendir(path);
	if ( subdp )
	{
		closedir( subdp );
		subdp = NULL;
		result = true;
	}
#endif

	return result;
}

bool
CompileScriptsInDirectory( lua_State *L, AppPackagerParams& params, const char *dstDir, const char *srcDir )
{
	const char *baseDir = params.GetSrcDir(); // this is the project directory
	bool isDirectory = false;
	bool result = false;

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
	struct _stat statbuf;
	WinString stringConverter;
	stringConverter.SetUTF8( srcDir );
	if (_tstat(stringConverter.GetTCHAR(), &statbuf) >= 0)
	{
		isDirectory = Rtt_VERIFY( S_ISDIR( statbuf.st_mode ) ) ? true : false;
	}
#elif defined(Rtt_LINUX_ENV)
	 isDirectory = Rtt_IsDirectory(baseDir);
#else
	struct stat statbuf;
	isDirectory = ( stat( srcDir, &statbuf ) >= 0 && Rtt_VERIFY( S_ISDIR( statbuf.st_mode ) ) );
#endif

	if (isDirectory)
	{
		DIR *dp = opendir( srcDir );
		if ( Rtt_VERIFY( dp ) )
		{
			struct dirent *dirp = readdir( dp );
			if ( dirp )
			{
				const int kSrcDirLen = (int) strlen( srcDir );
				const int kBaseDirLen = (int) strlen( baseDir );

				// Check that baseDir is a prefix of srcDir
				Rtt_ASSERT( kBaseDirLen <= kSrcDirLen );
				Rtt_ASSERT( 0 == strncmp( srcDir, baseDir, kBaseDirLen ) );

				const int kSubModulePathLen = Max( 0, kSrcDirLen - kBaseDirLen );
				const int kDstDirLen = (int) strlen( dstDir );
				size_t srcPathSize = kSrcDirLen + sizeof( *dirp ) + sizeof( LUA_DIRSEP );
				size_t dstPathSize = kDstDirLen + sizeof( *dirp ) + sizeof( LUA_DIRSEP ) + kSubModulePathLen;
				char *srcPath = (char*)malloc( srcPathSize );
				char *dstPath = (char*)malloc( dstPathSize );

				// Relationship between directories: $srcDir = $baseDir/$subModulePath
				// Extract submodule path:
				const size_t kSubModulePathSize = kSubModulePathLen + 1;
				char *subModulePath = (char*)( kSubModulePathLen > 0 ? malloc( kSubModulePathSize ) : NULL );
				if ( subModulePath )
				{
					// Copy portion of srcPath, excluding the prefix baseDir (and the dir separator)
					int numChars = snprintf( subModulePath, kSubModulePathSize, "%s", srcDir + kBaseDirLen + strlen( LUA_DIRSEP ) );

					// Replace LUA_DIRSEP character with '.'
					const char *oldCharStr = LUA_DIRSEP;
					ReplaceChar( subModulePath, numChars, oldCharStr[0], '.' );
				}

				const char kScriptSuffix[] = "." Rtt_LUA_SCRIPT_FILE_EXTENSION;
				const size_t kScriptSuffixLen = sizeof( kScriptSuffix ) - 1;

				const char kObjectSuffix[] = "." Rtt_LUA_OBJECT_FILE_EXTENSION;
				const size_t kObjectSuffixSize = sizeof( kObjectSuffix );

				Rtt_STATIC_ASSERT( sizeof( Rtt_LUA_OBJECT_FILE_EXTENSION ) <= sizeof( Rtt_LUA_SCRIPT_FILE_EXTENSION ) );

				result = true;

				if ( PlatformAppPackager::PushBuildCallback( L, params.GetBuildCallbacksRef(), "visitProjectTree" ) )
				{
					lua_pushliteral( L, "enterDirectory" );
					lua_pushstring( L, srcDir );

					if ( 0 != Lua::DoCall( L, 2, 0 ) )
					{
						params.SetBuildMessage("Error with visitProjectTree(\"enterDirectory\") while doing CompileScripts().");
						result = false;
					}
				}

				for( ; dirp && result; dirp = readdir( dp ) )
				{
					// Fetch the next file/directory name.
					const char *filename = dirp->d_name;

					// Skip files/directories that start with a '.' period.
					// These are either hidden files, the "." current directory, or ".." up a directory.
					if ('.' == filename[0])
					{
						continue;
					}

					// Create a path to the file/directory.
					snprintf( srcPath, srcPathSize, "%s" LUA_DIRSEP "%s", srcDir, filename );

					// If the next item is a directory, then recursively compile the files under that directory.
					if ( IsDirectory( srcPath ) )
					{
						result = CompileScriptsInDirectory( L, params, dstDir, srcPath );
						continue;
					}

					// This is a file. Compile this file if:
					// - It is a *.lua file.
					// - It is a "build.settings" file and the package params have flagged it to be included.
					bool isLuaFile = HasSuffix( filename, kScriptSuffix, kScriptSuffixLen );
					bool isBuildSettingsFile = false;
					if ( !isLuaFile )
					{
						isBuildSettingsFile = (Rtt_StringCompare( filename, "build.settings" ) == 0);
					}
					if ( isLuaFile || ( isBuildSettingsFile && params.IncludeBuildSettings() ) )
					{
						// Create a destination file path for the resulting compiled file.
						int dstPathLen = 0;
						if ( subModulePath )
						{
							// Support for sub-modules (Lua files in subdirectories):
							// Create flattened filename for dst using subModulePath.
							// This effectively replaces LUA_DIRSEP with a '.' 
							// starting with the module root.
							// 
							// For example: if there is a submodule "a.b" 
							// (i.e. in Lua you have: require "a.b")
							// then the srcPath looks like $BASEDIR/a/b.lua
							// and you'd want to compile the file to a flattened 
							// byte-code file name: $DSTDIR/a.b.lu
							dstPathLen = snprintf(
									dstPath, dstPathSize, "%s" LUA_DIRSEP "%s.%s", dstDir, subModulePath, filename );
						}
						else
						{
							dstPathLen = snprintf( dstPath, dstPathSize, "%s" LUA_DIRSEP "%s", dstDir, filename );
						}
						Rtt_ASSERT( dstPathLen > 0 );

						// Replace the ".lua" extension with ".lu". See static assert above.
						// Note: We do not do this with the "build.settings" file.
						//       This prevents Lua's require() function from loading it.
						if ( isLuaFile )
						{
							strncpy( dstPath + ( dstPathLen - kScriptSuffixLen ), kObjectSuffix, kObjectSuffixSize );
						}
						
						// Compile the file.
						const char *sources = srcPath;
						int status = Rtt_LuaCompile( L, 1, & sources, dstPath, params.IsStripDebug() );
						result = Rtt_VERIFY( 0 == status ) ? true : false;
						if ( ! result )
						{
							String tmpString;
							tmpString.Set("ERROR: Could not complete build because there were compile errors in Lua file: ");
							tmpString.Append(srcPath);
							Rtt_TRACE_SIM( ("%s", tmpString.GetString()) );
							tmpString.Append("\n\nCheck Simulator console for error messages.");
							params.SetBuildMessage(tmpString.GetString());
						}
						else if ( PlatformAppPackager::PushBuildCallback( L, params.GetBuildCallbacksRef(), "visitProjectTree" ) )
						{
							lua_pushliteral( L, "compiledFile" );
							lua_pushstring( L, srcPath );
							lua_pushstring( L, dstPath );

							if ( 0 != Lua::DoCall( L, 3, 0 ) )
							{
								params.SetBuildMessage("Error with visitProjectTree(\"compiledFile\") while doing CompileScripts().");
								result = false;
							}
						}

#if defined(Rtt_WIN_ENV) && !defined( Rtt_NO_GUI ) && !defined(Rtt_LINUX_ENV)
						CSimulatorApp *pApp = ((CSimulatorApp *)AfxGetApp());
						if (pApp != NULL && pApp->IsStopBuildRequested())
						{
							// A request to stop the build was made while the Java was running
							result = false;

							params.SetBuildMessage("Build stopped");
						}
#endif


					}
					else if ( PlatformAppPackager::PushBuildCallback( L, params.GetBuildCallbacksRef(), "visitProjectTree" ) )
					{
						lua_pushliteral( L, "skippedFile" );
						lua_pushstring( L, srcPath );

						if ( 0 != Lua::DoCall( L, 2, 0 ) )
						{
							params.SetBuildMessage("Error with visitProjectTree(\"skippedFile\") while doing CompileScripts().");
							result = false;
						}
					}
				}

				if ( PlatformAppPackager::PushBuildCallback( L, params.GetBuildCallbacksRef(), "visitProjectTree" ) )
				{
					lua_pushliteral( L, "leaveDirectory" );

					if ( 0 != Lua::DoCall( L, 1, 0 ) )
					{
						params.SetBuildMessage("Error with visitProjectTree(\"leaveDirectory\") while doing CompileScripts().");
						result = false;
					}
				}

				free( subModulePath );
				free( dstPath );
				free( srcPath );
			}
		}

		closedir( dp );
	}
	else
	{
        // CoronaBuilder can end up here
        String tmpString;
        
        tmpString.Set("ERROR: Could not complete build because the source directory is inaccessible: ");
        tmpString.Append(srcDir);
        Rtt_TRACE_SIM( ("%s", tmpString.GetString()) );
        
        params.SetBuildMessage(tmpString.GetString());
    }

	return result;
}

static std::string GenerateUUID()
{
#ifdef Rtt_MAC_ENV
	uuid_t id;
	uuid_generate(id);
	char outc[100];
	uuid_unparse(id, outc);
	return std::string(outc);
#elif Rtt_WIN_ENV
	UUID uuid;
	UuidCreate(&uuid);
	char *str;
	UuidToStringA(&uuid, (RPC_CSTR*)&str);
	std::string ret(str);
	RpcStringFreeA((RPC_CSTR*)&str);
	return ret;
#elif Rtt_LINUX_ENV
// FIXME
//	uuid_t id;
//	uuid_generate(id);
//	char outc[100];
//	uuid_unparse(id, outc);
//	return std::string(outc);
	return "";
#else
	static_assert(0, "Fix me");
#endif
}

static void
trimString(std::string& s)
{
	size_t p = s.find_first_not_of(" \t");
	s.erase(0, p);

	p = s.find_last_not_of(" \t");
	if (std::string::npos != p)
		s.erase(p+1);
}


static bool
ReplaceMainLuaWithLiveDebug( lua_State *L, AppPackagerParams& params, const char *dstDir )
{
	if(!params.IsLiveBuild())
		return true;

	static const std::string sLuaNil = "nil";

	static const std::string sProjectKey = "key";
	static const std::string sAddress = "ip";
	static const std::string sPort = "port";


	bool res = true;

	std::string key(sLuaNil);
	std::string ip(sLuaNil);
	std::string port(sLuaNil);

	//read or create config
	bool createConfig = true;
	std::string configPath = std::string(params.GetSrcDir()) + LUA_DIRSEP + ".CoronaLiveBuild";
#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
	WinString transcodedConfigPath(configPath.c_str());
	std::ifstream fsconfig(transcodedConfigPath.GetUTF16(), std::ios_base::binary);
#else
	std::ifstream fsconfig(configPath);
#endif
	if(fsconfig.is_open())
	{
		for(std::string line; std::getline(fsconfig, line); )
		{
			std::istringstream iss(line);
			std::string k, v;
			iss>>std::ws;
			bool readRes = true; // inner res
			readRes = readRes && std::getline(iss, k, '=');
			iss>>std::ws;
			readRes = readRes && std::getline(iss, v);

			trimString(k);
			trimString(v);
			readRes = readRes && k.length() && v.length();

			if(!readRes)
			{
				continue;
			}
			else if(k == sPort)
			{
				long nPort = std::stol(v);
				if(nPort>0)
					port = v;
			}
			else if(k == sProjectKey)
			{
				key = "'" + v + "'";
			}
			else if(k == sAddress)
			{
				ip = "'" + v + "'";
			}
		}
		if (key == sLuaNil || key.length()<=2)
		{
			Rtt_LogException("ERROR: Invalid Live Build configuration. Re-generating .CoronaLiveBuild file\n");
		}
		else
		{
			createConfig = false;
		}
	}

	if(createConfig)
	{
#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
		std::ofstream out(transcodedConfigPath.GetUTF16(), std::ios_base::binary);
#else
		std::ofstream out(configPath);
#endif
		res = res && out.is_open();

		std::string generatedKey = GenerateUUID();

		res = res && (out<<sProjectKey<<" = "<<generatedKey<<std::endl);
		res = res && (out<<"#"<<sPort<<" ="<<std::endl);
		res = res && (out<<"#"<<sAddress<<" ="<<std::endl);
		out.flush();
		res = res && out.good();
		out.close();

		if(!res) {
			Rtt_LogException("ERROR: Unable to write .CoronaLiveBuild configuration file. Please make sure you have write access to the project directory.\n");
			return false;
		}

		key = "'" + generatedKey + "'";
	}


	static const char *kMainLua = "main.lua";
	size_t sz = strlen(dstDir) + strlen(kMainLua) + 5;
	char * mainLuaCounterfeit = new char[sz];
	snprintf(mainLuaCounterfeit, sz, "%s" LUA_DIRSEP "%s", dstDir, "main.lua" );

	FILE *f = fopen( mainLuaCounterfeit, "wb" );
	if ( Rtt_VERIFY( f ) )
	{
		// hack to prevent loading with Archive::ResourceLoader except if starting with "plugin."
		static const char * luaBody = "local a=package.loaders[1]; package.loaders[1]=function(p)return string.sub(p,1,7)=='plugin.'and a(p)or nil end;require('plugin.liveBuild').run{id=%s, key=%s, ip=%s, port=%s}";
		char timestamp[20];
		snprintf(timestamp, 20, "%li", (long)time(NULL));

		size_t mainBodyLen = strlen(luaBody) + strlen(timestamp) + key.length() + ip.length() + port.length() + 5;
		char *mainBody = new char[mainBodyLen];

		snprintf(mainBody, mainBodyLen, luaBody, timestamp, key.c_str(), ip.c_str(), port.c_str());
		int status = fputs(mainBody, f);
		res = (status >= 0);
		fclose( f );

		char * mainLuaCounterfeitCompiled = new char[sz];
		snprintf(mainLuaCounterfeitCompiled, sz, "%s" LUA_DIRSEP "%s", dstDir, "main.lu" );

		const char *source = mainLuaCounterfeit;
		if(res)
		{
			res = (0 == Rtt_LuaCompile( L, 1, &source, mainLuaCounterfeitCompiled, true ));
		}

		unlink(mainLuaCounterfeit);


		delete[] mainBody;
		delete[] mainLuaCounterfeitCompiled;

	}

	delete[] mainLuaCounterfeit;

	return res;
}

bool
PlatformAppPackager::CompileScripts( AppPackagerParams * params, const char* tmpDir, bool doPostCompile )
{
#if 0
	const char* srcDir = params->GetSrcDir();
	const char* dstDir = tmpDir;

	const char kMainScript[] = Rtt_LUA_SCRIPT_FILE( "main" );
	const char kMainObject[] = Rtt_LUA_OBJECT_FILE( "main" );
	size_t srcMainSize = strlen( srcDir ) + sizeof( kMainScript ) + sizeof( LUA_DIRSEP );
	size_t dstMainSize = strlen( dstDir ) + sizeof( kMainObject ) + sizeof( LUA_DIRSEP );
	char* srcMain = (char*)malloc( srcMainSize );
	char* dstMain = (char*)malloc( dstMainSize );

	snprintf( srcMain, srcMainSize, "%s" LUA_DIRSEP "%s", srcDir, kMainScript );
	snprintf( dstMain, dstMainSize, "%s" LUA_DIRSEP "%s", dstDir, kMainObject );

	const char* pSrcMain = srcMain;
	bool result = Rtt_VERIFY( Rtt_LuaCompile( fVM, 1, & pSrcMain, dstMain, stripDebug ) );

	free( dstMain );
	free( srcMain );
#else

	const char* baseDir = params->GetSrcDir();
	const char* dstDir = tmpDir;

    // If "neverStripDebugInfo" is set in the build.settings, turn off stripping no matter what
    // upper levels of the Simulator might have decided
    if (fNeverStripDebugInfo)
    {
        Rtt_LogException("Note: debug info is not being stripped from application (settings.build.neverStripDebugInfo = true)\n");

        params->SetStripDebug(false);
    }
    
	bool result = CompileScriptsInDirectory( fVM, * params, dstDir, baseDir );
#endif

	if(result)
	{
		if ( doPostCompile )
		{
			DoPostCompile( dstDir, params->IsStripDebug() );
		}

		result = ReplaceMainLuaWithLiveDebug(fVM, *params, dstDir);
	}

	return result;
}

/**
 * Fetches the paths of all files under the given directory and its subdirectories and copies these paths
 * to the given string collection.
 * @param directoryPath Path to a directory to fetch file paths from.
 * @param filePathCollection Reference to a string collection this function will add the fetched file paths to.
 * @return Returns true if the given directory and subdirectories were traversed successfully.
 *         Note that this does not necessarily mean any files were found.
 *
 *         Returns false if at least one directory or subdirectory could not be successfully accessed.
 */
bool
FetchDirectoryTreeFilePaths( const char* directoryPath, std::vector<std::string>& filePathCollection )
{
	// Validate arguments.
	if ( Rtt_StringIsEmpty( directoryPath ) )
	{
		Rtt_TRACE_SIM(( "FetchDirectoryTreeFilePaths() was given an invalid path." ));
		return false;
	}
	if ( !IsDirectory( directoryPath ) )
	{
		Rtt_TRACE_SIM(( "FetchDirectoryTreeFilePaths() was not given a directory path." ));
		return false;
	}

	// Open the given directory so that we can traverse its files down below.
	DIR *directoryPointer = opendir( directoryPath );
	if ( !directoryPointer )
	{
		Rtt_TRACE_SIM(( "FetchDirectoryTreeFilePaths() failed to open directory: %s", directoryPath ));
		return false;
	}

	// Fetch the paths of all files under the opened directory.
	// Warning: This loop will recursively call this function for all subdirectories.
	bool wasSuccessful = true;
	struct dirent* nextEntryPointer;
	for (nextEntryPointer = readdir( directoryPointer ); nextEntryPointer; nextEntryPointer = readdir( directoryPointer ))
	{
		// Validate next entry's file/directory name.
		if ( Rtt_StringIsEmpty( nextEntryPointer->d_name ) )
		{
			continue;
		}

		// Skip files/directories that are prefixed with a '.' character.
		// These are either hidden files or "."/".." directories.
		if ( '.' == nextEntryPointer->d_name[0] )
		{
			continue;
		}

		// Create a string storing the full path to the next entry in the directory.
		std::string nextEntryPath( directoryPath );
		char lastCharacter = nextEntryPath.at( nextEntryPath.length() - 1 );
#ifdef Rtt_WIN_ENV
		if (( lastCharacter != '\\' ) && ( lastCharacter != '/' ))
#else
		if ( lastCharacter != '/' )
#endif
		{
			nextEntryPath.append( LUA_DIRSEP );
		}
		nextEntryPath.append( nextEntryPointer->d_name );

#ifdef Rtt_WIN_ENV
		// Skip this entry if flagged as hidden by the file system. (We need a full path for this test.)
		// This is needed on Windows to skip files that don't start with a leading '.', such as a "Thumbs.db" file.
		if ( Rtt_FileIsHidden( nextEntryPath.c_str() ) )
		{
			continue;
		}
#endif

		// Determine if the next entry is a subdirectory or a file.
		if ( IsDirectory( nextEntryPath.c_str() ) )
		{
			// The next entry is a subdirectory. Recursively fetch its file paths.
			wasSuccessful = FetchDirectoryTreeFilePaths( nextEntryPath.c_str(), filePathCollection );
			if ( !wasSuccessful )
			{
				break;
			}
		}
		else
		{
			// The next entry is a file. Store its file path to the given collection.
			filePathCollection.push_back( nextEntryPath );
		}
	}

	// Close the opened directory.
	closedir( directoryPointer );

	// Returns true if this directory and its subdirectories were traversed successsfullly.
	return wasSuccessful;
}

bool
PlatformAppPackager::ArchiveDirectoryTree(
	AppPackagerParams* params, const char* sourceDirectoryPath, const char* destinationFilePath)
{
	// Validate arguments.
	if ( !params || Rtt_StringIsEmpty( sourceDirectoryPath ) || Rtt_StringIsEmpty( destinationFilePath ) )
	{
		return false;
	}

	// Fetch all file paths under the given source directory.
	std::vector<std::string> sourceFilePathCollection;
	bool wasSuccessful = FetchDirectoryTreeFilePaths( sourceDirectoryPath, sourceFilePathCollection );
	if ( !wasSuccessful || sourceFilePathCollection.empty() )
	{
		return false;
	}

	// Copy all file paths to a string array.
	const char** sourceFilePathArray = new const char*[sourceFilePathCollection.size()];
	for (int fileIndex = (int)sourceFilePathCollection.size() - 1; fileIndex >= 0; fileIndex--)
	{
		sourceFilePathArray[fileIndex] = sourceFilePathCollection.at(fileIndex).c_str();
	}

	// Create the "resource.car" archive file containing the files fetched up above.
	Archive::Serialize( destinationFilePath, (int)sourceFilePathCollection.size(), sourceFilePathArray );

	// Clean up memory allocated up above.
	delete[] sourceFilePathArray;

	// Return true if the archive file was successfully created.
	return fServices.Platform().FileExists( destinationFilePath );
}

bool
PlatformAppPackager::CopyDirectoryTree( const PlatformAppPackager::CopyDirectoryTreeSettings &settings )
{
	// Validate arguments.
	if ( !settings.ParamsPointer ||
	     Rtt_StringIsEmpty( settings.SourceDirectoryPath ) ||
	     Rtt_StringIsEmpty( settings.DestinationDirectoryPath ) )
	{
		return false;
	}
	if ( IsDirectory( settings.SourceDirectoryPath ) == false )
	{
		if ( Rtt_StringIsEmpty( settings.ParamsPointer->GetBuildMessage() ) )
		{
			std::string message( "Failed to copy directory tree. Given invalid path:\n   " );
			message.append( settings.SourceDirectoryPath );
			settings.ParamsPointer->SetBuildMessage( message.c_str() );
		}
		return false;
	}

	// Fetch all file paths under the given source directory.
	std::vector<std::string> sourceFilePathCollection;
	bool wasSuccessful = FetchDirectoryTreeFilePaths( settings.SourceDirectoryPath, sourceFilePathCollection );
	if ( !wasSuccessful )
	{
		if ( Rtt_StringIsEmpty( settings.ParamsPointer->GetBuildMessage() ) )
		{
			std::string message( "Failed to acquire files from directory:\n   " );
			message.append( settings.SourceDirectoryPath );
			settings.ParamsPointer->SetBuildMessage( message.c_str() );
		}
		return false;
	}

	// Create the destination directory if it doesn't already exist.
	if ( IsDirectory( settings.DestinationDirectoryPath ) == false )
	{
		wasSuccessful = mkdir( settings.DestinationDirectoryPath );
		if ( !wasSuccessful )
		{
			if ( Rtt_StringIsEmpty( settings.ParamsPointer->GetBuildMessage() ) )
			{
				std::string message( "Failed to create directory:\n   " );
				message.append( settings.DestinationDirectoryPath );
				settings.ParamsPointer->SetBuildMessage( message.c_str() );
			}
			return false;
		}
	}

	// Do not continue if there are no files to copy.
	if ( sourceFilePathCollection.empty() )
	{
		return true;
	}

#ifdef Rtt_WIN_ENV
	// Convert the file exclusion patterns to UTF-16 ahead of time for best performance.
	LightPtrArray<wchar_t> utf16ExcludeFilePatternArray( &fServices.Platform().GetAllocator() );
	if ( settings.ExcludeFilePatternArray && ( settings.ExcludeFilePatternArray->Length() > 0 ) )
	{
		for ( int index = settings.ExcludeFilePatternArray->Length() - 1; index >= 0; index-- )
		{
			auto utf8Pattern = (*settings.ExcludeFilePatternArray)[index];
			if ( Rtt_StringIsEmpty( utf8Pattern ) == false )
			{
				auto utf16Pattern = lua_create_utf16_string_from( utf8Pattern );
				if ( utf16Pattern )
				{
					// Replace the forward slashes '/' with backslashes '\'. We do this because the Win32
					// pattern match function below won't accept forward slashes as path separators.
					wchar_t* characterPointer;
					while ( characterPointer = wcschr( utf16Pattern, L'/' ) )
					{
						*characterPointer = L'\\';
					}

					// Add the updated UTF-16 pattern string to the collection.
					utf16ExcludeFilePatternArray.Append( utf16Pattern );
				}
			}
		}
	}
#endif

	// Copy files to the given destination directory.
	wasSuccessful = true;
	size_t sourceDirectoryPathLength = strlen( settings.SourceDirectoryPath );
	for ( auto iter = sourceFilePathCollection.begin(); iter != sourceFilePathCollection.end(); iter++ )
	{
		// Fetch the path to the next file to be copied.
		std::string& utf8SourceFilePath = (*iter);
		if ( Rtt_StringIsEmpty( utf8SourceFilePath.c_str() ) )
		{
			continue;
		}

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
		// Convert the path to UTF-16.
		wchar_t* utf16SourceFilePath = lua_create_utf16_string_from( utf8SourceFilePath.c_str() );
		if ( !utf16SourceFilePath )
		{
			std::string message( "Failed to convert the following file path to UTF-16:\n   " );
			message.append( utf8SourceFilePath.c_str() );
			settings.ParamsPointer->SetBuildMessage( message.c_str() );
			wasSuccessful = false;
			break;
		}
		size_t utf16SourceFilePathLength = wcslen( utf16SourceFilePath );

		// Determine if the file should be copied or not based on the given exclusion filters.
		// Note: The Win32 pattern matching function has no understanding of folders or subfolders.
		//       So, we have to do a pattern match per subfolder in order for "File*.extension" patterns to work.
		bool shouldCopyFile = true;
		for ( int index = utf16ExcludeFilePatternArray.Length() - 1; index >= 0; index-- )
		{
			bool wasMatchFound = false;
			auto utf16Pattern = utf16ExcludeFilePatternArray[index];
			int offset = 0;
			while ( offset < (int)utf16SourceFilePathLength )
			{
				wasMatchFound = ::PathMatchSpecW( utf16SourceFilePath + offset, utf16Pattern ) ? true : false;
				if ( wasMatchFound )
				{
					break;
				}
				wchar_t* characterPointer = wcschr( utf16SourceFilePath + offset, L'\\' );
				if ( !characterPointer )
				{
					break;
				}
				offset = ((characterPointer + 1) - utf16SourceFilePath);
			}
			if ( wasMatchFound )
			{
				shouldCopyFile = false;
				break;
			}
		}
#elif defined(Rtt_LINUX_ENV)
		bool shouldCopyFile = true;
#else
		// Determine if the file should be copied or not based on the given exclusion filters.
		bool shouldCopyFile = true;
		if ( settings.ExcludeFilePatternArray )
		{
			for ( int index = settings.ExcludeFilePatternArray->Length() - 1; index >= 0; index-- )
			{
				const char* utf8Pattern = (*settings.ExcludeFilePatternArray)[index];
				if ( Rtt_StringIsEmpty( utf8Pattern ) == false )
				{
					int matchResultCode = fnmatch( utf8Pattern, utf8SourceFilePath.c_str(), FNM_NOESCAPE | FNM_PATHNAME );
					if ( 0 == matchResultCode )
					{
						shouldCopyFile = false;
						break;
					}
				}
			}
		}
#endif

		// Copy the file if not to be excluded.
		if ( shouldCopyFile )
		{
			// Create the path to where the file should be copied to.
			std::string utf8DestinationFilePath( settings.DestinationDirectoryPath );
			utf8DestinationFilePath.append( utf8SourceFilePath.c_str() + sourceDirectoryPathLength );

#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
			// Convert the destination path to UTF-16.
			wchar_t* utf16DestinationFilePath = lua_create_utf16_string_from( utf8DestinationFilePath.c_str() );
			if ( utf16DestinationFilePath )
			{
				// Check the destintation.
				if ( ::PathFileExistsW( utf16DestinationFilePath ) == FALSE )
				{
					// Create the destination directory (and subdirectories) if it doesn't already exist.
					const size_t kMaxCharacters = 1024;
					wchar_t utf16DestinationDirectoryPath[kMaxCharacters];
					utf16DestinationDirectoryPath[0] = L'\0';
					wcscpy_s( utf16DestinationDirectoryPath, kMaxCharacters, utf16DestinationFilePath );
					auto hasDirectoryPath = ::PathRemoveFileSpecW( utf16DestinationDirectoryPath );
					if ( hasDirectoryPath && !::PathIsDirectoryW( utf16DestinationDirectoryPath ) )
					{
						::SHCreateDirectoryExW( nullptr, utf16DestinationDirectoryPath, nullptr );
					}
				}

				// Copy the file.
				// If a file with the same name already exists at the destination, then it will be overwritten.
				auto wasCopied = ::CopyFileW( utf16SourceFilePath, utf16DestinationFilePath, FALSE );
				if ( !wasCopied )
				{
					std::string message("Failed to copy file:\r\n   ");
					message.append( utf8SourceFilePath.c_str() + sourceDirectoryPathLength + 1 );
					message.append("\r\n\r\nTo destination directory:\r\n   ");
					message.append( settings.DestinationDirectoryPath );
					auto errorCode = ::GetLastError();
					if ( errorCode )
					{
						LPWSTR utf16Buffer = nullptr;
						::FormatMessageW(
								FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
								nullptr, errorCode,
								MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
								(LPWSTR)&utf16Buffer, 0, nullptr);
						if ( utf16Buffer )
						{
							WinString stringConverter;
							stringConverter.SetUTF16( utf16Buffer );
							message.append( "\r\n\r\nReason:\r\n   " );
							message.append( stringConverter.GetUTF8() );
							::LocalFree( utf16Buffer );
						}
					}
					settings.ParamsPointer->SetBuildMessage( message.c_str() );
					wasSuccessful = false;
				}
			}
			else
			{
				// Failed to convert the destination path to UTF-16.
				std::string message( "Failed to convert the following file path to UTF-16:\n   " );
				message.append( utf8DestinationFilePath.c_str() );
				settings.ParamsPointer->SetBuildMessage( message.c_str() );
				wasSuccessful = false;
			}
#else
			Rtt_ASSERT_NOT_IMPLEMENTED();
#endif
		}

		// Cleanup any resources allocated during this iteration.
#if defined(Rtt_WIN_ENV) && !defined(Rtt_LINUX_ENV)
		lua_destroy_utf16_string( utf16SourceFilePath );
#endif

		// Do not continue if an error occurred up above.
		if ( !wasSuccessful )
		{
			break;
		}
	}

	// Cleanup allocated resources.
#ifdef Rtt_WIN_ENV
	for ( int index = utf16ExcludeFilePatternArray.Length() - 1; index >= 0; index-- )
	{
		lua_destroy_utf16_string( utf16ExcludeFilePatternArray[index] );
	}
#endif

	// Returns true if all files were copied to the given destination successfully.
	return wasSuccessful;
}

#if !defined( Rtt_NO_GUI )
bool
PlatformAppPackager::UnzipPlugins( AppPackagerParams *params, Runtime *runtime, const char *destinationDirectoryPath )
{
	// Validate.
	if ( !params || !runtime || Rtt_StringIsEmpty( destinationDirectoryPath ) )
	{
		Rtt_ASSERT(0);
		return false;
	}

	// Fetch the runtime's Lua state.
	lua_State* luaStatePointer = runtime->VMContext().L();
	if ( !luaStatePointer )
	{
		Rtt_ASSERT(0);
		return false;
	}

	// Do not continue if the given Corona runtime does not require any plugins.
	if ( runtime->RequiresDownloadablePlugins() == false )
	{
		return true;
	}

	// Create the destination directory if it doesn't already exist.
	if ( IsDirectory( destinationDirectoryPath ) == false )
	{
		bool wasCreated = mkdir( destinationDirectoryPath );
		if ( !wasCreated )
		{
			if ( Rtt_StringIsEmpty( params->GetBuildMessage() ) )
			{
				std::string message( "Failed to create directory:\n   " );
				message.append( destinationDirectoryPath );
				params->SetBuildMessage( message.c_str() );
			}
			return false;
		}
	}

	// Request the runtime's "shell.lua" to unzip its plugins via a runtime event.
	Lua::NewEvent( luaStatePointer, "_internalRequestUnzipPlugins" );
	int luaTableIndex = lua_gettop( luaStatePointer );
	lua_pushstring( luaStatePointer, destinationDirectoryPath );
	lua_setfield( luaStatePointer, luaTableIndex, "destinationPath" );
	lua_pushstring( luaStatePointer,  TargetDevice::TagForPlatform(params->GetTargetPlatform()) );
	lua_setfield( luaStatePointer, -2, "platform" );
	Lua::DispatchRuntimeEvent( luaStatePointer, 1 );

	// Determine if the runtime unzipped the plugins successfully.
	// Returns a boolean type set to true if successful.
	// Returns a string if failed. The string details why it failed.
	bool wasSuccessful = false;
	if ( lua_isboolean( luaStatePointer, -1 ) )
	{
		wasSuccessful = lua_toboolean( luaStatePointer, -1 ) ? true : false;
	}
	if ( !wasSuccessful )
	{
		if ( lua_isstring( luaStatePointer, -1 ) )
		{
			params->SetBuildMessage( lua_tostring( luaStatePointer, -1 ) );
		}
		if ( Rtt_StringIsEmpty( params->GetBuildMessage() ) )
		{
			params->SetBuildMessage( "Failed to extract the project's plugins." );
		}
	}

	// Pop off the return values.
	lua_pop( luaStatePointer, 1 );

	// Returns true if all plugins were unzipped successfully. Returns false if not.
	return wasSuccessful;
}
#endif

int
PlatformAppPackager::OpenBuildSettings( const char * srcDir )
{
    // Get global settings which might override build.settings.
	ReadGlobalCustomBuildId();

	int status = LUA_ERRFILE; // assume file does not exist
	
	Rtt_ASSERT( 0 == lua_gettop( (lua_State *) fVM ) );
	
	const char kConfig[] = "build.settings";
	
	String filePath( & fServices.Platform().GetAllocator() );
	
	filePath.Set( srcDir );
	filePath.Append( LUA_DIRSEP );
	filePath.Append( kConfig );

	const char * path = filePath.GetString();
	if ( !fServices.Platform().FileExists( path ) )
	{
		path = NULL;
	}
	
	if ( path )
	{
		status = Lua::DoFile( fVM, path, 0, true, &fErrorMesg );
	}
	
	return status;
}

/*
The following bytecode is adapted from `require` in init.lua (sans comments):

return function(settings)
	local preservedRequire = require
	local plugins = (settings and settings.plugins) or {}
	require = function (modname)
		if string.find(modname, "/") then
			error("Error calling 'require(\"" .. modname .. "\")'. Lua requires package names to use '.' as path separators, not '/'. Replace the '/' characters with '.' and try again.")
		elseif ( "simulator" == system.getInfo( "environment" ) ) or ( "win32" == system.getInfo( "platform" ) ) or ( "macos" == system.getInfo( "platform" ) ) then
			local prefix = "plugin."
			if ( string.sub( modname, 1, string.len( prefix ) ) == prefix )
				or ( nil ~= string.match( modname, 'CoronaProvider%.(.*)%.(.*)' ) ) then
				if ( "simulator" == system.getInfo( "environment" )
					and not string.starts(modname, "CoronaProvider.") ) then
					if plugins[modname] == nil then
						local guardedRequiredName = modname .. '.'
						local submodule = false
						for key, _ in pairs(plugins) do
							local guardedSettingsName = key .. "."
							if guardedRequiredName:starts(guardedSettingsName) or guardedSettingsName:starts(guardedRequiredName) then
								submodule = true
								break
							end
						end
						if not submodule then
							local output = "WARNING: "..modname.." is not configured in build.settings"
							output = output .. "\nstack traceback:\n"
							local stackdesc = debug.traceback()
							for line in stackdesc:gmatch("[^\r\n]+") do 
								if string.ends(line, "in main chunk") then
									output = output .. line .. "\n"
								end
							end
							print(output)
						end
					end
				end
				local result, mod = pcall( preservedRequire, modname )
				if ( result ) then
					return mod -- traditional require works fine
				elseif ( "string" == type( mod ) ) then
					if string.starts( mod, "error loading module" ) then
						error( mod, 2 )
					end
				end
				modname = string.gsub( modname, '%.', '_' )
			end
		end
		return preservedRequire( modname )
	end
end

An alternate approach would be to have this in a Lua file in the build process
and then used by (the compiled) init.lua.
*/
static const U8 kRequireWithSettings[] = {
 27, 76,117, 97, 81,  0,  1,  4,  4,  4,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  2,  2,  3,  0,  0,  0, 36,  0,  0,  0, 30,  0,  0,  1,
 30,  0,128,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,
 71,  0,  0,  0,  0,  1,  0,  4, 12,  0,  0,  0, 69,  0,  0,  0, 26,  0,  0,  0,
 22,128,  0,128,134, 64, 64,  0,154, 64,  0,  0, 22,  0,  0,128,138,  0,  0,  0,
228,  0,  0,  0,  0,  0,  0,  1,  0,  0,128,  0,199,  0,  0,  0, 30,  0,128,  0,
  2,  0,  0,  0,  4,  8,  0,  0,  0,114,101,113,117,105,114,101,  0,  4,  8,  0,
  0,  0,112,108,117,103,105,110,115,  0,  1,  0,  0,  0,  0,  0,  0,  0,  4,  0,
  0,  0, 70,  0,  0,  0,  2,  1,  0, 13,161,  0,  0,  0, 69,  0,  0,  0, 70, 64,
192,  0,128,  0,  0,  0,193,128,  0,  0, 92,128,128,  1, 90,  0,  0,  0, 22,128,
  1,128, 69,192,  0,  0,129,  0,  1,  0,192,  0,  0,  0,  1, 65,  1,  0,149,  0,
  1,  1, 92, 64,  0,  1, 22, 64, 35,128, 69,192,  1,  0, 70,  0,194,  0,129, 64,
  2,  0, 92,128,  0,  1, 87, 64,  0,131, 22,192,  2,128, 69,192,  1,  0, 70,  0,
194,  0,129,192,  2,  0, 92,128,  0,  1, 87, 64,  0,133, 22, 64,  1,128, 69,192,
  1,  0, 70,  0,194,  0,129,192,  2,  0, 92,128,  0,  1, 23, 64,  0,134, 22,192,
 30,128, 65, 64,  3,  0,133,  0,  0,  0,134,128, 67,  1,192,  0,  0,  0,  1,193,
  3,  0, 69,  1,  0,  0, 70,  1,196,  2,128,  1,128,  0, 92,  1,  0,  1,156,128,
  0,  0, 87, 64,  0,  1, 22,128,  1,128,133,  0,  0,  0,134,128, 68,  1,192,  0,
  0,  0,  1,193,  4,  0,156,128,128,  1, 87,128,128,136, 22,  0, 26,128,133,192,
  1,  0,134,  0, 66,  1,193, 64,  2,  0,156,128,  0,  1, 23,128,  0,131, 22,192,
 16,128,133,  0,  0,  0,134,  0, 69,  1,192,  0,  0,  0,  1, 65,  5,  0,156,128,
128,  1,154, 64,  0,  0, 22,  0, 15,128,132,  0,  0,  0,134,  0,  0,  1, 23, 64,
 68,  1, 22,  0, 14,128,128,  0,  0,  0,193,128,  5,  0,149,192,  0,  1,194,  0,
  0,  0,  5,193,  5,  0, 68,  1,  0,  0, 28,  1,  1,  1, 22,128,  3,128, 64,  2,
128,  3,129,130,  5,  0, 85,130,130,  4,139,  2, 69,  1,  0,  3,128,  4,156,130,
128,  1,154, 66,  0,  0, 22,  0,  1,128,139,  2,197,  4,  0,  3,  0,  1,156,130,
128,  1,154,  2,  0,  0, 22, 64,  0,128,194,  0,128,  0, 22, 64,  0,128, 33,129,
  0,  0, 22,128,251,127,218, 64,  0,  0, 22, 64,  7,128,  1,  1,  6,  0, 64,  1,
  0,  0,129, 65,  6,  0, 21,129,  1,  2, 64,  1,  0,  2,129,129,  6,  0, 21,129,
129,  2, 69,193,  6,  0, 70,  1,199,  2, 92,129,128,  0,139, 65,199,  2,  1,130,
  7,  0,156,  1,129,  1, 22,128,  2,128,133,  2,  0,  0,134,194, 71,  5,192,  2,
128,  4,  1,  3,  8,  0,156,130,128,  1,154,  2,  0,  0, 22,192,  0,128,128,  2,
  0,  2,192,  2,128,  4,  1, 67,  8,  0, 21,  1,  3,  5,161, 65,  0,  0, 22,128,
252,127,133,129,  8,  0,192,  1,  0,  2,156, 65,  0,  1,133,192,  8,  0,196,  0,
128,  0,  0,  1,  0,  0,156,192,128,  1,154,  0,  0,  0, 22, 64,  0,128,222,  0,
  0,  1, 22,192,  3,128,  5,  1,  9,  0, 64,  1,128,  1, 28,129,  0,  1, 23,  0,
  1,128, 22,128,  2,128,  5,  1,  0,  0,  6,  1, 69,  2, 64,  1,128,  1,129, 65,
  9,  0, 28,129,128,  1, 26,  1,  0,  0, 22,192,  0,128,  5,193,  0,  0, 64,  1,
128,  1,129,129,  9,  0, 28, 65,128,  1,  5,  1,  0,  0,  6,193, 73,  2, 64,  1,
  0,  0,129,  1, 10,  0,193, 65, 10,  0, 28,129,  0,  2,  0,  0,  0,  2, 68,  0,
128,  0,128,  0,  0,  0, 93,  0,  0,  1, 94,  0,  0,  0, 30,  0,128,  0, 42,  0,
  0,  0,  4,  7,  0,  0,  0,115,116,114,105,110,103,  0,  4,  5,  0,  0,  0,102,
105,110,100,  0,  4,  2,  0,  0,  0, 47,  0,  4,  6,  0,  0,  0,101,114,114,111,
114,  0,  4, 25,  0,  0,  0, 69,114,114,111,114, 32, 99, 97,108,108,105,110,103,
 32, 39,114,101,113,117,105,114,101, 40, 34,  0,  4,123,  0,  0,  0, 34, 41, 39,
 46, 32, 76,117, 97, 32,114,101,113,117,105,114,101,115, 32,112, 97, 99,107, 97,
103,101, 32,110, 97,109,101,115, 32,116,111, 32,117,115,101, 32, 39, 46, 39, 32,
 97,115, 32,112, 97,116,104, 32,115,101,112, 97,114, 97,116,111,114,115, 44, 32,
110,111,116, 32, 39, 47, 39, 46, 32, 82,101,112,108, 97, 99,101, 32,116,104,101,
 32, 39, 47, 39, 32, 99,104, 97,114, 97, 99,116,101,114,115, 32,119,105,116,104,
 32, 39, 46, 39, 32, 97,110,100, 32,116,114,121, 32, 97,103, 97,105,110, 46,  0,
  4, 10,  0,  0,  0,115,105,109,117,108, 97,116,111,114,  0,  4,  7,  0,  0,  0,
115,121,115,116,101,109,  0,  4,  8,  0,  0,  0,103,101,116, 73,110,102,111,  0,
  4, 12,  0,  0,  0,101,110,118,105,114,111,110,109,101,110,116,  0,  4,  6,  0,
  0,  0,119,105,110, 51, 50,  0,  4,  9,  0,  0,  0,112,108, 97,116,102,111,114,
109,  0,  4,  6,  0,  0,  0,109, 97, 99,111,115,  0,  4,  8,  0,  0,  0,112,108,
117,103,105,110, 46,  0,  4,  4,  0,  0,  0,115,117, 98,  0,  3,  0,  0,  0,  0,
  0,  0,240, 63,  4,  4,  0,  0,  0,108,101,110,  0,  0,  4,  6,  0,  0,  0,109,
 97,116, 99,104,  0,  4, 27,  0,  0,  0, 67,111,114,111,110, 97, 80,114,111,118,
105,100,101,114, 37, 46, 40, 46, 42, 41, 37, 46, 40, 46, 42, 41,  0,  4,  7,  0,
  0,  0,115,116, 97,114,116,115,  0,  4, 16,  0,  0,  0, 67,111,114,111,110, 97,
 80,114,111,118,105,100,101,114, 46,  0,  4,  2,  0,  0,  0, 46,  0,  4,  6,  0,
  0,  0,112, 97,105,114,115,  0,  4, 10,  0,  0,  0, 87, 65, 82, 78, 73, 78, 71,
 58, 32,  0,  4, 37,  0,  0,  0, 32,105,115, 32,110,111,116, 32, 99,111,110,102,
105,103,117,114,101,100, 32,105,110, 32, 98,117,105,108,100, 46,115,101,116,116,
105,110,103,115,  0,  4, 19,  0,  0,  0, 10,115,116, 97, 99,107, 32,116,114, 97,
 99,101, 98, 97, 99,107, 58, 10,  0,  4,  6,  0,  0,  0,100,101, 98,117,103,  0,
  4, 10,  0,  0,  0,116,114, 97, 99,101, 98, 97, 99,107,  0,  4,  7,  0,  0,  0,
103,109, 97,116, 99,104,  0,  4,  7,  0,  0,  0, 91, 94, 13, 10, 93, 43,  0,  4,
  5,  0,  0,  0,101,110,100,115,  0,  4, 14,  0,  0,  0,105,110, 32,109, 97,105,
110, 32, 99,104,117,110,107,  0,  4,  2,  0,  0,  0, 10,  0,  4,  6,  0,  0,  0,
112,114,105,110,116,  0,  4,  6,  0,  0,  0,112, 99, 97,108,108,  0,  4,  5,  0,
  0,  0,116,121,112,101,  0,  4, 21,  0,  0,  0,101,114,114,111,114, 32,108,111,
 97,100,105,110,103, 32,109,111,100,117,108,101,  0,  3,  0,  0,  0,  0,  0,  0,
  0, 64,  4,  5,  0,  0,  0,103,115,117, 98,  0,  4,  3,  0,  0,  0, 37, 46,  0,
  4,  2,  0,  0,  0, 95,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0
};


static int
luaload_require_with_settings(lua_State *L)
{
	return luaL_loadbuffer(L,(const char*)kRequireWithSettings,sizeof(kRequireWithSettings),"require_with_settings");
}

static bool
InitLuaForBuild( lua_State *L, const MPlatform &platform )
{
	int top = lua_gettop( L ), ref;

	if ( top > 0 ) // InitializeLuaPath() expects empty stack
	{
		lua_createtable( L, top, 0 );

		for ( int i = 1; i <= top; i++ )
		{
			lua_pushvalue( L, i );
			lua_rawseti( L, -2, i );
		}

		ref = luaL_ref( L, LUA_REGISTRYINDEX );

		lua_settop( L, 0 );
	}

#if !defined(Rtt_NO_GUI)
	lua_pushcfunction( L, luaopen_coronabaselib );
	lua_pushstring( L, "coronabaselib" );
	lua_call( L, 1, 0 );
	lua_getglobal( L, "coronabaselib" );

	Rtt_ASSERT( !lua_isnil( L, -1 ) );
	
	lua_getfield( L, -1, "print" );
	
	Rtt_ASSERT( lua_isfunction( L, -1 ) );
	
	lua_setglobal( L, "print" );
	lua_pop( L, 1 );
#endif

	lua_pushcfunction( L, luaopen_lfs );
	lua_pushstring( L, "lfs" );
	lua_call( L, 1, 0 );

	Lua::RegisterModuleLoader( L, "json", Lua::Open< luaload_json > );

	Lua::InitializeLuaPath( L, platform );
	
	if ( top > 0 ) // restore stack
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, ref );
		
		Rtt_ASSERT( !lua_isnil( L, -1 ) );

		for ( int i = 1; i <= top; i++ )
		{
			lua_rawgeti( L, 1, i );
		}

		lua_remove( L, 1 );
		luaL_unref( L, LUA_REGISTRYINDEX, ref );
	}

	return 0 == Lua::DoBuffer( L, &luaload_require_with_settings, NULL ); // TODO: error message?
}

static void
AddToRegistryOrPop( lua_State *L, const char* key )
{
	if ( lua_isstring( L, -1 ) )
	{
		lua_setfield( L, LUA_REGISTRYINDEX, key );
	}
	else
	{
		lua_pop( L, 1 );
	}
}

static const char kBuildSettingsStartFuncCode[] = "CoronaBuildSettingsStartFuncCode";
static const char kBuildSettingsStartScriptName[] = "CoronaBuildSettingsStartScriptName";
static const char kBuildSettingsBuildScriptName[] = "CoronaBuildSettingsBuildScriptName";

bool
PlatformAppPackager::ReadBuildSettings( const char * srcDir )
{
	lua_State *L = fVM;
	
	int status = OpenBuildSettings( srcDir );
	bool retflag = true;
	
	if ( 0 == status )
	{
		const char kSettings[] = "settings";
		lua_getglobal( L, kSettings ); // settings
		
		if ( lua_istable( L, -1 ) )
		{
			OnReadingBuildSettings( L, lua_gettop( L ) );
			lua_getfield( L, -1, "build" ); // settings.build
			if ( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, "custom" );
				if ( lua_isstring( L, -1 ) )
				{
					const char * customBuildId = lua_tostring( L, -1 );
					fCustomBuildId.Set( customBuildId );
				}
				lua_pop( L, 1 );
                
				lua_getfield( L, -1, "neverStripDebugInfo" );
				if ( lua_isboolean( L, -1 ) )
				{
					fNeverStripDebugInfo = lua_toboolean( L, 1 ) ? true : false;
				}
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 ); // pop settings.build

			lua_getfield( L, -1, "splashScreen" ); // settings.splashScreen
			if ( lua_istable( L, -1 ) )
			{
				int splashEnabled = false;

				lua_getfield( L, -1, "enable" ); // push settings.splashScreen.enable
				if ( lua_isboolean( L, -1 ) )
				{
					splashEnabled = lua_toboolean( L, -1 );
				}
				lua_pop( L, 1 ); // pop settings.splashScreen.enable

				lua_getfield( L, -1, "image" ); // push settings.splashScreen.image
				if ( lua_isstring( L, -1 ) )
				{
					const char *splashImageFile = lua_tostring( L, -1 );
					SetSplashImageFile(splashImageFile);
					Rtt_TRACE(("Custom splashScreen.image: %s", splashImageFile));
				}
				lua_pop( L, 1 ); // pop settings.splashScreen.image

				// Now check for platform specific splash screen settings which
				// will override the non-specific ones handled above
				const char *platformTag = TargetDevice::TagForPlatform( fTargetPlatform );

				lua_getfield( L, -1, platformTag ); // push settings.splashScreen.{platform}
				if ( lua_istable( L, -1 ) )
				{
					lua_getfield( L, -1, "enable" ); // push settings.splashScreen.enable
					if ( lua_isboolean( L, -1 ) )
					{
						splashEnabled = lua_toboolean( L, -1 );

						if (! splashEnabled )
						{
							// They've explicitly disabled for this platform
							SetSplashImageFile(NULL);
						}
					}
					lua_pop( L, 1 ); // pop settings.splashScreen.enable

					if (splashEnabled)
					{
						lua_getfield( L, -1, "image" ); // push settings.splashScreen.image
						if ( lua_isstring( L, -1 ) )
						{
							const char *splashImageFile = lua_tostring( L, -1 );
							SetSplashImageFile(splashImageFile);
							Rtt_TRACE(("Custom splashScreen.image: %s", splashImageFile));
						}
						lua_pop( L, 1 ); // pop settings.splashScreen.image
					}
				}
				lua_pop( L, 1 ); // pop settings.splashScreen.{platform}
			}
			lua_pop( L, 1 ); // pop settings.splashScreen

			lua_getfield( L, -1, "callbacks" ); // push settings.callbacks
			if ( lua_istable( L, -1 ) && LUA_NOREF == fBuildCallbacksRef )
			{
				// prevent multiple loads
				lua_pushnil( L );
				fBuildCallbacksRef = luaL_ref( L, LUA_REGISTRYINDEX );
				
				int top = lua_gettop( L );
				bool startOK = Lua::LoadFuncOrFilename( srcDir, L, "start" ); // push settings.callbacks.start or nil

				if ( startOK )
				{
					AddToRegistryOrPop( L, kBuildSettingsStartFuncCode );
					lua_getfield( L, -1, "start" ); // was it a filename?
					AddToRegistryOrPop( L, kBuildSettingsStartScriptName );
				}

				bool buildOK = startOK && Lua::LoadFuncOrFilename( srcDir, L, "build", true ); // push settings.callbacks.build or nil

				if ( buildOK && lua_isfunction( L, -1 ) )
				{
					lua_getfield( L, -2, "build" ); // was it a filename?
					AddToRegistryOrPop( L, kBuildSettingsBuildScriptName );

					if ( !InitLuaForBuild( L, fServices.Platform() ) )
					{
						retflag = false;
					}
					else if ( 0 == Lua::DoCall( L, 0, 1 ) )
					{
						if ( lua_istable( L, -1 ) )
						{
							lua_getfield( L, -1, "close" ); // push close or nil
							if ( lua_isfunction( L, -1 ) )
							{
								lua_newuserdata( L, 0 );
								lua_createtable( L, 0, 1 );
								lua_pushvalue( L, -3 );
								lua_setfield( L, -2, "__gc" ); // TODO: what if build fails?
								lua_setmetatable( L, -2 );
								luaL_ref( L, LUA_REGISTRYINDEX ); // keep until state closed
							}
							lua_pop( L, 1 ); // pop close

							fBuildCallbacksRef = luaL_ref( L, LUA_REGISTRYINDEX );
						}
					}
					else
					{
						retflag = false;
					}
				}
				lua_settop( L, top ); // pop settings.callbacks.build
			}

			lua_pop( L, 1 ); // pop 
		}
		lua_pop( L, 1 ); // pop settings
		
		// set global table "settings" to nil
		lua_pushnil( L );
		lua_setglobal( L, kSettings );
	}
	// If the build.settings file doesn't exist, we consider that a success because the file is optional.
	// But if there is a real Lua error, we need to return a failure.
	else if ( LUA_ERRFILE != status )
	{
		retflag = false;
	}
	
	Rtt_ASSERT( 0 == lua_gettop( L ) );

	return retflag;
}

/// Called when the "build.settings" file is being read.
/// @param L Pointer to the Lua state that has loaded the build settings table.
/// @param index The index to the "settings" table in the Lua stack.
void
PlatformAppPackager::OnReadingBuildSettings( lua_State *L, int index )
{
}

// Look for customBuildId, in priority order:
//    build.settings             fCustomBuildId
//    Preferences (or Registry)  fGlobalCustomBuildId
//    resources/AppSettings.lua  fAppSettingsCustomBuildId
const char * PlatformAppPackager::GetCustomBuildId() const
{
	const String *pCustomBuildId = &fCustomBuildId;  // base case

	static String customDailyBuild;
	fServices.GetPreference( kUserPreferenceCustomDailyBuild, &customDailyBuild );

	if( ! fCustomBuildId.IsEmpty() )
	{
		Rtt_LogException("WARNING: Using custom build id from build.settings: %s", fCustomBuildId.GetString());
		pCustomBuildId = &fCustomBuildId;
	}
	else if( ! fGlobalCustomBuildId.IsEmpty() && fGlobalCustomBuildId.GetLength() == 32 )
	{
		Rtt_LogException("WARNING: Using custom build id from user preferences: %s (%s)", fGlobalCustomBuildId.GetString(), kUserPreferenceCustomBuildID);
		pCustomBuildId = &fGlobalCustomBuildId;
	}
	else if( ! customDailyBuild.IsEmpty() )
	{
		// Turn a string like "2016.2855" into an MD5 as that's what the build server expects
		const MCrypto& crypto = fServices.Platform().GetCrypto();

		U8 digest[MCrypto::kMaxDigestSize];
		size_t digestLen = crypto.GetDigestLength( MCrypto::kMD5Algorithm );

		Rtt::Data<const char> data( customDailyBuild.GetString(), (int) strlen(customDailyBuild.GetString()) );

		crypto.CalculateDigest( MCrypto::kMD5Algorithm, data, digest );

		char *hex = (char*)calloc( sizeof( char ), digestLen*2 + 1 );
		for ( size_t i = 0; i < digestLen; i++ )
		{
			sprintf( hex + 2*i, "%02x", digest[i] );
		}

		Rtt_LogException("WARNING: Using custom daily build from user preferences: %s - %s (%s)", customDailyBuild.GetString(), hex,kUserPreferenceCustomDailyBuild);

		customDailyBuild.Set(hex);
		free( hex );

		pCustomBuildId = &customDailyBuild;
	}
	else if( ! fAppSettingsCustomBuildId.IsEmpty() )
	{
		Rtt_LogException("Using custom build id from app bundle: %s (%s)", fAppSettingsCustomBuildId.GetString(), kAppSettingsLuaFile);
		pCustomBuildId = &fAppSettingsCustomBuildId;
	}

	return pCustomBuildId->GetString();
}

// Called from ReadBuildSettings to get alternate sources of CustomBuildIds
void PlatformAppPackager::ReadGlobalCustomBuildId()
{
    // look in preferences
	fServices.GetPreference( kUserPreferenceCustomBuildID, &fGlobalCustomBuildId );
	// look in AppSettings.lua
	ReadAppSetting( kCustomId, &fAppSettingsCustomBuildId );
}

// TODO: probably move this to a different class, since it is general
// Read requested setting from resources/AppSettings.lua and set result
void PlatformAppPackager::ReadAppSetting( const char *setting, String *result )
{
    if( NULL != result )
	{
		// NOTE: Given enough settings, we might want to leave the Lua state resident instead of reopening/closing.
		String filePath( & fServices.Platform().GetAllocator() );
		fServices.Platform().PathForFile( kAppSettingsLuaFile, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, filePath );

		if ( ! filePath.GetString() )
		{
			return;
		}
		
		lua_State* L = luaL_newstate();
		int error = luaL_loadfile( L, filePath.GetString() ) || lua_pcall(L, 0, 0, 0);
		if(error)
		{
			printf( "Failed to open file: %s, with error: %s\n", filePath.GetString(), lua_tostring(L, -1) );
			lua_close( L );
			return;
		}
		
		lua_getglobal( L, kCustomId );
		if( lua_isstring( L, -1 ) )
		{
			result->Set( lua_tostring( L, -1 ) );
		}
		lua_pop( L, 1 );

		lua_close( L );
	}
}

// Cheap test for whether or not this is a daily build (daily builds will have a non-empty AppSettings.lua file
bool
PlatformAppPackager::IsAppSettingsEmpty( const MPlatform& platform )
{
	bool result = false;

	String filePath( & platform.GetAllocator() );
	platform.PathForFile( kAppSettingsLuaFile, MPlatform::kSystemResourceDir, MPlatform::kDefaultPathFlags, filePath );

	FILE *f = fopen( filePath.GetString(), "r" );
	if ( Rtt_VERIFY( f ) )
	{
		fseek( f, 0, SEEK_END );

		result = ( 0 == ftell( f ) );
		fclose( f );
	}

	return result;
}

bool
PlatformAppPackager::PushBuildCallback( lua_State *L, int ref, const char* name )
{
	if ( LUA_NOREF != ref && LUA_REFNIL != ref )
	{
		lua_rawgeti( L, LUA_REGISTRYINDEX, ref );

		Rtt_ASSERT( lua_istable( L, -1 ) );

		lua_getfield( L, -1, name );
		lua_remove( L, -2 );

		return true;
	}
	else
	{
		return false;
	}
}

bool
PlatformAppPackager::PrepareToCompile( AppPackagerParams &params, const void *extra )
{
	if ( PushBuildCallback( fVM, fBuildCallbacksRef, "prepareToCompile" ) )
	{
		lua_newtable( fVM );

		int top = lua_gettop( fVM );

		lua_pushliteral( fVM, LUA_DIRSEP );
		lua_setfield( fVM, -2, "separator" );

		AddToCompileArgsTable( extra );

		Rtt_ASSERT( lua_gettop( fVM ) == top );

		if ( 0 != Lua::DoCall( fVM, 1, 0 ) )
		{
			params.SetBuildMessage("Error with prepareToCompile while doing DoLocalBuild().");
			return false;
		}
	}
	return true;
}

bool
PlatformAppPackager::ReadyToArchive( AppPackagerParams &params )
{
	if ( PushBuildCallback( fVM, fBuildCallbacksRef, "readyToArchive" ) )
	{
		if ( 0 != Lua::DoCall( fVM, 1, 0 ) )
		{
			params.SetBuildMessage("Error with readyToArchive while doing DoLocalBuild().");
			return false;
		}
	}
	return true;
}

static
void AddFile( lua_State *L, const char *key, const char *root, const char *dstDir, int stripDebug )
{
	const char kScriptSuffix[] = "." Rtt_LUA_SCRIPT_FILE_EXTENSION;
	const size_t kScriptSuffixLen = sizeof( kScriptSuffix ) - 1;

	lua_getfield( L, LUA_REGISTRYINDEX, key );
	if ( lua_isstring( L, -1 ) )
	{
		const char *code = lua_tostring( L, -1 );
		size_t codeLength = lua_objlen( L, -1 );

		lua_pushfstring( L, "%s" LUA_DIRSEP "%s." Rtt_LUA_SCRIPT_FILE_EXTENSION, dstDir, root );

		const char *scriptName = lua_tostring( L, -1 );

		FILE *fp = fopen( scriptName, "wb" );
		if ( Rtt_VERIFY( fp ) )
		{
			fwrite( code, 1, codeLength, fp );
			fclose( fp );
		}

		lua_pushfstring( L, "%s" LUA_DIRSEP "%s." Rtt_LUA_OBJECT_FILE_EXTENSION, dstDir, root );

		const char *compiledName = lua_tostring( L, -1 );

		int status = Rtt_LuaCompile( L, 1, &scriptName, compiledName, stripDebug );

		if ( !Rtt_VERIFY( 0 == status ) )
		{
			// TODO: error?
		}

		unlink( scriptName );

		lua_pop( L, 2 );
	}
	lua_pop( L, 1 );
}

static
void RemoveFiles( lua_State *L, const char *key, const char *dstDir )
{
	const char kScriptSuffix[] = "." Rtt_LUA_SCRIPT_FILE_EXTENSION;
	const size_t kScriptSuffixLen = sizeof( kScriptSuffix ) - 1;

	lua_getfield( L, LUA_REGISTRYINDEX, key );
	if ( lua_isstring( L, -1 ) )
	{
		const char *name = lua_tostring( L, -1 );
		Rtt_ASSERT( HasSuffix( name, kScriptSuffix, kScriptSuffixLen ) );

		name = luaL_gsub( L, name, "/", "." );
		name = luaL_gsub( L, name, "\\", "." );

		const char *normalized = lua_tostring( L, -1 );

		lua_pushfstring( L, "%s" LUA_DIRSEP, dstDir );
		lua_pushlstring( L, normalized, lua_objlen( L, -2 ) - ( kScriptSuffixLen - 1 ) );
		lua_pushliteral( L, Rtt_LUA_OBJECT_FILE_EXTENSION );
		lua_concat( L, 3 );

		const char *compiledName = lua_tostring( L, -1 );

		unlink( compiledName );

		lua_pop( L, 3 );
	}
	lua_pop( L, 1 );
}

void
PlatformAppPackager::DoPostCompile( const char *dstDir, int stripDebug )
{
	AddFile( fVM, kBuildSettingsStartFuncCode, "_appStart_", dstDir, stripDebug );
	RemoveFiles( fVM, kBuildSettingsStartScriptName, dstDir );
	RemoveFiles( fVM, kBuildSettingsBuildScriptName, dstDir );
}

/// Returns a copy of the input string, escaping characters as needed such as apostrophies and double quotes.
/// @param input The source string to escape. Expected to be an ASCII or UTF-8 string. Cannot be NULL.
/// @return Returns a static string that is the escaped equivalent of the given input string.
const char * 
PlatformAppPackager::EscapeStringForIOS( const char * input )
{
	static char buf[256];
	const char * s;
	char * d;
	
	for ( s = input, d = buf; *s; )
	{
		if ( *s == '\'' || *s == '\"' )
			*d++ = '\\';
		
		*d++ = *s++;
	}
	*d = 0;
    return buf;
}

/// Copies the source string's characters to the target string, escaping characters that need to be
/// escaped along the way such as apostrophies, double quotes, backslashes, and unicode characters.
/// @see http://developer.android.com/guide/topics/resources/string-resource.html#FormattingAndStyling
/// @see http://en.wikipedia.org/wiki/UTF-8
/// @param sourceString The string to be escaped for Android, such as the application name.
///                     Expected to be an ASCII or UTF-8 string.
/// @param targetString String object that receives a copy of the source string characters along
///                     with any necessary escape characters. The result will be an ASCII string, not UTF-8.
void
PlatformAppPackager::EscapeStringForAndroid( const char *sourceString, String &targetString )
{
	char stringBuffer[32];
	size_t sourceStringLength;
	char sourceCharacter;
	int index;
#ifdef Rtt_WIN_ENV
	const char* UNICODE_ESCAPE_FORMAT_STRING = "\\u%04X";
	U16 unicodeCharacter;
	int unicodeByteCount;
#endif // Rtt_WIN_ENV


	// Clear the target string first.
	targetString.Set(NULL);

	// Do not continue if given an invalid source string.
	if (NULL == sourceString)
	{
		return;
	}

	// Copy characters from the source string to the target string, escaping characters along the way.
	sourceStringLength = strlen(sourceString);
	for (index = 0; index < (int)sourceStringLength; index++)
	{
		sourceCharacter = sourceString[index];
		if ('\'' == sourceCharacter)
		{
			// Append an escaped apostrophe.
			targetString.Append("&apos;");
		}
		else if ('\"' == sourceCharacter)
		{
			// Append an escaped double quote.
			targetString.Append("&quot;");
		}
		else if ('\\' == sourceCharacter)
		{
			// Append an escaped back slash.
			targetString.Append("\\\\");
		}
		else if ('&' == sourceCharacter)
		{
			// Append an escaped ampersand.
			targetString.Append("&amp;");
		}
		else if ('<' == sourceCharacter)
		{
			// Append an escaped less than sign.
			targetString.Append("&lt;");
		}
		else if ('>' == sourceCharacter)
		{
			// Append an escaped greater than sign.
			targetString.Append("&gt;");
		}
//TODO: OS X does not need the below, but Windows currently does because its WinXmlRpc library does not support UTF-8 yet.
#ifdef Rtt_WIN_ENV
		else if ((sourceCharacter & 0x80) && ((sourceString[index + 1] & 0xC0) == 0x80))
		{
			// The 1st bit in the character is set and the next character bits is set to 10xxxxxx.
			// This means that the next set of bytes are for 1 unicode character.
			// Extract the unicode bits from the next set of bytes and append them to the string.
			unicodeCharacter = 0;
			if ((sourceCharacter & 0xF0) == 0xE0)
			{
				// This is a 16-bit unicode character. Fetch the most significant bits.
				unicodeByteCount = 2;
				unicodeCharacter |= sourceCharacter & 0x0F;
			}
			else if ((sourceCharacter & 0xE0) == 0xC0)
			{
				// This is a 11-bit unicode character. Fetch the most significant bits.
				unicodeByteCount = 1;
				unicodeCharacter |= sourceCharacter & 0x1F;
			}
			else
			{
				// We do not support any other unicode length.
				continue;
			}
			for (; unicodeByteCount > 0; unicodeByteCount--)
			{
				index++;
				if (index >= (int)sourceStringLength)
				{
					break;
				}
				unicodeCharacter <<= 6;
				unicodeCharacter |= sourceString[index] & 0x3F;
			}
			snprintf(stringBuffer, 32, UNICODE_ESCAPE_FORMAT_STRING, unicodeCharacter);
			targetString.Append(stringBuffer);
		}
#endif // Rtt_WIN_ENV
		else
		{
			// This character does not need to be escaped. Just append it.
			stringBuffer[0] = sourceCharacter;
			stringBuffer[1] = '\0';
			targetString.Append(stringBuffer);
		}
	}
}

/// Copies the source string's characters to the target string, escaping characters that need to be
/// escaped to be a valid file name on Windows or OS X.
/// @param sourceString The file name string to be escaped.
/// @param targetString String object that receives the escaped file name string.
/// @param unicodeAllowed Are Unicode substitutions for illegal characters allowed.
void
PlatformAppPackager::EscapeFileName( const char *sourceString, String &targetString, bool unicodeAllowed /* = false */ )
{
	char stringBuffer[32];
	size_t sourceStringLength;
	char sourceCharacter;
	int index;
#ifdef Rtt_WIN_ENV
    int quoteCount = 1;
#endif
    const char *placeholderChar = "_";

	// Clear the target string first.
	targetString.Set(NULL);

	// Do not continue if given an invalid source string.
	if (NULL == sourceString)
	{
		return;
	}

	// Copy characters from the source string to the target string, escaping characters along the way.
	sourceStringLength = strlen(sourceString);
	for (index = 0; index < (int)sourceStringLength; index++)
	{
		sourceCharacter = sourceString[index];
		switch (sourceCharacter)
		{
			case '/':   // ∕ DIVISION SLASH; Unicode: U+2215, UTF-8: E2 88 95
				targetString.Append(unicodeAllowed ? "∕" : placeholderChar);
				break;
#ifdef Rtt_WIN_ENV
				// More characters need to be escaped on Windows
			case '\'':  // ’ RIGHT SINGLE QUOTATION MARK; Unicode: U+2019, UTF-8: E2 80 99
                targetString.Append(unicodeAllowed ? "’" : placeholderChar);
                break;
			case '"':   // ” RIGHT DOUBLE QUOTATION MARK; Unicode: U+201D, UTF-8: E2 80 9D
                        // “ LEFT DOUBLE QUOTATION MARK; Unicode: U+201C, UTF-8: E2 80 9C
                targetString.Append(unicodeAllowed ? (quoteCount++ % 2 ? "“" : "”") : placeholderChar);
                break;
			case '\\':  // ⧵  REVERSE SOLIDUS OPERATOR; Unicode: U+29F5, UTF-8: E2 A7 B5
                targetString.Append(unicodeAllowed ? "⧵" : placeholderChar);
                break;
			case '*':   // ﹡ SMALL ASTERISK; Unicode: U+FE61, UTF-8: EF B9 A1
                targetString.Append(unicodeAllowed ? "﹡" : placeholderChar);
                break;
			case '?':   // ？ FULLWIDTH QUESTION MARK; Unicode: U+FF1F, UTF-8: EF BC 9F
                targetString.Append(unicodeAllowed ? "？" : placeholderChar);
                break;
			case '|':   // ｜ FULLWIDTH VERTICAL LINE; Unicode: U+FF5C, UTF-8: EF BD 9C
                targetString.Append(unicodeAllowed ? "｜" : placeholderChar);
                break;
			case '&':   // ＆ FULLWIDTH AMPERSAND; Unicode: U+FF06, UTF-8: EF BC 86
                targetString.Append(unicodeAllowed ? "＆" : placeholderChar);
				break;
			case '<':   // ＜ FULLWIDTH LESS-THAN SIGN; Unicode: U+FF1C, UTF-8: EF BC 9C
                targetString.Append(unicodeAllowed ? "＜" : placeholderChar);
				break;
			case '>':   // ＞ FULLWIDTH GREATER-THAN SIGN; Unicode: U+FF1E, UTF-8: EF BC 9E
                targetString.Append(unicodeAllowed ? "＞" : placeholderChar);
                break;
			case ':':   // ： FULLWIDTH COLON; Unicode: U+FF1A, UTF-8: EF BC 9A
                targetString.Append(unicodeAllowed ? "：" : placeholderChar);
				break;
            case ' ':   //   EM SPACE; Unicode: U+2003, UTF-8: E2 80 83
                // targetString.Append(unicodeAllowed ? " " : " ");
                targetString.Append(" ");
				break;
#endif // Rtt_WIN_ENV
			default:
				// This character does not need to be escaped. Just append it.
				stringBuffer[0] = sourceCharacter;
				stringBuffer[1] = '\0';
				targetString.Append(stringBuffer);
				break;
		}
	}
}

#if !defined( Rtt_NO_GUI )
bool
PlatformAppPackager::AreAllPluginsAvailable( Runtime *runtime, String *missingPluginsString /*=NULL*/ )
{
	// Validate.
	if ( !runtime )
	{
		return false;
	}

	// Fetch the runtime's Lua state.
	lua_State* luaStatePointer = runtime->VMContext().L();
	if ( !luaStatePointer )
	{
		Rtt_ASSERT(0);
		return false;
	}

	// Do not continue if the given Corona runtime does not require any plugins.
	if ( runtime->RequiresDownloadablePlugins() == false )
	{
		return true;
	}

	// Query the runtime's "shell.lua" if all plugin zip files have been downloaded/acquired.
	Lua::NewEvent( luaStatePointer, "_internalQueryAreAllPluginsAvailable" );
	Lua::DispatchRuntimeEvent( luaStatePointer, 1 );
	bool hasAcquiredAllPlugins = false;
	if ( lua_isboolean( luaStatePointer, -1 ) )
	{
		hasAcquiredAllPlugins = lua_toboolean( luaStatePointer, -1 ) ? true : false;
	}
	else if ( lua_type( luaStatePointer, -1 ) == LUA_TSTRING )
	{
		if ( missingPluginsString )
		{
			missingPluginsString->Set( lua_tostring( luaStatePointer, -1 ) );
		}
	}
	lua_pop( luaStatePointer, 1 );
	return hasAcquiredAllPlugins;
}
#endif

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

