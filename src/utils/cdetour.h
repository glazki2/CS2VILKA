#pragma once

#include "funchook.h"
#include "module.h"
#include "utlvector.h"
#include "gameconfig.h"

class CDetourBase
{
public:
	virtual ~CDetourBase() = default;
	virtual const char *GetName() = 0;
	virtual bool CreateDetour(CGameConfig *gameConfig) = 0;
	virtual bool EnableDetour() = 0;
	virtual bool DisableDetour() = 0;
	virtual bool FreeDetour() = 0;
	virtual bool IsInstalled() const = 0;
};

template<typename T>
class CDetour : public CDetourBase
{
public:
	CDetour(T *pfnDetour, const char *pszName) : m_pfnDetour(pfnDetour), m_pszName(pszName)
	{
		m_hook = nullptr;
		m_bInstalled = false;
		m_pSignature = nullptr;
		m_pSymbol = nullptr;
		m_pModule = nullptr;
		m_pfnFunc = nullptr;
	}

	bool CreateDetour(CGameConfig *gameConfig) override;
	bool EnableDetour() override;
	bool DisableDetour() override;
	bool FreeDetour() override;

	bool IsInstalled() const override
	{
		return m_bInstalled;
	}

	const char *GetName() override
	{
		return m_pszName;
	}

	T *GetFunc()
	{
		return m_pfnFunc;
	}

	// Shorthand for calling original.
	template<typename... Args>
	auto operator()(Args &&...args)
	{
		return std::invoke(m_pfnFunc, std::forward<Args>(args)...);
	}

private:
	CModule **m_pModule;
	T *m_pfnDetour;
	const char *m_pszName;
	byte *m_pSignature;
	const char *m_pSymbol;
	T *m_pfnFunc;
	funchook_t *m_hook;
	bool m_bInstalled;
};

extern CUtlVector<CDetourBase *> g_vecDetours;

template<typename T>
bool CDetour<T>::CreateDetour(CGameConfig *gameConfig)
{
	if (m_hook)
	{
		return true;
	}
	m_pfnFunc = (T *)gameConfig->ResolveFunctionSignature(m_pszName);
	if (!m_pfnFunc)
	{
		return false;
	}

	m_hook = funchook_create();
	if (!m_hook)
	{
		return false;
	}
	if (funchook_prepare(m_hook, (void **)&m_pfnFunc, (void *)m_pfnDetour) != FUNCHOOK_ERROR_SUCCESS)
	{
		funchook_destroy(m_hook);
		m_hook = nullptr;
		return false;
	}

	g_vecDetours.AddToTail(this);
	return true;
}

template<typename T>
bool CDetour<T>::EnableDetour()
{
	if (m_bInstalled)
	{
		return true;
	}
	if (!m_hook)
	{
		return false;
	}
	if (funchook_install(m_hook, 0) != FUNCHOOK_ERROR_SUCCESS)
	{
		return false;
	}
	m_bInstalled = true;
	return true;
}

template<typename T>
bool CDetour<T>::DisableDetour()
{
	if (!m_bInstalled)
	{
		return true;
	}
	if (funchook_uninstall(m_hook, 0) != FUNCHOOK_ERROR_SUCCESS)
	{
		return false;
	}
	m_bInstalled = false;
	return true;
}

template<typename T>
bool CDetour<T>::FreeDetour()
{
	if (!DisableDetour())
	{
		return false;
	}
	const bool destroyed = !m_hook || funchook_destroy(m_hook) == FUNCHOOK_ERROR_SUCCESS;
	m_hook = nullptr;
	m_pfnFunc = nullptr;
	return destroyed;
}

#define DECLARE_DETOUR(name, detour) CDetour<decltype(detour)> name(detour, #name)

#define INIT_DETOUR(config, name) (name.CreateDetour(config) && name.EnableDetour())

bool FlushAllDetours();
