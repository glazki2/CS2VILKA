#include <cstdint>
#include <memory>
#include "gameconfig.h"
#include "addresses.h"

CGameConfig::CGameConfig(const std::string &gameDir, const std::string &path)
{
	this->m_szGameDir = gameDir;
	this->m_szPath = path;
	this->m_pKeyValues = new KeyValues("Games");
}

CGameConfig::~CGameConfig()
{
	delete m_pKeyValues;
}

bool CGameConfig::Init(IFileSystem *filesystem, char *conf_error, int conf_error_size)
{
	if (!m_pKeyValues->LoadFromFile(filesystem, m_szPath.c_str(), nullptr))
	{
		snprintf(conf_error, conf_error_size, "the file is missing or cannot be read");
		return false;
	}

	KeyValues *game = m_pKeyValues->FindKey(m_szGameDir.c_str());
	if (game)
	{
#if defined _LINUX
		const char *platform = "linux";
#else
		const char *platform = "windows";
#endif

		KeyValues *offsets = game->FindKey("Offsets");
		if (offsets)
		{
			FOR_EACH_SUBKEY(offsets, it)
			{
				m_umOffsets[it->GetName()] = it->GetInt(platform, -1);
			}
		}

		KeyValues *signatures = game->FindKey("Signatures");
		if (signatures)
		{
			FOR_EACH_SUBKEY(signatures, it)
			{
				m_umLibraries[it->GetName()] = std::string(it->GetString("library"));
				m_umSignatures[it->GetName()] = std::string(it->GetString(platform));
				m_umAllowMultiMatch[it->GetName()] = it->GetBool("allow_multi_match", false);
			}
		}

		KeyValues *patches = game->FindKey("Patches");
		if (patches)
		{
			FOR_EACH_SUBKEY(patches, it)
			{
				m_umPatches[it->GetName()] = std::string(it->GetString(platform));
			}
		}
	}
	else
	{
		snprintf(conf_error, conf_error_size, "there is no section for this server build");
		return false;
	}
	return true;
}

const std::string CGameConfig::GetPath()
{
	return m_szPath;
}

const char *CGameConfig::GetSignature(const std::string &name)
{
	auto it = m_umSignatures.find(name);
	if (it == m_umSignatures.end())
	{
		return nullptr;
	}
	return it->second.c_str();
}

const char *CGameConfig::GetPatch(const std::string &name)
{
	auto it = m_umPatches.find(name);
	if (it == m_umPatches.end())
	{
		return nullptr;
	}
	return it->second.c_str();
}

int CGameConfig::GetOffset(const std::string &name)
{
	auto it = m_umOffsets.find(name);
	if (it == m_umOffsets.end())
	{
		return -1;
	}
	return it->second;
}

const char *CGameConfig::GetLibrary(const std::string &name)
{
	auto it = m_umLibraries.find(name);
	if (it == m_umLibraries.end())
	{
		return nullptr;
	}
	return it->second.c_str();
}

CModule **CGameConfig::GetModule(const char *name)
{
	const char *library = this->GetLibrary(name);
	if (!library)
	{
		return nullptr;
	}

	if (strcmp(library, "engine") == 0)
	{
		return &modules::engine;
	}
	else if (strcmp(library, "server") == 0)
	{
		return &modules::server;
	}
	else if (strcmp(library, "tier0") == 0)
	{
		return &modules::tier0;
	}
	else if (strcmp(library, "schemasystem") == 0)
	{
		return &modules::schemasystem;
	}
	return nullptr;
}

bool CGameConfig::IsSymbol(const char *name)
{
	const char *sigOrSymbol = this->GetSignature(name);
	if (!sigOrSymbol || strlen(sigOrSymbol) <= 0)
	{
		return false;
	}
	return sigOrSymbol[0] == '@';
}

const char *CGameConfig::GetSymbol(const char *name)
{
	const char *symbol = this->GetSignature(name);

	if (!symbol || strlen(symbol) <= 1)
	{
		return nullptr;
	}
	return symbol + 1;
}

void *CGameConfig::ResolveSignature(const char *name)
{
	return ResolveSignature(name, false);
}

void *CGameConfig::ResolveFunctionSignature(const char *name)
{
	return ResolveSignature(name, true);
}

void *CGameConfig::ResolveSignature(const char *name, bool useOriginalImage)
{
	CModule **module = this->GetModule(name);
	if (!module || !(*module) || !(*module)->m_hModule || !(*module)->m_base || !(*module)->m_size)
	{
		return nullptr;
	}
	int error = SIG_OK;
	void *address = nullptr;
	if (this->IsSymbol(name))
	{
		const char *symbol = this->GetSymbol(name);
		if (!symbol)
		{
			return nullptr;
		}
		address = dlsym((*module)->m_hModule, symbol);
	}
	else
	{
		const char *signature = this->GetSignature(name);
		if (!signature)
		{
			return nullptr;
		}

		size_t iLength = 0;
		std::unique_ptr<byte[]> pSignature(HexToByte(signature, iLength));
		if (!pSignature)
		{
			return nullptr;
		}
		address = (*module)->FindSignature(pSignature.get(), iLength, error);
		// Another plugin may already have replaced the function entry. The platform resolver verifies the exact mapped image before recovering it.
		if (!address && error == SIG_NOT_FOUND && useOriginalImage)
		{
			address = (*module)->FindOriginalSignature(pSignature.get(), iLength, error);
		}
		if (error == SIG_FOUND_MULTIPLE && !m_umAllowMultiMatch[name])
		{
			return nullptr;
		}
	}

	if (!address)
	{
		return nullptr;
	}
	return address;
}

void *CGameConfig::ResolveSignatureFromMov(const char *name)
{
	// Convoluted way of having GameEventManager regardless of lateloading
	u8 *ptr = (u8 *)this->ResolveSignature(name);

	if (!ptr)
	{
		return nullptr;
	}

	ptr += 3;
	// x86-64 RIP displacements are signed 32-bit values.
	i32 offset {};
	std::memcpy(&offset, ptr, sizeof(offset));

	// Go to the next instruction, which is the starting point of the relative jump
	ptr += 4;

	// Now grab our pointer
	return (void *)(ptr + offset);
}

// Static functions
std::string CGameConfig::GetDirectoryName(const std::string &directoryPathInput)
{
	std::string directoryPath = std::string(directoryPathInput);

	size_t found = std::string(directoryPath).find_last_of("/\\");
	if (found != std::string::npos)
	{
		return std::string(directoryPath, found + 1);
	}
	return "";
}

int CGameConfig::HexStringToUint8Array(const char *hexString, uint8_t *byteArray, size_t maxBytes)
{
	if (!hexString)
	{
		return -1;
	}

	size_t hexStringLength = strlen(hexString);
	size_t byteCount = hexStringLength / 4; // Each "\\x" represents one byte.

	if (hexStringLength % 4 != 0 || byteCount == 0 || byteCount > maxBytes)
	{
		return -1; // Return an error code.
	}

	for (size_t i = 0; i < hexStringLength; i += 4)
	{
		if (sscanf(hexString + i, "\\x%2hhX", &byteArray[i / 4]) != 1)
		{
			return -1; // Return an error code.
		}
	}

	byteArray[byteCount] = '\0'; // Add a null-terminating character.

	return byteCount; // Return the number of bytes successfully converted.
}

byte *CGameConfig::HexToByte(const char *src, size_t &length)
{
	if (!src || strlen(src) <= 0)
	{
		return nullptr;
	}

	length = strlen(src) / 4;
	uint8_t *dest = new uint8_t[length + 1];
	int byteCount = HexStringToUint8Array(src, dest, length);
	if (byteCount <= 0)
	{
		delete[] dest;
		return nullptr;
	}
	return (byte *)dest;
}
