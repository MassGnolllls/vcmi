/*
 * Perception.cpp, part of VCMI engine - FairAI
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"
#include "Perception.h"

#include "../../../lib/callback/CCallback.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/gameState/InfoAboutArmy.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"

namespace FAI
{

// ---------------------------------------------------------------- ObservedArmy

std::pair<int, int> ObservedArmy::quantityRange(int quantityId)
{
	// Ranges as displayed by the game (Few, Several, Pack, Lots, Horde, Throng, Swarm, Zounds, Legion).
	// "Legion" is open-ended; 2000 is used as a pessimistic upper estimate.
	static const std::array<std::pair<int, int>, 10> ranges = {{
		{0, 2000}, // 0 = unknown
		{1, 4},
		{5, 9},
		{10, 19},
		{20, 49},
		{50, 99},
		{100, 249},
		{250, 499},
		{500, 999},
		{1000, 2000},
	}};

	if(quantityId < 0 || quantityId >= static_cast<int>(ranges.size()))
		return ranges[0];
	return ranges[quantityId];
}

ObservedArmy ObservedArmy::fromDescriptor(const ArmyDescriptor & descriptor)
{
	ObservedArmy result;
	result.known = true;

	for(const auto & slot : descriptor)
	{
		const CStackBasicDescriptor & stack = slot.second;
		if(stack.getId() == CreatureID::NONE)
			continue;

		ObservedStack observed;
		observed.creature = stack.getId();
		if(descriptor.isDetailed)
		{
			observed.exact = true;
			observed.minCount = observed.maxCount = stack.getCount();
		}
		else
		{
			auto range = quantityRange(stack.getCount());
			observed.minCount = range.first;
			observed.maxCount = range.second;
		}
		result.units.push_back(observed);
	}
	return result;
}

ObservedArmy ObservedArmy::exactOwn(const CArmedInstance * army)
{
	ObservedArmy result;
	result.known = true;
	for(const auto & slot : army->Slots())
	{
		ObservedStack observed;
		observed.creature = slot.second->getId();
		observed.minCount = observed.maxCount = slot.second->getCount();
		observed.exact = true;
		result.units.push_back(observed);
	}
	return result;
}

static uint64_t strengthOf(const ObservedStack & s, int count)
{
	const CCreature * creature = s.creature.toCreature();
	return creature ? static_cast<uint64_t>(creature->getAIValue()) * std::max(count, 0) : 0;
}

uint64_t ObservedArmy::minStrength() const
{
	uint64_t total = 0;
	for(const auto & s : units)
		total += strengthOf(s, s.minCount);
	return total;
}

uint64_t ObservedArmy::maxStrength() const
{
	uint64_t total = 0;
	for(const auto & s : units)
		total += strengthOf(s, s.maxCount);
	return total;
}

uint64_t ObservedArmy::expectedStrength() const
{
	uint64_t total = 0;
	for(const auto & s : units)
		total += strengthOf(s, (s.minCount + s.maxCount) / 2);
	return total;
}

// ---------------------------------------------------------------- Perception

Perception::Perception(std::shared_ptr<CCallback> cb, PlayerColor me)
	: cb(std::move(cb))
	, me(me)
{
}

std::vector<const CGHeroInstance *> Perception::ownHeroes() const
{
	return cb->getHeroesInfo();
}

std::vector<const CGTownInstance *> Perception::ownTowns() const
{
	return cb->getTownsInfo(true);
}

bool Perception::isOwn(const CGObjectInstance * obj) const
{
	return obj && obj->getOwner() == me;
}

bool Perception::isVisibleNow(const int3 & tile) const
{
	return cb->isVisible(tile);
}

ObservedArmy Perception::observeArmy(const CArmedInstance * army) const
{
	if(!army)
		return {};

	if(isOwn(army))
		return ObservedArmy::exactOwn(army);

	if(!cb->isVisible(army))
		return {};

	if(army->ID == Obj::HERO)
	{
		InfoAboutHero info;
		if(cb->getHeroInfo(army, info))
			return ObservedArmy::fromDescriptor(info.army);
		return {};
	}

	if(army->ID == Obj::TOWN)
	{
		InfoAboutTown info;
		if(cb->getTownInfo(army, info))
			return ObservedArmy::fromDescriptor(info.army);
		return {};
	}

	if(army->ID == Obj::MONSTER || army->ID == Obj::GARRISON || army->ID == Obj::GARRISON2)
	{
		// A human sees the creature type and the quantity category when inspecting these
		return ObservedArmy::fromDescriptor(ArmyDescriptor(army, false));
	}

	// Creature banks, dwellings, mines etc.: guards are not shown to a human in advance
	return {};
}

void Perception::refresh()
{
	const int today = cb->getDate(Date::DAY);
	std::set<ObjectInstanceID> seenNow;

	std::vector<ObservedObject> updates;
	for(const CGObjectInstance * obj : cb->getAllVisitableObjs())
	{
		ObservedObject observed;
		observed.id = obj->id;
		observed.type = obj->ID;
		observed.subtype = obj->subID;
		observed.owner = obj->getOwner();
		observed.visitablePos = obj->visitablePos();
		observed.lastSeenDay = today;
		observed.visibleNow = true;

		if(const auto * armed = dynamic_cast<const CArmedInstance *>(obj))
		{
			ObservedArmy army = observeArmy(armed);
			if(army.known)
				observed.army = army;
		}

		seenNow.insert(obj->id);
		updates.push_back(std::move(observed));
	}

	std::unique_lock lock(mx);
	for(auto it = objects.begin(); it != objects.end();)
	{
		if(seenNow.count(it->first))
		{
			++it;
			continue;
		}
		// We look at the tile and the object is gone: forget it. If the tile is hidden, keep the
		// (possibly outdated) memory, exactly like a human player would.
		if(cb->isVisible(it->second.visitablePos))
		{
			it = objects.erase(it);
			continue;
		}
		it->second.visibleNow = false;
		++it;
	}
	for(auto & observed : updates)
		objects[observed.id] = std::move(observed);
}

void Perception::onTilesHidden(const std::set<int3> & tiles)
{
	std::unique_lock lock(mx);
	hiddenAgain.insert(tiles.begin(), tiles.end());
	for(auto & entry : objects)
	{
		if(tiles.count(entry.second.visitablePos))
			entry.second.visibleNow = false;
	}
	logAi->debug("FairAI perception: %d tiles hidden again (%d stale in total)", tiles.size(), hiddenAgain.size());
}

void Perception::onTilesRevealed(const std::set<int3> & tiles)
{
	std::unique_lock lock(mx);
	for(const auto & tile : tiles)
		hiddenAgain.erase(tile);
}

void Perception::onObjectRemoved(ObjectInstanceID id)
{
	// The client learns about every removal; we only "notice" it if we can see the tile.
	std::unique_lock lock(mx);
	auto it = objects.find(id);
	if(it != objects.end() && cb->isVisible(it->second.visitablePos))
		objects.erase(it);
}

const CGObjectInstance * Perception::visibleObject(ObjectInstanceID id) const
{
	{
		std::unique_lock lock(mx);
		auto it = objects.find(id);
		if(it == objects.end() || (!it->second.visibleNow && it->second.owner != me))
			return nullptr;
	}
	return cb->getObj(id);
}

std::map<ObjectInstanceID, ObservedObject> Perception::knownObjects() const
{
	std::unique_lock lock(mx);
	return objects;
}

std::set<int3> Perception::staleTiles() const
{
	std::unique_lock lock(mx);
	return hiddenAgain;
}

std::string Perception::summary() const
{
	std::unique_lock lock(mx);
	int visible = 0;
	int foreignArmies = 0;
	for(const auto & entry : objects)
	{
		if(entry.second.visibleNow)
			visible++;
		if(entry.second.army && entry.second.owner != me)
			foreignArmies++;
	}
	return boost::str(boost::format("known objects: %d (visible now: %d), foreign armies observed: %d, stale tiles: %d")
		% objects.size() % visible % foreignArmies % hiddenAgain.size());
}

}
