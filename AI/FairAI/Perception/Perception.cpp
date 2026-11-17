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
#include "../../../lib/callback/IGameRandomizer.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/ResourceTypeHandler.h"
#include "../../../lib/mapObjectConstructors/CRewardableConstructor.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/gameState/InfoAboutArmy.h"
#include "../../../lib/gameState/SThievesGuildInfo.h"
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

// ---------------------------------------------------------------- reward tables

namespace
{

/// Dice of our own for rolling what a table can produce. The game decides which of a table's
/// entries a building gets with one hundred-sided die: we hand out each of its faces once, so a
/// hundred rolled buildings show every entry exactly as often as the table says.
class TableDice final : public vstd::RNG
{
	CRandomGenerator inner;
	int face;

public:
	TableDice(int seed, int face)
		: inner(seed)
		, face(face)
	{
	}

	int nextInt(int lower, int upper) override { return lower == 0 && upper == 99 ? face : inner.nextInt(lower, upper); }
	int64_t nextInt64(int64_t lower, int64_t upper) override { return inner.nextInt64(lower, upper); }
	double nextDouble(double lower, double upper) override { return inner.nextDouble(lower, upper); }
	int nextInt(int upper) override { return inner.nextInt(upper); }
	int64_t nextInt64(int64_t upper) override { return inner.nextInt64(upper); }
	double nextDouble(double upper) override { return inner.nextDouble(upper); }
	int nextInt() override { return inner.nextInt(); }
	int nextBinomialInt(int coinsCount, double coinChance) override { return inner.nextBinomialInt(coinsCount, coinChance); }
};

/// The engine's table reader asks a randomizer for its rolls; this one belongs to us alone and
/// knows nothing of the game being played (the server's randomizer never leaves the server).
class TableRandomizer final : public IGameRandomizer
{
	TableDice dice;

public:
	TableRandomizer(int seed, int face)
		: dice(seed, face)
	{
	}

	ArtifactID rollArtifact() override { return ArtifactID(ArtifactID::NONE); }
	ArtifactID rollArtifact(EArtifactClass type) override { return ArtifactID(ArtifactID::NONE); }
	ArtifactID rollArtifact(std::set<ArtifactID> filtered) override
	{
		if(filtered.empty())
			return ArtifactID(ArtifactID::NONE);
		return *std::next(filtered.begin(), dice.nextInt64(0, static_cast<int64_t>(filtered.size()) - 1));
	}
	std::vector<ArtifactID> rollMarketArtifactSet() override { return {}; }
	CreatureID rollCreature() override { return CreatureID(CreatureID::NONE); }
	CreatureID rollCreature(int tier) override { return CreatureID(CreatureID::NONE); }
	PrimarySkill rollPrimarySkillForLevelup(const CGHeroInstance * hero) override { return PrimarySkill::ATTACK; }
	SecondarySkill rollSecondarySkillForLevelup(const CGHeroInstance * hero, const std::set<SecondarySkill> & candidates) override
	{
		return candidates.empty() ? SecondarySkill(SecondarySkill::NONE) : *candidates.begin();
	}
	std::vector<SecondarySkill> rollSecondarySkills(const CGHeroInstance * hero) override { return {}; }
	vstd::RNG & getDefault() override { return dice; }
};

uint64_t mix(uint64_t h, int64_t v)
{
	return (h ^ static_cast<uint64_t>(v)) * 1099511628211ULL;
}

uint64_t hashResources(uint64_t h, const TResources & resources)
{
	for(const auto & res : LIBRARY->resourceTypeHandler->getAllObjects())
		h = mix(h, resources[res]);
	return h;
}

template<typename Id>
uint64_t hashIds(uint64_t h, const std::vector<Id> & ids)
{
	h = mix(h, static_cast<int64_t>(ids.size()));
	for(const auto & id : ids)
		h = mix(h, id.getNum());
	return h;
}

uint64_t hashStacks(uint64_t h, const std::vector<CStackBasicDescriptor> & stacks)
{
	h = mix(h, static_cast<int64_t>(stacks.size()));
	for(const auto & stack : stacks)
		h = mix(mix(h, stack.getId().getNum()), stack.getCount());
	return h;
}

uint64_t hashLimiter(uint64_t h, const Rewardable::Limiter & limiter)
{
	for(int v : {limiter.dayOfWeek, limiter.daysPassed, limiter.heroExperience, limiter.heroLevel, limiter.manaPoints, limiter.manaPercentage,
		limiter.movePoints, limiter.movePercentage, static_cast<int>(limiter.canLearnSkills), static_cast<int>(limiter.commanderAlive),
		static_cast<int>(limiter.hasExtraCreatures)})
		h = mix(h, v);
	h = hashResources(h, limiter.resources);
	for(int v : limiter.primary)
		h = mix(h, v);
	for(const auto & [skill, level] : limiter.secondary)
		h = mix(mix(h, skill.getNum()), level);
	h = hashIds(h, limiter.artifacts);
	h = hashIds(h, limiter.scrolls);
	h = hashIds(h, limiter.spells);
	h = hashIds(h, limiter.canLearnSpells);
	h = hashStacks(h, limiter.creatures);
	h = hashStacks(h, limiter.canReceiveCreatures);
	h = hashIds(h, limiter.heroes);
	h = hashIds(h, limiter.heroClasses);
	h = hashIds(h, limiter.players);
	for(const auto * list : {&limiter.allOf, &limiter.anyOf, &limiter.noneOf})
	{
		h = mix(h, static_cast<int64_t>(list->size()) + 77);
		for(const auto & sub : *list)
			if(sub)
				h = hashLimiter(h, *sub);
	}
	return h;
}

uint64_t hashReward(uint64_t h, const Rewardable::Reward & reward)
{
	h = hashResources(h, reward.resources);
	for(int v : {reward.heroExperience, reward.heroLevel, reward.manaDiff, reward.manaPercentage, reward.movePoints, reward.movePercentage,
		static_cast<int>(reward.removeObject), reward.spellCast.first.getNum(), reward.spellCast.second,
		reward.revealTiles ? reward.revealTiles->radius + 1000 * (reward.revealTiles->hide ? 1 : 0) : -7})
		h = mix(h, v);
	h = hashStacks(h, reward.guards);
	for(int v : reward.primary)
		h = mix(h, v);
	for(const auto & [skill, level] : reward.secondary)
		h = mix(mix(h, skill.getNum()), level);
	for(const auto & [from, to] : reward.creaturesChange)
		h = mix(mix(h, from.getNum()), to.getNum());
	h = hashIds(h, reward.grantedArtifacts);
	h = hashIds(h, reward.takenArtifacts);
	h = hashIds(h, reward.grantedScrolls);
	h = hashIds(h, reward.takenScrolls);
	h = hashIds(h, reward.spells);
	h = hashStacks(h, reward.creatures);
	h = hashStacks(h, reward.takenCreatures);
	for(const auto * list : {&reward.heroBonuses, &reward.playerBonuses, &reward.commanderBonuses})
	{
		h = mix(h, static_cast<int64_t>(list->size()) + 55);
		for(const auto & bonus : *list)
			if(bonus)
				h = mix(mix(h, static_cast<int64_t>(bonus->type)), bonus->val);
	}
	return h;
}

/// One building per face of the die, read by the engine's own table reader
std::shared_ptr<const RewardTable> buildTable(const CRewardableConstructor & constructor, const CGObjectInstance * obj, IGameInfoCallback * info)
{
	constexpr int FACES = 100;
	auto table = std::make_shared<RewardTable>();
	std::map<uint64_t, int> bySignature;                                      // looks -> sample
	std::map<std::vector<std::pair<CreatureID, int>>, int> byGuards;          // guards -> state
	std::vector<int> stateOfSample;
	try
	{
		for(int face = 0; face < FACES; ++face)
		{
			TableRandomizer randomizer(7919 * (obj->ID.getNum() + 1) + 104729 * (obj->subID.getNum() + 1) + face, face);
			Rewardable::Configuration rolled = constructor.generateConfiguration(info, randomizer, obj->ID, {});
			if(face == 0)
			{
				table->visitMode = static_cast<Rewardable::EVisitMode>(rolled.visitMode);
				table->selectMode = static_cast<Rewardable::ESelectMode>(rolled.selectMode);
				table->forceCombat = rolled.forceCombat;
				table->layout = rolled.guardsLayout.empty() ? std::string("default") : rolled.guardsLayout;
			}

			RewardSample sample;
			std::vector<std::pair<CreatureID, int>> guards;
			uint64_t signature = 1469598103934665603ULL;
			for(auto & visit : rolled.info)
			{
				// the engine posts the guards of every entry, slot after slot, as long as there is room
				for(const auto & guard : visit.reward.guards)
				{
					if(guards.size() < GameConstants::ARMY_SIZE && guard.getId().hasValue() && guard.getCount() > 0)
						guards.emplace_back(guard.getId(), guard.getCount());
				}
				if(visit.visitType != Rewardable::EEventType::EVENT_FIRST_VISIT)
					continue;
				signature = hashReward(hashLimiter(mix(signature, 31), visit.limiter), visit.reward);
				visit.message = MetaString();
				visit.description = MetaString();
				sample.visits.push_back(visit);
			}
			for(const auto & [creature, count] : guards)
				signature = mix(mix(signature, creature.getNum()), count);

			auto known = bySignature.find(signature);
			int index;
			if(known != bySignature.end())
			{
				index = known->second;
				table->samples[index].weight++;
			}
			else
			{
				index = static_cast<int>(table->samples.size());
				bySignature[signature] = index;
				table->samples.push_back(std::move(sample));
				stateOfSample.push_back(-1);
			}
			table->total++;

			auto state = byGuards.find(guards);
			if(state == byGuards.end())
			{
				state = byGuards.emplace(guards, static_cast<int>(table->states.size())).first;
				GuardState fresh;
				fresh.guards = guards;
				for(const auto & [creature, count] : guards)
					fresh.strength += static_cast<uint64_t>(creature.toCreature()->getAIValue()) * count;
				table->states.push_back(fresh);
			}
			GuardState & in = table->states[state->second];
			in.chance += 1.0 / FACES;
			if(stateOfSample[index] < 0)
			{
				stateOfSample[index] = state->second;
				in.samples.push_back(index);
			}
			table->guarded = table->guarded || !guards.empty();
		}
	}
	catch(const std::exception & e)
	{
		logAi->debug("FairAI perception: the table of object %d/%d cannot be read (%s)", obj->ID.getNum(), obj->subID.getNum(), e.what());
		return nullptr;
	}
	if(!table->guarded)
		table->states.clear();
	else
		std::stable_sort(table->states.begin(), table->states.end(), [](const GuardState & a, const GuardState & b) { return a.strength < b.strength; });
	return table;
}

}

std::shared_ptr<const RewardTable> Perception::rewardTable(const CGObjectInstance * obj) const
{
	if(!obj)
		return nullptr;
	const std::pair<int, int> key{obj->ID.getNum(), obj->subID.getNum()};
	{
		std::unique_lock lock(tableMx);
		if(auto known = tables.find(key); known != tables.end())
			return known->second;
	}
	// by the kind of building alone: the table is the same for every building of that kind
	std::shared_ptr<const RewardTable> table;
	if(dynamic_cast<const CRewardableObject *>(obj))
	{
		try
		{
			const auto handler = obj->getObjectHandler();
			if(const auto * constructor = dynamic_cast<const CRewardableConstructor *>(handler.get()))
				table = buildTable(*constructor, obj, cb.get());
		}
		catch(const std::exception &)
		{
			// a kind of object the game has no table for
		}
	}
	if(table && logAi->isDebugEnabled())
	{
		std::string states;
		for(const auto & state : table->states)
		{
			states += boost::str(boost::format(" [%.0f%%:") % (100 * state.chance));
			for(const auto & [creature, count] : state.guards)
				states += boost::str(boost::format(" %d %s") % count % creature.toCreature()->getJsonKey());
			states += boost::str(boost::format(", strength %d]") % state.strength);
		}
		logAi->debug("FairAI perception: table of building %d/%d (%s): %d different outcomes in %d rolls%s%s", key.first, key.second, obj->getObjectName(),
			table->samples.size(), table->total, table->guarded ? ", guards:" : "", states);
	}
	std::unique_lock lock(tableMx);
	tables[key] = table;
	return table;
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
		// a hero half inside the fog: the info window needs the tile he stands on
		if(!cb->isVisible(army->visitablePos()))
			return {};
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
		observed.table = rewardTable(obj);
		if(observed.table && observed.table->guarded)
		{
			// A guarded building one of our heroes has stood before: from then on a right click lists
			// its guards (if the game is set to show them) - or none, when nobody guards it any more.
			const auto * building = dynamic_cast<const CRewardableObject *>(obj);
			if(building && building->wasScouted(me) && cb->getSettings().getBoolean(EGameSettings::BANKS_SHOW_GUARDS_COMPOSITION))
			{
				observed.guardsShown = true;
				for(const Component & shown : obj->getPopupComponents(me))
				{
					if(shown.type == ComponentType::CREATURE && shown.value && *shown.value > 0)
						observed.shownGuards.emplace_back(shown.subType.as<CreatureID>(), *shown.value);
				}
			}
		}
		if(obj->ID == Obj::HERO && !isOwn(obj) && cb->isVisible(obj->visitablePos()))
		{
			// primary skills: only where the hero's info window shows them
			InfoAboutHero info;
			if(cb->getHeroInfo(obj, info) && info.details && info.details->primskills.size() >= 4)
				observed.heroSkills = std::array<int, 4>{info.details->primskills[0], info.details->primskills[1], info.details->primskills[2], info.details->primskills[3]};
		}

		seenNow.insert(obj->id);
		updates.push_back(std::move(observed));
	}

	// What we own we know without looking (the kingdom overview lists it). A town or mine that our
	// memory still calls ours but that is no longer on that list was taken while hidden from us -
	// under a Cover of Darkness, say. Taken by the enemy, if there is only one; else by somebody.
	std::set<ObjectInstanceID> mine;
	for(const CGObjectInstance * obj : cb->getMyObjects())
		mine.insert(obj->id);
	PlayerColor taker = PlayerColor::NEUTRAL;
	{
		int enemies = 0;
		for(int c = 0; c < PlayerColor::PLAYER_LIMIT_I; ++c)
		{
			const PlayerColor color(c);
			if(color != me && cb->getPlayerStatus(color, false) == EPlayerStatus::INGAME
				&& cb->getPlayerRelations(me, color) == PlayerRelations::ENEMIES)
			{
				++enemies;
				taker = color;
			}
		}
		if(enemies != 1)
			taker = PlayerColor::NEUTRAL;
	}

	std::unique_lock lock(mx);
	for(auto it = objects.begin(); it != objects.end();)
	{
		if(seenNow.count(it->first))
		{
			++it;
			continue;
		}
		if(it->second.owner == me && !mine.count(it->first) && it->second.type != Obj::HERO)
			it->second.owner = taker;
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

GuildInfo Perception::thievesGuild() const
{
	GuildInfo result;
	for(const auto * town : cb->getTownsInfo(true))
	{
		if(!town->hasBuilt(BuildingID::TAVERN))
			continue;
		SThievesGuildInfo info;
		cb->getThievesGuildInfo(info, town);
		result.available = true;
		for(const auto & [color, hero] : info.colorToBestHero)
		{
			if(color == me || !hero.portraitSource.hasValue())
				continue;
			GuildHero shown;
			shown.hero = hero.portraitSource;
			// the window shows the four primary skills (never the army: we do not read it)
			if(hero.details && hero.details->primskills.size() >= 4)
				shown.skills = std::array<int, 4>{hero.details->primskills[0], hero.details->primskills[1], hero.details->primskills[2], hero.details->primskills[3]};
			result.bestHero[color] = shown;
		}
		for(size_t place = 0; place < info.numOfTowns.size(); ++place)
			for(const auto & color : info.numOfTowns[place])
				result.townPlace[color] = static_cast<int>(place);
		break; // every tavern of ours shows the same
	}
	return result;
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
		// our own mine or dwelling in land that is hidden again (Cover of Darkness): we do not see it either
		if(!it->second.visibleNow && !cb->isVisible(it->second.visitablePos))
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
