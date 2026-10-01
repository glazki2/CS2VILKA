#include "plat.h"
#include "module.h"

#include "tier0/memdbgon.h"

void Plat_WriteMemory(void *pPatchAddress, uint8_t *pPatch, int iPatchSize)
{
	WriteProcessMemory(GetCurrentProcess(), pPatchAddress, (void *)pPatch, iPatchSize, nullptr);
}

void CModule::InitializeSections()
{
	IMAGE_DOS_HEADER *pDosHeader = reinterpret_cast<IMAGE_DOS_HEADER *>(m_hModule);
	IMAGE_NT_HEADERS *pNtHeader = reinterpret_cast<IMAGE_NT_HEADERS64 *>(reinterpret_cast<uintptr_t>(m_hModule) + pDosHeader->e_lfanew);

	IMAGE_SECTION_HEADER *pSectionHeader = IMAGE_FIRST_SECTION(pNtHeader);

	for (int i = 0; i < pNtHeader->FileHeader.NumberOfSections; i++)
	{
		Section section;
		section.m_szName = (char *)pSectionHeader[i].Name;
		section.m_pBase = (void *)((uint8_t *)m_base + pSectionHeader[i].VirtualAddress);
		section.m_iSize = pSectionHeader[i].SizeOfRawData;

		m_sections.push_back(std::move(section));
	}
}

void *CModule::FindOriginalSignature(const byte *pData, size_t iSigLength, int &error)
{
	error = SIG_NOT_FOUND;
	if (!m_hModule || !m_base || !pData || iSigLength == 0)
	{
		return nullptr;
	}

	wchar_t path[MAX_PATH];
	const DWORD pathLength = GetModuleFileNameW(m_hModule, path, ARRAYSIZE(path));
	if (!pathLength || pathLength == ARRAYSIZE(path))
	{
		return nullptr;
	}

	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	LARGE_INTEGER fileSize {};
	if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0 || fileSize.HighPart != 0)
	{
		CloseHandle(file);
		return nullptr;
	}

	std::vector<byte> image(static_cast<size_t>(fileSize.QuadPart));
	DWORD bytesRead = 0;
	if (!ReadFile(file, image.data(), static_cast<DWORD>(image.size()), &bytesRead, nullptr) || bytesRead != image.size())
	{
		CloseHandle(file);
		return nullptr;
	}
	CloseHandle(file);

	if (image.size() < sizeof(IMAGE_DOS_HEADER))
	{
		return nullptr;
	}
	auto *fileDos = reinterpret_cast<IMAGE_DOS_HEADER *>(image.data());
	if (fileDos->e_magic != IMAGE_DOS_SIGNATURE || fileDos->e_lfanew < 0
		|| static_cast<size_t>(fileDos->e_lfanew) > image.size() - sizeof(IMAGE_NT_HEADERS64))
	{
		return nullptr;
	}
	auto *fileNt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image.data() + fileDos->e_lfanew);
	if (fileNt->Signature != IMAGE_NT_SIGNATURE || fileNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		return nullptr;
	}

	auto *loadedDos = reinterpret_cast<IMAGE_DOS_HEADER *>(m_base);
	auto *loadedNt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(reinterpret_cast<byte *>(m_base) + loadedDos->e_lfanew);
	if (loadedDos->e_magic != IMAGE_DOS_SIGNATURE || loadedNt->Signature != IMAGE_NT_SIGNATURE
		|| loadedNt->FileHeader.TimeDateStamp != fileNt->FileHeader.TimeDateStamp
		|| loadedNt->FileHeader.NumberOfSections != fileNt->FileHeader.NumberOfSections
		|| loadedNt->OptionalHeader.SizeOfImage != fileNt->OptionalHeader.SizeOfImage || loadedNt->OptionalHeader.SizeOfImage != m_size)
	{
		return nullptr;
	}

	auto *fileSections = IMAGE_FIRST_SECTION(fileNt);
	auto *loadedSections = IMAGE_FIRST_SECTION(loadedNt);
	const size_t sectionTableOffset = reinterpret_cast<byte *>(fileSections) - image.data();
	const size_t sectionCount = fileNt->FileHeader.NumberOfSections;
	if (sectionTableOffset > image.size() || sectionCount > (image.size() - sectionTableOffset) / sizeof(IMAGE_SECTION_HEADER))
	{
		return nullptr;
	}

	void *matchAddress = nullptr;
	for (size_t i = 0; i < sectionCount; ++i)
	{
		const auto &section = fileSections[i];
		const auto &loadedSection = loadedSections[i];
		if (section.VirtualAddress != loadedSection.VirtualAddress || section.Misc.VirtualSize != loadedSection.Misc.VirtualSize
			|| section.PointerToRawData != loadedSection.PointerToRawData || section.SizeOfRawData != loadedSection.SizeOfRawData
			|| section.Characteristics != loadedSection.Characteristics)
		{
			return nullptr;
		}
		if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || section.SizeOfRawData < iSigLength || section.PointerToRawData > image.size()
			|| section.SizeOfRawData > image.size() - section.PointerToRawData)
		{
			continue;
		}

		byte *sectionData = image.data() + section.PointerToRawData;
		SignatureIterator signatures(sectionData, section.SizeOfRawData, pData, iSigLength);
		while (void *match = signatures.FindNext(true))
		{
			const size_t sectionOffset = static_cast<byte *>(match) - sectionData;
			const size_t imageOffset = static_cast<size_t>(section.VirtualAddress) + sectionOffset;
			if (imageOffset >= m_size || iSigLength > m_size - imageOffset)
			{
				continue;
			}
			if (matchAddress)
			{
				error = SIG_FOUND_MULTIPLE;
				return matchAddress;
			}
			matchAddress = static_cast<byte *>(m_base) + imageOffset;
		}
	}

	if (matchAddress)
	{
		error = SIG_OK;
	}
	return matchAddress;
}

void *CModule::FindVirtualTable(const std::string &name)
{
	auto runTimeData = GetSection(".data");
	auto readOnlyData = GetSection(".rdata");

	if (!runTimeData || !readOnlyData)
	{
		return nullptr;
	}

	std::string decoratedTableName = ".?AV" + name + "@@";

	SignatureIterator sigIt(runTimeData->m_pBase, runTimeData->m_iSize, (const byte *)decoratedTableName.c_str(), decoratedTableName.size() + 1);
	void *typeDescriptor = sigIt.FindNext(false);

	if (!typeDescriptor)
	{
		return nullptr;
	}

	typeDescriptor = (void *)((uintptr_t)typeDescriptor - 0x10);

	const uint32_t rttiTDRva = (uintptr_t)typeDescriptor - (uintptr_t)m_base;

	SignatureIterator sigIt2(readOnlyData->m_pBase, readOnlyData->m_iSize, (const byte *)&rttiTDRva, sizeof(uint32_t));

	while (void *completeObjectLocator = sigIt2.FindNext(false))
	{
		auto completeObjectLocatorHeader = (uintptr_t)completeObjectLocator - 0xC;
		// check RTTI Complete Object Locator header, always 0x1
		if (*(int32_t *)(completeObjectLocatorHeader) != 1)
		{
			continue;
		}

		// check RTTI Complete Object Locator vtable offset
		if (*(int32_t *)((uintptr_t)completeObjectLocator - 0x8) != 0)
		{
			continue;
		}

		SignatureIterator sigIt3(readOnlyData->m_pBase, readOnlyData->m_iSize, (const byte *)&completeObjectLocatorHeader, sizeof(void *));
		void *vtable = sigIt3.FindNext(false);

		if (!vtable)
		{
			return nullptr;
		}

		return (void *)((uintptr_t)vtable + 0x8);
	}

	return nullptr;
}
