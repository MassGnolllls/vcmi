/*
 * main.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FairAI.h"
#include "Combat/FairBattleAI.h"

#ifdef __GNUC__
#define strcpy_s(a, b, c) strncpy(a, c, b)
#endif

static const char * const g_cszAiName = "FairAI";

extern "C" DLL_EXPORT int GetGlobalAiVersion()
{
	return AI_INTERFACE_VER;
}

extern "C" DLL_EXPORT void GetAiName(char * name)
{
	strcpy_s(name, strlen(g_cszAiName) + 1, g_cszAiName);
}

// One library for both roles: adventure AI and combat AI. The combat AI can also be
// selected on its own (e.g. as combat AI for other adventure AIs or in arena battles).
extern "C" DLL_EXPORT void GetNewAI(std::shared_ptr<CGlobalAI> & out)
{
	out = std::make_shared<FAI::FairAIGateway>();
}

extern "C" DLL_EXPORT void GetNewBattleAI(std::shared_ptr<CBattleGameInterface> & out)
{
	out = std::make_shared<FAI::Combat::FairBattleAI>();
}
