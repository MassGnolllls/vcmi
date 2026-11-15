set(fairAI_SRCS
		FairAI.cpp
		Status.cpp
		Perception/Perception.cpp
		Combat/CombatSim.cpp
		Combat/FairBattleAI.cpp
		Adventure/AdventureAI.cpp
)

# Components of VCMI's BattleAI reused by FairAI's combat module (hero spells, special units,
# engine-based enumeration of legal attacks). Compiled into FairAI so both libraries stay independent.
set(battleAIDir ${CMAKE_CURRENT_SOURCE_DIR}/../BattleAI)
list(APPEND fairAI_SRCS
		${battleAIDir}/AttackPossibility.cpp
		${battleAIDir}/BattleAI.cpp
		${battleAIDir}/BattleEvaluator.cpp
		${battleAIDir}/BattleExchangeVariant.cpp
		${battleAIDir}/EnemyInfo.cpp
		${battleAIDir}/PossibleSpellcast.cpp
		${battleAIDir}/PotentialTargets.cpp
		${battleAIDir}/SpellTargetsEvaluator.cpp
		${battleAIDir}/StackWithBonuses.cpp
		${battleAIDir}/ThreatMap.cpp
)

set(fairAI_HEADERS
		StdInc.h

		FairAI.h
		Status.h
		Perception/Perception.h
		Combat/CombatSim.h
		Combat/FairBattleAI.h
		Adventure/AdventureAI.h
		Adventure/Params.h
)

if(NOT ENABLE_STATIC_LIBS)
	list(APPEND fairAI_SRCS main.cpp StdInc.cpp)
endif()
assign_source_group(${fairAI_SRCS} ${fairAI_HEADERS})

if(ENABLE_STATIC_LIBS)
	add_library(FairAI STATIC ${fairAI_SRCS} ${fairAI_HEADERS})
else()
	add_library(FairAI SHARED ${fairAI_SRCS} ${fairAI_HEADERS})
	install(TARGETS FairAI RUNTIME DESTINATION ${AI_LIB_DIR} LIBRARY DESTINATION ${AI_LIB_DIR})
endif()

target_include_directories(FairAI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(FairAI PRIVATE vcmi TBB::tbb)

vcmi_set_output_dir(FairAI "AI")
enable_pch(FairAI)
