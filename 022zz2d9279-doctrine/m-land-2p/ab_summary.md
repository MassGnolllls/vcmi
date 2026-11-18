# A/B test (16 configs, seeds 60)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 15 | 8 | 0.659 ± 0.191 | - | 0.649 ± 0.191 | - | 24 |
| nWFHC | `{"magicWorth": 1, "fetchByShare": 1, "noChase": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 15 | 9 | 0.625 ± 0.198 | -0.034 ± 0.221 | 0.625 ± 0.198 | -0.024 ± 0.223 | 24 |
| nKFHP | `{"breakout": 1, "fetchByShare": 1, "noChase": 1, "passage": 1, "regionValue": 8000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nKWPC | `{"breakout": 1, "magicWorth": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTFPC | `{"townWorth": 1, "fetchByShare": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTWHP | `{"townWorth": 1, "magicWorth": 1, "noChase": 1, "passage": 1, "regionValue": 8000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTKHC | `{"townWorth": 1, "breakout": 1, "noChase": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nTKWF | `{"townWorth": 1, "breakout": 1, "magicWorth": 1, "fetchByShare": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nAHPC | `{"townHeroes": 1, "secondArmy": 1, "noChase": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nAWFP | `{"townHeroes": 1, "secondArmy": 1, "magicWorth": 1, "fetchByShare": 1, "passage": 1, "regionValue": 8000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nAKFC | `{"townHeroes": 1, "secondArmy": 1, "breakout": 1, "fetchByShare": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nAKWH | `{"townHeroes": 1, "secondArmy": 1, "breakout": 1, "magicWorth": 1, "noChase": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nATFH | `{"townHeroes": 1, "secondArmy": 1, "townWorth": 1, "fetchByShare": 1, "noChase": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nATWC | `{"townHeroes": 1, "secondArmy": 1, "townWorth": 1, "magicWorth": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nATKP | `{"townHeroes": 1, "secondArmy": 1, "townWorth": 1, "breakout": 1, "passage": 1, "regionValue": 8000}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nATKWFHPC | `{"townHeroes": 1, "secondArmy": 1, "townWorth": 1, "breakout": 1, "magicWorth": 1, "fetchByShare": 1, "noChase": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
