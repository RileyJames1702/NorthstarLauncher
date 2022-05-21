#include "pch.h"
#include "hooks.h"
#include "hookutils.h"
#include "masterserver.h"
#include "convar.h"
#include "r2client.h"

ConVar* Cvar_ns_has_agreed_to_send_token;

// mirrored in script
const int NOT_DECIDED_TO_SEND_TOKEN = 0;
const int AGREED_TO_SEND_TOKEN = 1;
const int DISAGREED_TO_SEND_TOKEN = 2;

typedef void (*AuthWithStryderType)(void* a1);
AuthWithStryderType AuthWithStryder;
void AuthWithStryderHook(void* a1)
{
	// game will call this forever, until it gets a valid auth key
	// so, we need to manually invalidate our key until we're authed with northstar, then we'll allow game to auth with stryder
	if (!g_MasterServerManager->m_bOriginAuthWithMasterServerDone && Cvar_ns_has_agreed_to_send_token->GetInt() != DISAGREED_TO_SEND_TOKEN)
	{
		// if player has agreed to send token and we aren't already authing, try to auth
		if (Cvar_ns_has_agreed_to_send_token->GetInt() == AGREED_TO_SEND_TOKEN &&
			!g_MasterServerManager->m_bOriginAuthWithMasterServerInProgress)
			g_MasterServerManager->AuthenticateOriginWithMasterServer(R2::g_LocalPlayerUserID, R2::g_LocalPlayerOriginToken);

		// invalidate key so auth will fail
		*R2::g_LocalPlayerOriginToken = 0;
	}

	AuthWithStryder(a1);
}

ON_DLL_LOAD_CLIENT_RELIESON("engine.dll", ClientAuthHooks, ConVar, [](HMODULE baseAddress)
{
	// this cvar will save to cfg once initially agreed with
	Cvar_ns_has_agreed_to_send_token = new ConVar(
		"ns_has_agreed_to_send_token",
		"0",
		FCVAR_ARCHIVE_PLAYERPROFILE,
		"whether the user has agreed to send their origin token to the northstar masterserver");

	HookEnabler hook;
	ENABLER_CREATEHOOK(hook, (char*)baseAddress + 0x1843A0, &AuthWithStryderHook, reinterpret_cast<LPVOID*>(&AuthWithStryder));
})