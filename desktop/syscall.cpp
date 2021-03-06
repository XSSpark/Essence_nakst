// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

ThreadLocalStorage *GetThreadLocalStorage() {
	return (ThreadLocalStorage *) ProcessorTLSRead(tlsStorageOffset);
}

double EsTimeStampMs() {
	if (!api.startupInformation->timeStampTicksPerMs) {
		return 0;
	} else {
		return (double) ProcessorReadTimeStamp() / api.startupInformation->timeStampTicksPerMs;
	}
}

void EsDateNowUTC(EsDateComponents *date) {
	uint64_t linear = api.global->schedulerTimeMs + api.global->schedulerTimeOffset;
	DateToComponents(linear, date);
}

void *EsMemoryReserve(size_t size, EsMemoryProtection protection, uint32_t flags) {
	intptr_t result = EsSyscall(ES_SYSCALL_MEMORY_ALLOCATE, size, flags, protection, 0);

	if (result >= 0) {
		return (void *) result;
	} else {
		return nullptr;
	}
}

EsHandle EsMemoryCreateShareableRegion(size_t bytes) {
	return EsSyscall(ES_SYSCALL_MEMORY_ALLOCATE, bytes, 0, 0, 1);
}

void EsMemoryUnreserve(void *address, size_t size) {
	EsSyscall(ES_SYSCALL_MEMORY_FREE, (uintptr_t) address, size, 0, 0);
}

bool EsMemoryCommit(void *pointer, size_t bytes) {
	EsAssert(((uintptr_t) pointer & (ES_PAGE_SIZE - 1)) == 0 && (bytes & (ES_PAGE_SIZE - 1)) == 0); // Misaligned pointer/bytes in EsMemoryCommit.
	return ES_SUCCESS == (intptr_t) EsSyscall(ES_SYSCALL_MEMORY_COMMIT, (uintptr_t) pointer >> ES_PAGE_BITS, bytes >> ES_PAGE_BITS, 0, 0);
}

void EsMemoryFaultRange(const void *pointer, size_t bytes, uint32_t flags) {
	EsSyscall(ES_SYSCALL_MEMORY_FAULT_RANGE, (uintptr_t) pointer, bytes, flags, 0);
}

bool EsMemoryDecommit(void *pointer, size_t bytes) {
	EsAssert(((uintptr_t) pointer & (ES_PAGE_SIZE - 1)) == 0 && (bytes & (ES_PAGE_SIZE - 1)) == 0); // Misaligned pointer/bytes in EsMemoryDecommit.
	return ES_SUCCESS == (intptr_t) EsSyscall(ES_SYSCALL_MEMORY_COMMIT, (uintptr_t) pointer >> ES_PAGE_BITS, bytes >> ES_PAGE_BITS, 1, 0);
}

EsError EsProcessCreate(const EsProcessCreationArguments *arguments, EsProcessInformation *information) {
	EsProcessInformation _information;
	if (!information) information = &_information;

	EsError error = EsSyscall(ES_SYSCALL_PROCESS_CREATE, (uintptr_t) arguments, 0, (uintptr_t) information, 0);

	if (error == ES_SUCCESS && information == &_information) {
		EsHandleClose(information->handle);
		EsHandleClose(information->mainThread.handle);
	}

	return error;
}

EsError EsMessagePost(EsElement *target, EsMessage *message) {
	EsMutexAcquire(&api.postBoxMutex);

	_EsMessageWithObject m = { target, *message };
	bool success = api.postBox.Add(m);

	if (api.postBox.Length() == 1 && success) {
		EsMessage m;
		m.type = ES_MSG_WAKEUP;
		success = ES_SUCCESS == (EsError) EsSyscall(ES_SYSCALL_MESSAGE_POST, (uintptr_t) &m, 0, ES_CURRENT_PROCESS, 0);
	}

	EsMutexRelease(&api.postBoxMutex);

	return success ? ES_SUCCESS : ES_ERROR_INSUFFICIENT_RESOURCES;
}

EsError EsMessagePostRemote(EsHandle process, EsMessage *message) {
	return EsSyscall(ES_SYSCALL_MESSAGE_POST, (uintptr_t) message, 0, process, 0);
}

EsHandle EsEventCreate(bool autoReset) {
	return EsSyscall(ES_SYSCALL_EVENT_CREATE, autoReset, 0, 0, 0);
}

void EsEventSet(EsHandle handle) {
	EsSyscall(ES_SYSCALL_EVENT_SET, handle, 0, 0, 0);
}

void EsEventReset(EsHandle handle) {
	EsSyscall(ES_SYSCALL_EVENT_RESET, handle, 0, 0, 0);
}

EsError EsHandleClose(EsHandle handle) {
	return EsSyscall(ES_SYSCALL_HANDLE_CLOSE, handle, 0, 0, 0);
}

void EsThreadTerminate(EsHandle thread) {
	EsSyscall(ES_SYSCALL_THREAD_TERMINATE, thread, 0, 0, 0);
}

void EsProcessTerminate(EsHandle process, int status) {
	EsSyscall(ES_SYSCALL_PROCESS_TERMINATE, process, status, 0, 0);
}

void EsProcessTerminateCurrent() {
	EsSyscall(ES_SYSCALL_PROCESS_TERMINATE, ES_CURRENT_PROCESS, 0, 0, 0);
}

int EsProcessGetExitStatus(EsHandle process) {
	return EsSyscall(ES_SYSCALL_PROCESS_GET_STATUS, process, 0, 0, 0);
}

void EsProcessGetCreateData(EsProcessCreateData *data) {
	EsMemoryCopy(data, &api.startupInformation->data, sizeof(EsProcessCreateData));
}

void ThreadInitialise(ThreadLocalStorage *local);

__attribute__((no_instrument_function))
void ThreadEntry(EsGeneric argument, EsThreadEntryCallback entryFunction) {
	ThreadLocalStorage local;
	ThreadInitialise(&local);
	entryFunction(argument);
	EsThreadTerminate(ES_CURRENT_THREAD);
}

EsError EsThreadCreate(EsThreadEntryCallback entryFunction, EsThreadInformation *information, EsGeneric argument) {
	EsThreadInformation discard = {};

	if (!information) {
		information = &discard;
	}

	EsError error = EsSyscall(ES_SYSCALL_THREAD_CREATE, (uintptr_t) ThreadEntry, (uintptr_t) entryFunction, (uintptr_t) information, argument.u);

	if (error == ES_SUCCESS && information == &discard) {
		EsHandleClose(information->handle);
	}

	return error;
}

EsHandle EsMemoryShare(EsHandle sharedMemoryRegion, EsHandle targetProcess, bool readOnly) {
	return EsSyscall(ES_SYSCALL_HANDLE_SHARE, sharedMemoryRegion, targetProcess, readOnly, 0);
}

void *EsMemoryMapObject(EsHandle sharedMemoryRegion, uintptr_t offset, size_t size, unsigned flags) {
	intptr_t result = EsSyscall(ES_SYSCALL_MEMORY_MAP_OBJECT, sharedMemoryRegion, offset, size, flags);

	if (result >= 0) {
		return (void *) result;
	} else {
		return nullptr;
	}
}

uintptr_t EsWait(const EsHandle *handles, size_t count, uintptr_t timeoutMs) {
	return EsSyscall(ES_SYSCALL_WAIT, (uintptr_t) handles, count, timeoutMs, 0);
}

void EsProcessPause(EsHandle process, bool resume) {
	EsSyscall(ES_SYSCALL_PROCESS_PAUSE, process, resume, 0, 0);
}

EsObjectID EsThreadGetID(EsHandle thread) {
	if (thread == ES_CURRENT_THREAD) {
		return GetThreadLocalStorage()->id;
	} else {
		EsObjectID id;
		EsSyscall(ES_SYSCALL_THREAD_GET_ID, thread, (uintptr_t) &id, 0, 0);
		return id;
	}
}

EsObjectID EsProcessGetID(EsHandle process) {
	EsObjectID id;
	EsSyscall(ES_SYSCALL_THREAD_GET_ID, process, (uintptr_t) &id, 0, 0);
	return id;
}

void EsBatch(EsBatchCall *calls, size_t count) {
#if 0
	for (uintptr_t i = 0; i < count; i++) {
		EsBatchCall *call = calls + i;
		// ... modify system call for version changes ... 
	}
#endif

	EsSyscall(ES_SYSCALL_BATCH, (uintptr_t) calls, count, 0, 0);
}

void EsConstantBufferRead(EsHandle buffer, void *output) {
	EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_READ, buffer, (uintptr_t) output, 0, 0);
}

void EsProcessGetState(EsHandle process, EsProcessState *state) {
	EsSyscall(ES_SYSCALL_PROCESS_GET_STATE, process, (uintptr_t) state, 0, 0);
}

void EsSchedulerYield() {
	EsSyscall(ES_SYSCALL_YIELD_SCHEDULER, 0, 0, 0, 0);
}

void EsSleep(uint64_t milliseconds) {
	EsSyscall(ES_SYSCALL_SLEEP, milliseconds >> 32, milliseconds & 0xFFFFFFFF, 0, 0);
}

EsHandle EsTakeSystemSnapshot(int type, size_t *bufferSize) {
	return EsSyscall(ES_SYSCALL_SYSTEM_TAKE_SNAPSHOT, type, (uintptr_t) bufferSize, 0, 0);
}

EsHandle EsProcessOpen(uint64_t pid) {
	// TODO This won't work correctly if arguments to system call are 32-bit.
	return EsSyscall(ES_SYSCALL_PROCESS_OPEN, pid, 0, 0, 0);
}

EsHandle EsConstantBufferShare(EsHandle constantBuffer, EsHandle targetProcess) {
	return EsSyscall(ES_SYSCALL_HANDLE_SHARE, constantBuffer, targetProcess, 0, 0);
}

EsHandle EsConstantBufferCreate(const void *data, size_t dataBytes, EsHandle targetProcess) {
	return EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_CREATE, (uintptr_t) data, targetProcess, dataBytes, 0);
}

size_t EsConstantBufferGetSize(EsHandle buffer) {
	return EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_READ, buffer, 0, 0, 0);
}

EsError EsAddressResolve(const char *domain, ptrdiff_t domainBytes, uint32_t flags, EsAddress *address) {
	return EsSyscall(ES_SYSCALL_DOMAIN_NAME_RESOLVE, (uintptr_t) domain, domainBytes, (uintptr_t) address, flags);
}

EsError EsConnectionOpen(EsConnection *connection, uint32_t flags) {
	connection->error = ES_SUCCESS;
	connection->open = false;

	EsError error = EsSyscall(ES_SYSCALL_CONNECTION_OPEN, (uintptr_t) connection, flags, 0, 0);

	if (error == ES_SUCCESS && (flags & ES_CONNECTION_OPEN_WAIT)) {
		while (!connection->open && connection->error == ES_SUCCESS) {
			EsConnectionPoll(connection);
		}

		return connection->error;
	} else {
		return error;
	}
}

void EsConnectionPoll(EsConnection *connection) {
	EsSyscall(ES_SYSCALL_CONNECTION_POLL, (uintptr_t) connection, 0, 0, connection->handle);
}

void EsConnectionNotify(EsConnection *connection) {
	EsSyscall(ES_SYSCALL_CONNECTION_NOTIFY, 0, connection->sendWritePointer, connection->receiveReadPointer, connection->handle);
}

void EsConnectionClose(EsConnection *connection) {
	EsObjectUnmap(connection->sendBuffer);
	EsHandleClose(connection->handle);
}

EsError EsConnectionWriteSync(EsConnection *connection, const void *_data, size_t dataBytes) {
	const uint8_t *data = (const uint8_t *) _data;

	while (dataBytes) {
		EsConnectionPoll(connection);

		if (connection->error != ES_SUCCESS) {
			return connection->error;
		}

		size_t space = connection->sendWritePointer >= connection->sendReadPointer 
			? connection->sendBufferBytes - connection->sendWritePointer
			: connection->sendReadPointer - connection->sendWritePointer - 1;

		if (!space) {
			continue;
		}

		size_t bytesToWrite = space > dataBytes ? dataBytes : space;
		EsMemoryCopy(connection->sendBuffer + connection->sendWritePointer, data, bytesToWrite);
		data += bytesToWrite, dataBytes -= bytesToWrite;
		connection->sendWritePointer = (connection->sendWritePointer + bytesToWrite) % connection->sendBufferBytes;
		EsConnectionNotify(connection);
	}

	return ES_SUCCESS;
}

EsError EsConnectionRead(EsConnection *connection, void *_buffer, size_t bufferBytes, size_t *bytesRead) {
	uint8_t *buffer = (uint8_t *) _buffer;
	*bytesRead = 0;

	EsConnectionPoll(connection);

	if (connection->error != ES_SUCCESS) {
		return connection->error;
	}

	while (bufferBytes && connection->receiveReadPointer != connection->receiveWritePointer) {
		size_t bytesAvailable = connection->receiveReadPointer > connection->receiveWritePointer
			? connection->receiveBufferBytes - connection->receiveReadPointer
			: connection->receiveWritePointer - connection->receiveReadPointer;
		size_t bytesToRead = bufferBytes > bytesAvailable ? bytesAvailable : bufferBytes;
		EsMemoryCopy(buffer, connection->receiveBuffer + connection->receiveReadPointer, bytesToRead);
		connection->receiveReadPointer = (connection->receiveReadPointer + bytesToRead) % connection->receiveBufferBytes;
		buffer += bytesToRead, bufferBytes -= bytesToRead;
		*bytesRead += bytesToRead;
		EsConnectionNotify(connection);
	}

	return ES_SUCCESS;
}

size_t EsGameControllerStatePoll(EsGameControllerState *buffer) {
	return EsSyscall(ES_SYSCALL_GAME_CONTROLLER_STATE_POLL, (uintptr_t) buffer, 0, 0, 0);
}

bool DesktopSyscall(EsObjectID windowID, struct ApplicationProcess *process, uint8_t *buffer, size_t bytes, EsBuffer *pipe);
struct ApplicationProcess *DesktopGetApplicationProcessForDesktop();

void MessageDesktop(void *message, size_t messageBytes, EsHandle embeddedWindow = ES_INVALID_HANDLE, EsBuffer *responseBuffer = nullptr) {
	static EsMutex messageDesktopMutex = {};

	EsObjectID embeddedWindowID = embeddedWindow ? EsSyscall(ES_SYSCALL_WINDOW_GET_ID, embeddedWindow, 0, 0, 0) : 0;

	EsMutexAcquire(&messageDesktopMutex);

	if (api.startupInformation->isDesktop) {
		struct ApplicationProcess *process = DesktopGetApplicationProcessForDesktop();
		bool noFatalErrors = DesktopSyscall(embeddedWindowID, process, (uint8_t *) message, messageBytes, responseBuffer);
		EsAssert(noFatalErrors);
	} else {
		if (messageBytes <= DESKTOP_MESSAGE_SIZE_LIMIT) {
			uint32_t length = messageBytes;
			EsPipeWrite(api.desktopRequestPipe, &length, sizeof(length));
			EsPipeWrite(api.desktopRequestPipe, &embeddedWindowID, sizeof(embeddedWindowID));
			EsPipeWrite(api.desktopRequestPipe, message, messageBytes);
			EsAssert(sizeof(length) == EsPipeRead(api.desktopResponsePipe, &length, sizeof(length), false));
			EsAssert((length != 0) == (responseBuffer != 0));

			while (length) {
				char buffer[4096];
				size_t bytesRead = EsPipeRead(api.desktopResponsePipe, buffer, sizeof(buffer) > length ? length : sizeof(buffer), false);
				if (!bytesRead) break;
				EsBufferWrite(responseBuffer, buffer, bytesRead);
				length -= bytesRead;
			}
		}
	}

	if (responseBuffer) {
		responseBuffer->bytes = responseBuffer->position;
		responseBuffer->position = 0;
	}

	EsMutexRelease(&messageDesktopMutex);
}

struct ClipboardInformation {
	uint8_t desktopMessageTag;
	intptr_t error;
	EsClipboardFormat format;
	uint32_t flags;
};

EsFileStore *EsClipboardOpen(EsClipboard clipboard) {
	(void) clipboard;
	uint8_t m = DESKTOP_MSG_CREATE_CLIPBOARD_FILE;
	EsBuffer buffer = { .canGrow = true };
	EsHandle file;
	EsError error;
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, &buffer);
	EsBufferReadInto(&buffer, &file, sizeof(file));
	EsBufferReadInto(&buffer, &error, sizeof(error));
	EsHeapFree(buffer.out);
	EsFileStore *fileStore = FileStoreCreateFromHandle(file);
	if (!fileStore) return nullptr;
	fileStore->error = error;
	return fileStore;
}

EsError EsClipboardCloseAndAdd(EsClipboard clipboard, EsClipboardFormat format, EsFileStore *fileStore, uint32_t flags) {
	(void) clipboard;
	EsError error = fileStore->error;

	if (error == ES_SUCCESS) {
		ClipboardInformation information = {};
		information.desktopMessageTag = DESKTOP_MSG_CLIPBOARD_PUT;
		information.error = error;
		information.format = format;
		information.flags = flags;
		MessageDesktop(&information, sizeof(information));
	}

	FileStoreCloseHandle(fileStore);
	return error;
}

EsError EsClipboardAddText(EsClipboard clipboard, const char *text, ptrdiff_t textBytes) {
	EsFileStore *fileStore = EsClipboardOpen(clipboard);

	if (fileStore) {
		EsFileStoreWriteAll(fileStore, text, textBytes); 
		return EsClipboardCloseAndAdd(clipboard, ES_CLIPBOARD_FORMAT_TEXT, fileStore);
	} else {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}
}

void ClipboardGetInformation(EsHandle *file, ClipboardInformation *information) {
	uint8_t m = DESKTOP_MSG_CLIPBOARD_GET;
	EsBuffer buffer = { .canGrow = true };
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, &buffer);
	EsBufferReadInto(&buffer, information, sizeof(*information));
	EsBufferReadInto(&buffer, file, sizeof(file));
	EsHeapFree(buffer.out);
}

bool EsClipboardHasFormat(EsClipboard clipboard, EsClipboardFormat format) {
	(void) clipboard;
	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);
	if (file) EsHandleClose(file);
	if (information.error != ES_SUCCESS) return false;

	if (format == ES_CLIPBOARD_FORMAT_TEXT) {
		return information.format == ES_CLIPBOARD_FORMAT_TEXT || information.format == ES_CLIPBOARD_FORMAT_PATH_LIST;
	} else {
		return information.format == format;
	}
}

bool EsClipboardHasData(EsClipboard clipboard) {
	(void) clipboard;
	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);
	if (file) EsHandleClose(file);
	return information.error == ES_SUCCESS && information.format != ES_CLIPBOARD_FORMAT_INVALID;
}

char *EsClipboardReadText(EsClipboard clipboard, size_t *bytes, uint32_t *flags) {
	(void) clipboard;

	char *result = nullptr;
	*bytes = 0;

	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);

	if (file) {
		if (information.format == ES_CLIPBOARD_FORMAT_TEXT || information.format == ES_CLIPBOARD_FORMAT_PATH_LIST) {
			result = (char *) EsFileReadAllFromHandle(file, bytes);

			if (flags) {
				*flags = information.flags;
			}
		}

		EsHandleClose(file);
	}

	return result;
}

void EsPipeCreate(EsHandle *readEnd, EsHandle *writeEnd) {
	*readEnd = *writeEnd = ES_INVALID_HANDLE;
	EsSyscall(ES_SYSCALL_PIPE_CREATE, (uintptr_t) readEnd, (uintptr_t) writeEnd, 0, 0);
}

size_t EsPipeRead(EsHandle pipe, void *buffer, size_t bytes, bool allowShortReads) {
	if (!bytes) {
		return 0;
	} else if (allowShortReads) {
		return EsSyscall(ES_SYSCALL_PIPE_READ, pipe, (uintptr_t) buffer, bytes, 0);
	} else {
		size_t position = 0;

		while (position != bytes) {
			size_t read = EsPipeRead(pipe, buffer ? ((uint8_t *) buffer + position) : nullptr, bytes - position, true);

			if (!read) {
				// There are no writers.
				break;
			} else {
				// Keeping reading until the buffer is full.
				position += read;
				EsAssert(position <= bytes);
			}
		}

		return position;
	}
}

size_t EsPipeWrite(EsHandle pipe, const void *buffer, size_t bytes) {
	if (bytes) {
		return EsSyscall(ES_SYSCALL_PIPE_WRITE, pipe, (uintptr_t) buffer, bytes, 0);
	} else {
		return 0;
	}
}

EsError EsDeviceControl(EsHandle handle, EsDeviceControlType type, void *dp, void *dq) {
	return EsSyscall(ES_SYSCALL_DEVICE_CONTROL, handle, type, (uintptr_t) dp, (uintptr_t) dq);
}

uintptr_t _EsDebugCommand(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	return EsSyscall(ES_SYSCALL_DEBUG_COMMAND, a, b, c, d);
}
