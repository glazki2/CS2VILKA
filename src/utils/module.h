#pragma once
#include "../common.h"
#include "dbg.h"
#include "interface.h"
#include "strtools.h"
#include "plat.h"

#include <string>
#include <vector>

#ifdef _WIN32
#include <Psapi.h>
#endif

enum SigError
{
	SIG_OK,
	SIG_NOT_FOUND,
	SIG_FOUND_MULTIPLE,
};

// equivalent to FindSignature, but allows for multiple signatures to be found and iterated over
class SignatureIterator
{
public:
	SignatureIterator(void *pBase, size_t iSize, const byte *pSignature, size_t iSigLength)
		: m_pBase((byte *)pBase), m_iSize(iSize), m_pSignature(pSignature), m_iSigLength(iSigLength)
	{
		m_pCurrent = m_pBase;
	}

	void *FindNext(bool allowWildcard)
	{
		if (!m_pBase || !m_pCurrent || !m_pSignature || m_iSigLength == 0)
		{
			return nullptr;
		}

		size_t consumed = static_cast<size_t>(m_pCurrent - m_pBase);
		if (consumed > m_iSize || m_iSigLength > m_iSize - consumed)
		{
			return nullptr;
		}

		for (size_t i = 0; i <= m_iSize - consumed - m_iSigLength; i++)
		{
			size_t Matches = 0;
			while (*(m_pCurrent + i + Matches) == m_pSignature[Matches] || (allowWildcard && m_pSignature[Matches] == '\x2A'))
			{
				Matches++;
				if (Matches == m_iSigLength)
				{
					m_pCurrent += i + 1;
					return m_pCurrent - 1;
				}
			}
		}

		return nullptr;
	}

private:
	byte *m_pBase;
	size_t m_iSize;
	const byte *m_pSignature;
	size_t m_iSigLength;
	byte *m_pCurrent;
};

class CModule
{
public:
	CModule(const char *path, const char *module) : m_pszModule(module), m_pszPath(path)
	{
		char szModule[MAX_PATH];

		V_snprintf(szModule, MAX_PATH, "%s%s%s%s%s", Plat_GetGameDirectory(), path, MODULE_PREFIX, m_pszModule, MODULE_EXT);

		m_hModule = dlmount(szModule);

		if (!m_hModule)
		{
			m_base = nullptr;
			m_size = 0;
			return;
		}
		m_base = nullptr;
		m_size = 0;

#ifdef _WIN32
		MODULEINFO m_hModuleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), m_hModule, &m_hModuleInfo, sizeof(m_hModuleInfo)))
		{
			return;
		}

		m_base = (void *)m_hModuleInfo.lpBaseOfDll;
		m_size = m_hModuleInfo.SizeOfImage;
		InitializeSections();
#else
		if (GetModuleInformation(m_hModule, &m_base, &m_size, m_sections))
		{
			return;
		}
#endif
	}

	~CModule()
	{
		if (m_hModule)
		{
			dlclose(m_hModule);
		}
	}

	CModule(const CModule &) = delete;
	CModule &operator=(const CModule &) = delete;

	void *FindSignature(const byte *pData, size_t iSigLength, int &error)
	{
		if (!m_base || !pData || iSigLength == 0 || iSigLength > m_size)
		{
			error = SIG_NOT_FOUND;
			return nullptr;
		}
		unsigned char *pMemory;
		void *return_addr = nullptr;
		error = 0;

		pMemory = (byte *)m_base;

		for (size_t i = 0; i <= m_size - iSigLength; i++)
		{
			size_t Matches = 0;
			while (*(pMemory + i + Matches) == pData[Matches] || pData[Matches] == '\x2A')
			{
				Matches++;
				if (Matches == iSigLength)
				{
					if (return_addr)
					{
						error = SIG_FOUND_MULTIPLE;
						return return_addr;
					}

					return_addr = (void *)(pMemory + i);
					break;
				}
			}
		}

		if (!return_addr)
		{
			error = SIG_NOT_FOUND;
		}

		return return_addr;
	}

	void *FindOriginalSignature(const byte *pData, size_t iSigLength, int &error);

	void *FindInterface(const char *name)
	{
		CreateInterfaceFn fn = (CreateInterfaceFn)dlsym(m_hModule, "CreateInterface");

		if (!fn)
		{
			return nullptr;
		}

		void *pInterface = fn(name, nullptr);

		if (!pInterface)
		{
			return nullptr;
		}

		return pInterface;
	}

	Section *GetSection(const std::string_view name)
	{
		for (auto &section : m_sections)
		{
			if (section.m_szName == name)
			{
				return &section;
			}
		}

		return nullptr;
	}
#ifdef _WIN32
	void InitializeSections();
#endif
	void *FindVirtualTable(const std::string &name);

public:
	const char *m_pszModule;
	const char *m_pszPath;
	HINSTANCE m_hModule;
	void *m_base;
	size_t m_size;
	std::vector<Section> m_sections;
};
