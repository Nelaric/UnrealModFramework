// Copyright (c) 2026 Nelaric Contributors
#include "ModDirectoryWatcher.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_LINUX
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>
#elif PLATFORM_MAC
#include "Misc/ScopeLock.h"
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#endif

namespace Mod::IO
{
namespace
{
void AddChange(FDirectoryChanges& Changes, FString Name)
{
	if (!Changes.bRescan)
	{
		Changes.Names.Add(MoveTemp(Name));
		if (Changes.Names.Num() > 256)
		{
			Changes.Names.Reset();
			Changes.bRescan = true;
		}
	}
}
} // namespace

struct FDirectoryWatcher::FNativeWatch
{
	bool Start(const FString& Directory);
	FDirectoryChanges ReadChanges();
	~FNativeWatch();

	bool bActive = false;
#if PLATFORM_WINDOWS
	bool Arm();
	HANDLE DirectoryHandle = INVALID_HANDLE_VALUE;
	OVERLAPPED Request = {};
	alignas(DWORD) uint8 Buffer[64 * 1024];
#elif PLATFORM_LINUX
	int FileDescriptor = -1;
#elif PLATFORM_MAC
	static void OnEvents(ConstFSEventStreamRef, void* Context, size_t Count, void* Paths,
	                     const FSEventStreamEventFlags Flags[], const FSEventStreamEventId[]);
	FSEventStreamRef Stream = nullptr;
	dispatch_queue_t Queue = nullptr;
	FString Path;
	FCriticalSection ChangesLock;
	FDirectoryChanges Pending;
	bool bRootChanged = false;
#endif
};

FDirectoryWatcher::FDirectoryWatcher() = default;
FDirectoryWatcher::~FDirectoryWatcher() = default;

bool FDirectoryWatcher::Start(const FString& Directory)
{
	Stop();
	Native = MakeUnique<FNativeWatch>();
	if (!Native->Start(Directory))
	{
		Stop();
	}
	return IsActive();
}

void FDirectoryWatcher::Stop()
{
	Native.Reset();
}

FDirectoryChanges FDirectoryWatcher::ReadChanges()
{
	return IsActive() ? Native->ReadChanges() : FDirectoryChanges{};
}

bool FDirectoryWatcher::IsActive() const
{
	return Native && Native->bActive;
}

#if PLATFORM_WINDOWS
bool FDirectoryWatcher::FNativeWatch::Arm()
{
	::ResetEvent(Request.hEvent);
	bActive = !!::ReadDirectoryChangesW(DirectoryHandle, Buffer, sizeof(Buffer), false,
	                                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
	                                        FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
	                                    nullptr, &Request, nullptr);
	return bActive;
}

bool FDirectoryWatcher::FNativeWatch::Start(const FString& Directory)
{
	FString NativePath = Directory;
	FPaths::MakePlatformFilename(NativePath);
	DirectoryHandle =
	    ::CreateFileW(*NativePath, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
	                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	Request.hEvent = ::CreateEventW(nullptr, true, false, nullptr);
	return DirectoryHandle != INVALID_HANDLE_VALUE && Request.hEvent && Arm();
}

FDirectoryChanges FDirectoryWatcher::FNativeWatch::ReadChanges()
{
	FDirectoryChanges Changes;
	DWORD Bytes = 0;
	if (!::GetOverlappedResult(DirectoryHandle, &Request, &Bytes, false))
	{
		const DWORD Error = ::GetLastError();
		if (Error != ERROR_IO_INCOMPLETE)
		{
			Changes.bRescan = true;
			bActive = Error == ERROR_NOTIFY_ENUM_DIR && Arm();
		}
		return Changes;
	}

	constexpr DWORD PrefixSize = STRUCT_OFFSET(FILE_NOTIFY_INFORMATION, FileName);
	Changes.bRescan = Bytes == 0 || Bytes > sizeof(Buffer);
	if (Changes.bRescan)
	{
		Arm();
		return Changes;
	}
	for (DWORD Offset = 0; Offset < Bytes;)
	{
		const DWORD Remaining = Bytes - Offset;
		if (Remaining < PrefixSize)
		{
			Changes.bRescan = true;
			break;
		}
		const FILE_NOTIFY_INFORMATION& Entry = *reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(Buffer + Offset);
		if (Entry.FileNameLength > Remaining - PrefixSize || Entry.FileNameLength % sizeof(WCHAR) != 0)
		{
			Changes.bRescan = true;
			break;
		}
		AddChange(Changes, FString(static_cast<int32>(Entry.FileNameLength / sizeof(WCHAR)), Entry.FileName));
		if (Entry.NextEntryOffset == 0)
		{
			break;
		}
		if (Entry.NextEntryOffset < PrefixSize + Entry.FileNameLength || Entry.NextEntryOffset > Remaining ||
		    Entry.NextEntryOffset % alignof(DWORD) != 0)
		{
			Changes.bRescan = true;
			break;
		}
		Offset += Entry.NextEntryOffset;
	}
	Changes.bRescan |= !Arm();
	return Changes;
}

FDirectoryWatcher::FNativeWatch::~FNativeWatch()
{
	if (DirectoryHandle != INVALID_HANDLE_VALUE)
	{
		if (bActive)
		{
			::CancelIoEx(DirectoryHandle, &Request);
			DWORD Bytes = 0;
			::GetOverlappedResult(DirectoryHandle, &Request, &Bytes, true);
		}
		::CloseHandle(DirectoryHandle);
	}
	if (Request.hEvent)
	{
		::CloseHandle(Request.hEvent);
	}
}
#elif PLATFORM_LINUX
bool FDirectoryWatcher::FNativeWatch::Start(const FString& Directory)
{
	FileDescriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	bActive = FileDescriptor >= 0 &&
	          inotify_add_watch(FileDescriptor, TCHAR_TO_UTF8(*Directory),
	                            IN_CREATE | IN_DELETE | IN_MOVE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
	                                IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW) >= 0;
	return bActive;
}

FDirectoryChanges FDirectoryWatcher::FNativeWatch::ReadChanges()
{
	FDirectoryChanges Changes;
	alignas(inotify_event) char Buffer[16 * 1024];
	const ssize_t Bytes = read(FileDescriptor, Buffer, sizeof(Buffer));
	if (Bytes < 0 && errno != EAGAIN && errno != EINTR)
	{
		Changes.bRescan = true;
		bActive = false;
	}
	for (ssize_t Offset = 0; Offset < Bytes;)
	{
		const inotify_event& Entry = *reinterpret_cast<const inotify_event*>(Buffer + Offset);
		if (Entry.mask & (IN_Q_OVERFLOW | IN_IGNORED | IN_MOVE_SELF | IN_DELETE_SELF))
		{
			Changes.bRescan = true;
			bActive &= (Entry.mask & (IN_IGNORED | IN_MOVE_SELF | IN_DELETE_SELF)) == 0;
		}
		if (Entry.len && !(Entry.mask & IN_ISDIR))
		{
			AddChange(Changes, UTF8_TO_TCHAR(Entry.name));
		}
		Offset += sizeof(inotify_event) + Entry.len;
	}
	return Changes;
}

FDirectoryWatcher::FNativeWatch::~FNativeWatch()
{
	if (FileDescriptor >= 0)
	{
		close(FileDescriptor);
	}
}
#elif PLATFORM_MAC
bool FDirectoryWatcher::FNativeWatch::Start(const FString& Directory)
{
	Path = Directory;
	CFStringRef DirectoryString = FPlatformString::TCHARToCFString(*Path);
	const void* Value = DirectoryString;
	CFArrayRef Directories = CFArrayCreate(kCFAllocatorDefault, &Value, 1, &kCFTypeArrayCallBacks);
	FSEventStreamContext Context = {0, this, nullptr, nullptr, nullptr};
	Stream = FSEventStreamCreate(kCFAllocatorDefault, &OnEvents, &Context, Directories, kFSEventStreamEventIdSinceNow,
	                             0.15, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot);
	CFRelease(Directories);
	CFRelease(DirectoryString);
	if (Stream)
	{
		Queue = dispatch_queue_create("org.nelaric.modruntime.directorywatcher", DISPATCH_QUEUE_SERIAL);
		FSEventStreamSetDispatchQueue(Stream, Queue);
		bActive = FSEventStreamStart(Stream);
	}
	return bActive;
}

void FDirectoryWatcher::FNativeWatch::OnEvents(ConstFSEventStreamRef, void* Context, size_t Count, void* Paths,
                                               const FSEventStreamEventFlags Flags[], const FSEventStreamEventId[])
{
	FNativeWatch& Watch = *static_cast<FNativeWatch*>(Context);
	const char* const* EventPaths = static_cast<const char* const*>(Paths);
	FScopeLock Lock(&Watch.ChangesLock);
	for (size_t Index = 0; Index < Count; ++Index)
	{
		Watch.bRootChanged |= (Flags[Index] & kFSEventStreamEventFlagRootChanged) != 0;
		Watch.Pending.bRescan |= Watch.bRootChanged || (Flags[Index] & kFSEventStreamEventFlagMustScanSubDirs) != 0;
		const FString EventPath = UTF8_TO_TCHAR(EventPaths[Index]);
		if (FPaths::IsSamePath(FPaths::GetPath(EventPath), Watch.Path))
		{
			AddChange(Watch.Pending, FPaths::GetCleanFilename(EventPath));
		}
	}
}

FDirectoryChanges FDirectoryWatcher::FNativeWatch::ReadChanges()
{
	FScopeLock Lock(&ChangesLock);
	bActive &= !bRootChanged;
	FDirectoryChanges Changes = MoveTemp(Pending);
	Pending = {};
	return Changes;
}

FDirectoryWatcher::FNativeWatch::~FNativeWatch()
{
	if (Stream)
	{
		FSEventStreamStop(Stream);
		FSEventStreamInvalidate(Stream);
		dispatch_sync_f(Queue, nullptr, [](void*) {});
		FSEventStreamRelease(Stream);
		dispatch_release(Queue);
	}
}
#else
bool FDirectoryWatcher::FNativeWatch::Start(const FString&)
{
	return false;
}

FDirectoryChanges FDirectoryWatcher::FNativeWatch::ReadChanges()
{
	return {};
}

FDirectoryWatcher::FNativeWatch::~FNativeWatch() = default;
#endif
} // namespace Mod::IO

IMPLEMENT_MODULE(FDefaultModuleImpl, HAL)
