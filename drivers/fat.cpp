// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Validation of all fields.
// TODO Don't load entire FAT in memory.
// TODO Long file names.

#include <module.h>
#include <shared/fat.cpp>

#define SECTOR_SIZE (512)

struct Volume : KFileSystem {
	union {
		char _unused0[SECTOR_SIZE];
		SuperBlock16 sb16;
		SuperBlock32 sb32;
		SuperBlockCommon superBlock;
	};

	KNode *root;
	uint8_t *fat;
	uintptr_t sectorOffset;
	uint32_t terminateCluster;

#define TYPE_FAT12 (12)
#define TYPE_FAT16 (16)
#define TYPE_FAT32 (32)
	int type;

	FATDirectoryEntry *rootDirectoryEntries;
};

struct DirectoryEntryReference {
	uint32_t cluster, offset;
};

struct FSNode {
	Volume *volume;
	FATDirectoryEntry entry;

	// The root directory is loaded during fileSystem mount.
	// If this is non-null, run directory data from here.
	FATDirectoryEntry *rootDirectory;
};

static uint32_t NextCluster(Volume *volume, uint32_t currentCluster) {
	if (volume->type == TYPE_FAT12) {
		uint8_t byte1 = volume->fat[currentCluster * 3 / 2 + 0], 
			byte2 = volume->fat[currentCluster * 3 / 2 + 1];

		if (currentCluster & 1) currentCluster = (byte2 << 4) + (byte1 >> 4);
		else 			currentCluster = (byte2 << 8) + (byte1 >> 0);

		return currentCluster & 0xFFF;
	} else if (volume->type == TYPE_FAT16) {
		return ((uint16_t *) volume->fat)[currentCluster];
	} else if (volume->type == TYPE_FAT32) {
		return ((uint32_t *) volume->fat)[currentCluster];
	} else {
		KernelPanic("[FAT] NextCluster - Unsupported FAT type.\n");
		return 0;
	}
}

static uint32_t CountUsedClusters(Volume *volume) {
	size_t total = 0;

	if (volume->type == TYPE_FAT12) {
		 total = volume->sb16.sectorsPerFAT16 * volume->superBlock.bytesPerSector * 2 / 3;
	} else if (volume->type == TYPE_FAT16) {
		 total = volume->sb16.sectorsPerFAT16 * volume->superBlock.bytesPerSector / 2;
	} else if (volume->type == TYPE_FAT32) {
		 total = volume->sb16.sectorsPerFAT16 * volume->superBlock.bytesPerSector / 4;
	}

	size_t count = 0;

	for (uintptr_t i = 0; i < total; i++) {
		if (NextCluster(volume, i)) {
			count++;
		}
	}

	return count;
}

static EsError Load(KNode *_directory, KNode *_node, KNodeMetadata *, const void *entryData) {
	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;
	SuperBlockCommon *superBlock = &volume->superBlock;

	uint8_t *clusterBuffer = (uint8_t *) EsHeapAllocate(superBlock->sectorsPerCluster * SECTOR_SIZE, false, K_FIXED);
	if (!clusterBuffer) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(clusterBuffer, 0, K_FIXED));

	DirectoryEntryReference reference = *(DirectoryEntryReference *) entryData;
	FATDirectoryEntry entry;

	if (!directory->rootDirectory) {
		EsError error = volume->Access((reference.cluster * superBlock->sectorsPerCluster + volume->sectorOffset) * SECTOR_SIZE, 
				superBlock->sectorsPerCluster * SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) clusterBuffer, ES_FLAGS_DEFAULT);
		if (error != ES_SUCCESS) return error;

		entry = ((FATDirectoryEntry *) clusterBuffer)[reference.offset];
	} else {
		entry = directory->rootDirectory[reference.offset];
	}

	FSNode *node = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

	if (!node) {
		EsHeapFree(node, 0, K_FIXED);
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	_node->driverNode = node;
	node->volume = volume;
	node->entry = entry;

	return ES_SUCCESS;
}

static size_t Read(KNode *node, void *_buffer, EsFileOffset offset, EsFileOffset count) {
#define READ_FAILURE(message, error) do { KernelLog(LOG_ERROR, "FAT", "read failure", "Read - " message); return error; } while (0)

	FSNode *file = (FSNode *) node->driverNode;
	Volume *volume = file->volume;
	SuperBlockCommon *superBlock = &volume->superBlock;

	uint8_t *clusterBuffer = (uint8_t *) EsHeapAllocate(superBlock->sectorsPerCluster * SECTOR_SIZE, false, K_FIXED);
	EsDefer(EsHeapFree(clusterBuffer, 0, K_FIXED));
	if (!clusterBuffer) READ_FAILURE("Could not allocate cluster buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);

	uint8_t *outputBuffer = (uint8_t *) _buffer;
	uint64_t firstCluster = offset / (SECTOR_SIZE * superBlock->sectorsPerCluster);
	uint32_t currentCluster = file->entry.firstClusterLow + (file->entry.firstClusterHigh << 16);
	for (uintptr_t i = 0; i < firstCluster; i++) currentCluster = NextCluster(volume, currentCluster);
	offset %= (SECTOR_SIZE * superBlock->sectorsPerCluster);

	while (count) {
		uint32_t bytesFromThisCluster = superBlock->sectorsPerCluster * SECTOR_SIZE - offset;
		if (bytesFromThisCluster > count) bytesFromThisCluster = count;

		EsError error = volume->Access((currentCluster * superBlock->sectorsPerCluster + volume->sectorOffset) * SECTOR_SIZE, 
				superBlock->sectorsPerCluster * SECTOR_SIZE, K_ACCESS_READ, 
				(uint8_t *) clusterBuffer, ES_FLAGS_DEFAULT);
		if (error != ES_SUCCESS) READ_FAILURE("Could not read cluster.\n", error);

		EsMemoryCopy(outputBuffer, clusterBuffer + offset, bytesFromThisCluster);
		count -= bytesFromThisCluster, outputBuffer += bytesFromThisCluster, offset = 0;
		currentCluster = NextCluster(volume, currentCluster);
	}

	return true;
}

static EsError Scan(const char *_name, size_t nameLength, KNode *node) {
#define SCAN_FAILURE(message, error) do { KernelLog(LOG_ERROR, "FAT", "scan failure", "Scan - " message); return error; } while (0)

	uint8_t name[] = "           "; 

	{
		uintptr_t i = 0, j = 0;
		bool inExtension = false;

		while (i < nameLength) {
			if (j == 11) return ES_ERROR_FILE_DOES_NOT_EXIST; // Name too long.
			uint8_t c = _name[i++];
			if (c == '.' && !inExtension) j = 8, inExtension = true;
			else name[j++] = (c >= 'a' && c <= 'z') ? (c + 'A' - 'a') : c;
		}
	}

	FSNode *directory = (FSNode *) node->driverNode;
	Volume *volume = directory->volume;
	SuperBlockCommon *superBlock = &volume->superBlock;

	uint8_t *clusterBuffer = (uint8_t *) EsHeapAllocate(superBlock->sectorsPerCluster * SECTOR_SIZE, false, K_FIXED);
	EsDefer(EsHeapFree(clusterBuffer, 0, K_FIXED));
	if (!clusterBuffer) SCAN_FAILURE("Could not allocate cluster buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);

	uint32_t currentCluster = directory->entry.firstClusterLow + (directory->entry.firstClusterHigh << 16);
	uintptr_t directoryPosition = 0;

	while (currentCluster < volume->terminateCluster) {
		if (!directory->rootDirectory) {
			EsError error = volume->Access((currentCluster * superBlock->sectorsPerCluster + volume->sectorOffset) * SECTOR_SIZE, 
					superBlock->sectorsPerCluster * SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) clusterBuffer, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) SCAN_FAILURE("Could not read cluster.\n", error);
		}

		for (uintptr_t i = 0; i < superBlock->sectorsPerCluster * SECTOR_SIZE / sizeof(FATDirectoryEntry); i++, directoryPosition++) {
			FATDirectoryEntry *entry = directory->rootDirectory ? (directory->rootDirectory + directoryPosition) : ((FATDirectoryEntry *) clusterBuffer + i);
			if (entry->name[0] == 0xE5 || entry->attributes == 0x0F || (entry->attributes & 8)) goto nextEntry;
			if (!entry->name[0]) return ES_ERROR_FILE_DOES_NOT_EXIST;

			for (uintptr_t j = 0; j < 11; j++) {
				uint8_t c = entry->name[j];

				if (name[j] != ((c >= 'a' && c <= 'z') ? (c + 'A' - 'a') : c)) {
					goto nextEntry;
				}
			}

			{
				KNodeMetadata metadata = {};
				metadata.type = (entry->attributes & 0x10) ? ES_NODE_DIRECTORY : ES_NODE_FILE;

				if (metadata.type == ES_NODE_FILE) {
					metadata.totalSize = entry->fileSizeBytes;
				} else if (metadata.type == ES_NODE_DIRECTORY) {
					uint32_t currentCluster = entry->firstClusterLow + (entry->firstClusterHigh << 16);

					while (currentCluster < volume->terminateCluster) {
						currentCluster = NextCluster(volume, currentCluster);
						metadata.directoryChildren += SECTOR_SIZE * superBlock->sectorsPerCluster / sizeof(FATDirectoryEntry);
					}
				}

				DirectoryEntryReference reference = {};
				reference.cluster = directory->rootDirectory ? 0 : currentCluster;
				reference.offset = directory->rootDirectory ? directoryPosition : i;
				return FSDirectoryEntryFound(node, &metadata, &reference, _name, nameLength, false);
			}

			nextEntry:;
		}

		if (!directory->rootDirectory) {
			currentCluster = NextCluster(volume, currentCluster);
		}
	}

	return ES_ERROR_FILE_DOES_NOT_EXIST;
}

static EsError Enumerate(KNode *node) {
#define ENUMERATE_FAILURE(message, error) do { KernelLog(LOG_ERROR, "FAT", "enumerate failure", "Enumerate - " message); return error; } while (0)

	FSNode *directory = (FSNode *) node->driverNode;
	Volume *volume = directory->volume;
	SuperBlockCommon *superBlock = &volume->superBlock;

	uint8_t *clusterBuffer = (uint8_t *) EsHeapAllocate(superBlock->sectorsPerCluster * SECTOR_SIZE, false, K_FIXED);
	EsDefer(EsHeapFree(clusterBuffer, 0, K_FIXED));
	if (!clusterBuffer) ENUMERATE_FAILURE("Could not allocate cluster buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);

	uint32_t currentCluster = directory->entry.firstClusterLow + (directory->entry.firstClusterHigh << 16);
	uint64_t directoryPosition = 0;

	while (currentCluster < volume->terminateCluster) {
		if (!directory->rootDirectory) {
			EsError error = volume->Access((currentCluster * superBlock->sectorsPerCluster + volume->sectorOffset) * SECTOR_SIZE, 
					superBlock->sectorsPerCluster * SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) clusterBuffer, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) ENUMERATE_FAILURE("Could not read cluster.\n", error);
		}

		for (uintptr_t i = 0; i < superBlock->sectorsPerCluster * SECTOR_SIZE / sizeof(FATDirectoryEntry); i++, directoryPosition++) {
			FATDirectoryEntry *entry = directory->rootDirectory ? (directory->rootDirectory + directoryPosition) : ((FATDirectoryEntry *) clusterBuffer + i);
			if (entry->name[0] == 0xE5 || entry->attributes == 0x0F || (entry->attributes & 8)) continue;

			if (!entry->name[0]) {
				return ES_SUCCESS;
			}

			uint8_t name[12];
			size_t nameLength = 0;
			bool hasExtension = entry->name[8] != ' ' || entry->name[9] != ' ' || entry->name[10] != ' ';

			if (entry->name[0] == '.' && (entry->name[1] == '.' || entry->name[1] == ' ') && entry->name[2] == ' ') {
				continue;
			}

			for (uintptr_t i = 0; i < 11; i++) {
				if (i == 8 && hasExtension) name[nameLength++] = '.';
				if (entry->name[i] != ' ') name[nameLength++] = entry->name[i];
			}

			KNodeMetadata metadata = {};

			metadata.type = (entry->attributes & 0x10) ? ES_NODE_DIRECTORY : ES_NODE_FILE;

			if (metadata.type == ES_NODE_DIRECTORY) {
				metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
			} else if (metadata.type == ES_NODE_FILE) {
				metadata.totalSize = entry->fileSizeBytes;
			}

			DirectoryEntryReference reference = {};
			reference.cluster = directory->rootDirectory ? 0 : currentCluster;
			reference.offset = directory->rootDirectory ? directoryPosition : i;

			EsError error = FSDirectoryEntryFound(node, &metadata, &reference, 
					(const char *) name, nameLength, false);

			if (error != ES_SUCCESS) {
				return error;
			}
		}

		if (!directory->rootDirectory) {
			currentCluster = NextCluster(volume, currentCluster);
		}
	}

	return ES_SUCCESS;
}

static bool Mount(Volume *volume) {
#define MOUNT_FAILURE(message) do { KernelLog(LOG_ERROR, "FAT", "mount failure", "Mount - " message); goto failure; } while (0)

	{
		EsError error;
		SuperBlockCommon *superBlock = &volume->superBlock;
		error = volume->Access(0, SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) superBlock, ES_FLAGS_DEFAULT); 
		if (error != ES_SUCCESS) MOUNT_FAILURE("Could not read super block.\n");

		uint32_t sectorCount = superBlock->totalSectors ?: superBlock->largeSectorCount;
		uint32_t clusterCount = sectorCount / superBlock->sectorsPerCluster;
		uint32_t sectorsPerFAT = 0;

		if (clusterCount < 0x00000FF5) {
			volume->type = TYPE_FAT12;
			volume->terminateCluster = 0xFF8;
			sectorsPerFAT = volume->sb16.sectorsPerFAT16;
		} else if (clusterCount < 0x0000FFF5) {
			volume->type = TYPE_FAT16;
			volume->terminateCluster = 0xFFF8;
			sectorsPerFAT = volume->sb16.sectorsPerFAT16;
		} else if (clusterCount < 0x0FFFFFF5) {
			volume->type = TYPE_FAT32;
			volume->terminateCluster = 0xFFFFFF8;
			sectorsPerFAT = volume->sb32.sectorsPerFAT32;
		} else {
			MOUNT_FAILURE("Unsupported cluster count. Maybe ExFAT?\n");
		}

		uint32_t rootDirectoryOffset = superBlock->reservedSectors + superBlock->fatCount * sectorsPerFAT;
		uint32_t rootDirectorySectors = (superBlock->rootDirectoryEntries * sizeof(FATDirectoryEntry) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;

		volume->sectorOffset = rootDirectoryOffset + rootDirectorySectors - 2 * superBlock->sectorsPerCluster;

		volume->fat = (uint8_t *) EsHeapAllocate(sectorsPerFAT * SECTOR_SIZE, true, K_FIXED);
		if (!volume->fat) MOUNT_FAILURE("Could not allocate FAT.\n");
		error = volume->Access(superBlock->reservedSectors * SECTOR_SIZE, sectorsPerFAT * SECTOR_SIZE, K_ACCESS_READ, volume->fat, ES_FLAGS_DEFAULT);
		if (error != ES_SUCCESS) MOUNT_FAILURE("Could not read FAT.\n");

		volume->spaceUsed = CountUsedClusters(volume) * superBlock->sectorsPerCluster * superBlock->bytesPerSector;
		volume->spaceTotal = volume->block->information.sectorSize * volume->block->information.sectorCount;

		volume->rootDirectory->driverNode = EsHeapAllocate(sizeof(FSNode), true, K_FIXED);
		if (!volume->rootDirectory->driverNode) MOUNT_FAILURE("Could not allocate root node.\n");

		FSNode *root = (FSNode *) volume->rootDirectory->driverNode;
		root->volume = volume;

		if (volume->type == TYPE_FAT32) {
			root->entry.firstClusterLow = volume->sb32.rootDirectoryCluster & 0xFFFF;
			root->entry.firstClusterHigh = (volume->sb32.rootDirectoryCluster >> 16) & 0xFFFF;

			uint32_t currentCluster = volume->sb32.rootDirectoryCluster;

			while (currentCluster < volume->terminateCluster) {
				currentCluster = NextCluster(volume, currentCluster);
				volume->rootDirectoryInitialChildren += SECTOR_SIZE * superBlock->sectorsPerCluster / sizeof(FATDirectoryEntry);
			}
		} else {
			root->rootDirectory = (FATDirectoryEntry *) EsHeapAllocate(rootDirectorySectors * SECTOR_SIZE, true, K_FIXED);
			volume->rootDirectoryEntries = root->rootDirectory;

			error = volume->Access(rootDirectoryOffset * SECTOR_SIZE, rootDirectorySectors * SECTOR_SIZE, 
					K_ACCESS_READ, (uint8_t *) root->rootDirectory, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) MOUNT_FAILURE("Could not read root directory.\n");

			for (uintptr_t i = 0; i < superBlock->rootDirectoryEntries; i++) {
				if (root->rootDirectory[i].name[0] == 0xE5 || root->rootDirectory[i].attributes == 0x0F || (root->rootDirectory[i].attributes & 0x08)) continue;
				else if (root->rootDirectory[i].name[0] == 0x00) break;
				else volume->rootDirectoryInitialChildren++;
			}
		}

		return true;
	}

	failure:
	if (volume->root && volume->root->driverNode) EsHeapFree(((FSNode *) volume->root->driverNode)->rootDirectory, 0, K_FIXED);
	if (volume->root) EsHeapFree(volume->root->driverNode, 0, K_FIXED);
	EsHeapFree(volume->root, 0, K_FIXED);
	EsHeapFree(volume->fat, 0, K_FIXED);
	return false;
}

static void Close(KNode *node) {
	EsHeapFree(node->driverNode, sizeof(FSNode), K_FIXED);
}

static void DeviceAttach(KDevice *parent) {
	Volume *volume = (Volume *) KDeviceCreate("FAT", parent, sizeof(Volume));

	if (!volume || !FSFileSystemInitialise(volume)) {
		KernelLog(LOG_ERROR, "FAT", "allocate error", "DeviceAttach - Could not initialise volume.\n");
		return;
	}

	if (volume->block->information.sectorSize != SECTOR_SIZE) {
		KernelLog(LOG_ERROR, "FAT", "mount failure", "DeviceAttach - Unsupported sector size.\n");
		KDeviceDestroy(volume);
		return;
	}

	if (!Mount(volume)) {
		KernelLog(LOG_ERROR, "FAT", "mount failure", "DeviceAttach - Could not mount FAT volume.\n");
		KDeviceDestroy(volume);
		return;
	}

	volume->read = Read;
	volume->load = Load;
	volume->scan = Scan;
	volume->enumerate = Enumerate;
	volume->close = Close;

	if (volume->type == TYPE_FAT32) {
		volume->nameBytes = sizeof(volume->sb32.label);
		EsMemoryCopy(volume->name, volume->sb32.label, volume->nameBytes);
	} else {
		if ((volume->rootDirectoryEntries[0].attributes & 8) && (volume->rootDirectoryEntries[0].attributes != 0x0F)) {
			volume->nameBytes = sizeof(volume->rootDirectoryEntries[0].name);
			EsMemoryCopy(volume->name, volume->rootDirectoryEntries[0].name, volume->nameBytes);
		} else {
			volume->nameBytes = sizeof(volume->sb16.label);
			EsMemoryCopy(volume->name, volume->sb16.label, volume->nameBytes);
		}
	}

	volume->directoryEntryDataBytes = sizeof(DirectoryEntryReference);
	volume->nodeDataBytes = sizeof(FSNode);
	EsMemoryCopy(&volume->identifier, volume->type == TYPE_FAT32 ? &volume->sb32.serial : &volume->sb16.serial, sizeof(uint32_t));

	FSRegisterFileSystem(volume); 
}

KDriver driverFAT = {
	.attach = DeviceAttach,
};
