# What creatures can do, and what FairAI knows of it

199 creatures of the installed game (mods: none), 90 kinds of ability.

open: 29 | switch: 10 | simulated: 33 | formula: 9 | no effect: 9

## open

| ability | creatures | examples | what FairAI does with it |
|---|---|---|---|
| CASTS | 30 | archangel, masterGenie, pitLord, ogreMage, iceElemental ... | how often the unit can cast. |
| SPELLCASTER | 25 | archangel, pitLord, ogreMage, iceElemental, magmaElemental ... | casts a spell instead of attacking. Spells: airShield, bloodlust, chainLightning, detonates01, detonates02, fireball, fortune, frostRing, heatStrokeDreadnought, heatStrokeJuggernaut, iceBolt, inferno, lightningBolt, magicArrow, meditation01, meditation02, meteorShower, mirth, protectAir, protectEarth, protectFire, protectWater, quicksand, resurrection, sandVigour, summonDemons, summonLarva, teleport. |
| SPELL_IMMUNITY | 20 | troglodyte, infernalTroglodyte, airElemental, earthElemental, waterElemental ... | immune to a spell (the hero's spells are not part of the forecast yet). |
| CREATURE_ENCHANT_POWER | 15 | masterGenie, ogreMage, iceElemental, magmaElemental, stormElemental ... | how long the unit's spells last. |
| ENCHANTED | 10 | bountyHunter, gunslinger, halflingGrenadier, hotaEngineer, hotaMechanic ... | under a lasting spell from the start. |
| SPELL_DAMAGE_REDUCTION | 10 | ironGolem, stoneGolem, goldGolem, diamondGolem, firebird ... | takes less damage from spells. |
| MORE_DAMAGE_FROM_SPELL | 8 | airElemental, earthElemental, fireElemental, waterElemental, iceElemental ... | takes more damage from a spell. |
| MAGIC_RESISTANCE | 7 | dwarf, battleDwarf, crystalDragon, jotunn, jotunnWarlord ... | chance to resist hostile spells. |
| CREATURE_SPELL_POWER | 5 | fairieDragon, blackbeard, fangarm, cowboy, grandshaman | strength of the unit's spells. |
| NO_WALL_PENALTY | 5 | archMage, enchanter, sharpshooter, arrowTower, snowElf | matters in sieges, which the simulator does not play. |
| CATAPULT | 4 | cyclop, cyclopKing, catapult, cannon | sieges. |
| BLOCKS_RANGED_RETALIATION | 3 | ballista, arrowTower, cannon | is not shot back at (matters against the two units that shoot back). |
| ADJACENT_SPELLCASTER | 2 | hotaEngineer, hotaMechanic | casts on a neighbour instead of attacking. |
| CHANGES_SPELL_COST_FOR_ALLY | 2 | mage, archMage | our hero's spells cost less (the price of spell points does not know it). |
| CHANGES_SPELL_COST_FOR_ENEMY | 2 | pegasus, silverPegasus | the enemy hero's spells cost more. |
| FIRST_STRIKE | 2 | bountyHunter, gunslinger | retaliates before it is struck. |
| RANGED_RETALIATION | 2 | bountyHunter, gunslinger | shoots back at shooters. |
| SPELL_RESISTANCE_AURA | 2 | unicorn, warUnicorn | neighbours resist spells. |
| CATAPULT_EXTRA_SHOTS | 1 | cyclopKing | sieges. |
| ENCHANTER | 1 | enchanter | casts a mass spell every few rounds. Spells: airShield, bless, haste, slow, stoneSkin, weakness. |
| FEROCITY | 1 | ayssid | strikes again when it has killed. |
| MAGIC_MIRROR | 1 | fairieDragon | may throw a spell back. |
| MANA_CHANNELING | 1 | familiar | passes the enemy hero's spent spell points to ours. |
| MANA_DRAIN | 1 | wraith | takes spell points from the enemy hero every round. |
| ON_COMBAT_EVENT | 1 | yetiRunemaster | effects triggered by events of the battle (yeti runemaster). |
| RANDOM_SPELLCASTER | 1 | masterGenie | casts a random helpful spell. |
| REBIRTH | 1 | phoenix | part of the stack rises again when it is destroyed. |
| REVENGE | 1 | haspid | damage grows with its own losses. |
| YETI_RUNE_LEVEL_COUNTER | 1 | yetiRunemaster | runes of the yeti runemaster. |

## switch

| ability | creatures | examples | what FairAI does with it |
|---|---|---|---|
| SPELL_AFTER_ATTACK | 27 | dendroidGuard, dendroidSoldier, unicorn, warUnicorn, zombieLord ... | by what the spell does: puts out of action, ages, poisons, curses, weakens, bolts [simAfterHit]. Spells: acidBreath, age, bind, blind, curse, disease, dispelHelpful, disruptingRay, freezingShot, hypnotize, paralyze, poison, stoneGaze, thunderbolt, weakness. |
| MIND_IMMUNITY | 13 | giant, titan, airElemental, earthElemental, fireElemental ... | cannot be blinded or hypnotised [simAfterHit]. |
| SPELL_LIKE_ATTACK | 12 | monk, zealot, titan, magog, lich ... | shot bursts on the hexes around the target, friend and foe [simMultiHex]. Spells: deathCloud, fireballAbility, hotaEnchanterArrow, hotaGrenade, hotaIce, hotaLightGlobe, hotaLightning, hotaRay, hotaStorm. |
| TWO_HEX_ATTACK_BREATH | 10 | greenDragon, goldDragon, redDragon, blackDragon, firebird ... | breath reaches the hex behind the target [simMultiHex]. |
| SPECIFIC_SPELL_POWER | 7 | archangel, pitLord, thunderbird, automaton, hotaEngineer ... | strength of the unit's own spell [simAfterHit, for spells after an attack]. Spells: detonates01, detonates02, hotaRepair, resurrection, summonDemons, thunderbolt. |
| FEARFUL | 4 | azureDragon, bountyHunter, gunslinger, halflingGrenadier | fear: enemies of azure dragons may lose their turn, the fearless do not [simAfterHit]. |
| DEATH_STARE | 2 | mightyGorgon, seadog | kills whole creatures of the target, whatever they are worth [simAfterHit]. |
| MULTIHEX_UNIT_ATTACK | 2 | hotaDreadnought, hotaJuggernaut | attack reaches further hexes, own troops included [simMultiHex]. |
| ACID_BREATH | 1 | rustDragon | acid damage and lost defence [simAfterHit]. |
| THREE_HEADED_ATTACK | 1 | cerberus | bites the neighbours of its target [simMultiHex]. |

## simulated

| ability | creatures | examples | what FairAI does with it |
|---|---|---|---|
| STACKS_SPEED | 199 | pikeman, halberdier, archer, marksman, griffin ... | speed and turn order. |
| LIVING | 157 | pikeman, halberdier, archer, marksman, griffin ... | the game's own mark of the living: life drain, death stare, ageing and poison work on these only. |
| FLYING | 53 | griffin, royalGriffin, angel, archangel, pegasus ... | flies over obstacles and units. |
| SHOTS | 48 | archer, marksman, monk, zealot, woodElf ... | ammunition. |
| SHOOTER | 45 | archer, marksman, monk, zealot, woodElf ... | shoots; melee and range penalties. |
| BLOCKS_RETALIATION | 17 | naga, nagaQueen, cerberus, devil, archDevil ... | is not struck back. |
| NON_LIVING | 15 | ironGolem, stoneGolem, airElemental, earthElemental, fireElemental ... | no morale; life drain, death stare, ageing and poison do nothing. |
| UNDEAD | 15 | skeleton, skeletonWarrior, walkingDead, zombieLord, wight ... | no morale; life drain, death stare, ageing and poison do nothing. |
| HP_REGENERATION | 11 | wight, wraith, troll, haspid, seaserpent ... | top creature heals every round. |
| SPELL_SCHOOL_IMMUNITY | 9 | efreet, efreetSultan, fireElemental, energyElemental, phoenix ... | fire immunity against fire shield and fireball shots; hero spells are not part of the forecast yet. |
| ADDITIONAL_ATTACK | 8 | marksman, crusader, grandElf, hobgoblinWolfRider, automaton ... | strikes or shoots twice. |
| LEVEL_SPELL_IMMUNITY | 6 | greenDragon, goldDragon, redDragon, blackDragon, magicElemental ... | immune to spells up to a level: spell-like shots and effects after an attack do nothing. |
| MORALE | 6 | angel, archangel, boneDragon, ghostDragon, minotaur ... | expected value of the morale dice. |
| SIEGE_WEAPON | 6 | catapult, ballista, firstAidTent, ammoCart, arrowTower ... | war machine: does not move, knows no morale. |
| LUCK | 5 | devil, archDevil, halfling, leprechaun, halflingGrenadier | expected value of the luck dice. |
| NO_DISTANCE_PENALTY | 5 | sharpshooter, catapult, arrowTower, corsair, seadog | full damage at any range. |
| ATTACKS_ALL_ADJACENT | 4 | hydra, chaosHydra, psychicElemental, magicElemental | hits everything around it. |
| JOUSTING | 4 | cavalier, champion, haspid, seaserpent | damage grows with the distance charged. |
| MECHANICAL | 4 | automaton, hotaDreadnought, hotaJuggernaut, sentinelAutomaton | no morale; life drain, death stare, ageing and poison do nothing. |
| NO_LUCK | 4 | catapult, firstAidTent, ammoCart, arrowTower | luck dice do not apply. |
| RETURN_AFTER_STRIKE | 4 | harpy, harpyHag, armadillo, bellwetherArmadillo | flies back after its strike. |
| DOUBLE_DAMAGE_CHANCE | 3 | dreadKnight, crewmate, seaman | death blow, as expected value. |
| UNLIMITED_RETALIATIONS | 3 | royalGriffin, fangarm, bountyHunter | retaliates every time. |
| CHARGE_IMMUNITY | 2 | pikeman, halberdier | takes no jousting bonus. |
| GARGOYLE | 2 | stoneGargoyle, obsidianGargoyle | not living: life drain, death stare, ageing and poison do nothing. |
| ADDITIONAL_RETALIATION | 1 | griffin | more than one retaliation a round. |
| FIRE_SHIELD | 1 | efreetSultan | burns its attacker. |
| FREE_SHOOTING | 1 | steelElf | shoots with an enemy next to it. |
| HEALER | 1 | firstAidTent | first aid tent. |
| LIFE_DRAIN | 1 | vampireLord | heals by the damage it deals - to the living only. |
| NOT_ACTIVE | 1 | ammoCart | blinded, paralysed or petrified units skip their turns until attacked. |
| NO_MORALE | 1 | sandwormLarva | morale dice do not apply. |
| NO_RETALIATION | 1 | cannon | never retaliates. |

## formula

| ability | creatures | examples | what FairAI does with it |
|---|---|---|---|
| CREATURE_DAMAGE | 199 | pikeman, halberdier, archer, marksman, griffin ... | damage range. |
| PRIMARY_SKILL | 199 | pikeman, halberdier, archer, marksman, griffin ... | attack and defence. |
| STACK_HEALTH | 199 | pikeman, halberdier, archer, marksman, griffin ... | health. |
| KING | 30 | angel, archangel, greenDragon, goldDragon, giant ... | target of slayer. |
| HATE | 21 | angel, archangel, genie, masterGenie, titan ... | more damage against the creatures it hates. |
| NO_MELEE_PENALTY | 14 | zealot, mage, archMage, titan, beholder ... | full damage in melee. |
| ENEMY_DEFENCE_REDUCTION | 4 | behemoth, ancientBehemoth, arrowTower, halflingGrenadier | ignores part of the target's defence. |
| ENEMY_ATTACK_REDUCTION | 2 | nix, nixWarrior | ignores part of the attacker's attack. |
| GENERAL_DAMAGE_REDUCTION | 2 | nix, nixWarrior | takes less damage. |

## no effect

| ability | creatures | examples | what FairAI does with it |
|---|---|---|---|
| SKELETON_TRANSFORMER_TARGET | 16 | greenDragon, goldDragon, boneDragon, ghostDragon, redDragon ... | what the skeleton transformer makes of it. |
| DRAGON_NATURE | 10 | greenDragon, goldDragon, boneDragon, ghostDragon, redDragon ... | what the unit is (targets of some artifacts). |
| NONE | 5 | cannon, jotunn, jotunnWarlord, warMammoth, yetiRunemaster | an ability this version of the engine does not know (written for a newer one): it does nothing in the game either. |
| CPU_CONTROLLED | 3 | ballista, arrowTower, cannon | war machines the game steers itself. |
| GENERATE_RESOURCE | 2 | kobold, koboldForeman | daily resources from creatures in an army (not counted in the economy yet). |
| MULTIHEX_ANIMATION | 2 | hotaDreadnought, hotaJuggernaut | how the attack is drawn. |
| NO_TERRAIN_PENALTY | 1 | nomad | adventure map: the engine's pathfinder handles it. |
| SPECIAL_CRYSTAL_GENERATION | 1 | crystalDragon | weekly crystals of the crystal dragon (not counted in the economy yet). |
| VISIONS | 1 | rogue | adventure map. |

