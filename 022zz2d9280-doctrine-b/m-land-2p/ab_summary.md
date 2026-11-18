# A/B test (16 configs, seeds 60)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 14 | 7 | 0.650 ± 0.183 | - | 0.655 ± 0.181 | - | 24 |
| nWFHC | `{"magicWorth": 1, "fetchByShare": 1, "noChase": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 13 | 11 | 0.542 ± 0.204 | -0.109 ± 0.251 | 0.542 ± 0.204 | -0.113 ± 0.252 | 24 |
| nKFHP | `{"breakout": 2, "fetchByShare": 1, "noChase": 1, "passage": 1, "regionValue": 8000}` | 24 | 10 | 14 | 0.417 ± 0.201 | -0.234 ± 0.241 | 0.417 ± 0.201 | -0.238 ± 0.243 | 24 |
| nKWPC | `{"breakout": 2, "magicWorth": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 13 | 11 | 0.542 ± 0.204 | -0.109 ± 0.268 | 0.542 ± 0.204 | -0.113 ± 0.268 | 24 |
| nTFPC | `{"townWorth": 1, "townAmbush": 1, "fetchByShare": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 10 | 14 | 0.417 ± 0.201 | -0.234 ± 0.240 | 0.417 ± 0.201 | -0.238 ± 0.239 | 24 |
| nTWHP | `{"townWorth": 1, "townAmbush": 1, "magicWorth": 1, "noChase": 1, "passage": 1, "regionValue": 8000}` | 24 | 11 | 12 | 0.474 ± 0.200 | -0.176 ± 0.261 | 0.470 ± 0.200 | -0.185 ± 0.259 | 24 |
| nTKHC | `{"townWorth": 1, "townAmbush": 1, "breakout": 2, "noChase": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 23 | 16 | 6 | 0.729 ± 0.182 | +0.094 ± 0.223 | 0.720 ± 0.182 | +0.081 ± 0.218 | 23 |
| nTKWF | `{"townWorth": 1, "townAmbush": 1, "breakout": 2, "magicWorth": 1, "fetchByShare": 1}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.058 ± 0.246 | 0.708 ± 0.186 | +0.054 ± 0.244 | 24 |
| nAHPC | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "noChase": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 10 | 14 | 0.417 ± 0.201 | -0.234 ± 0.285 | 0.417 ± 0.201 | -0.238 ± 0.285 | 24 |
| nAWFP | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "magicWorth": 1, "fetchByShare": 1, "passage": 1, "regionValue": 8000}` | 23 | 19 | 4 | 0.826 ± 0.158 | +0.148 ± 0.269 | 0.826 ± 0.158 | +0.143 ± 0.265 | 23 |
| nAKFC | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "breakout": 2, "fetchByShare": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 13 | 10 | 0.551 ± 0.200 | -0.099 ± 0.303 | 0.542 ± 0.204 | -0.113 ± 0.299 | 24 |
| nAKWH | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "breakout": 2, "magicWorth": 1, "noChase": 1}` | 24 | 14 | 9 | 0.603 ± 0.196 | -0.048 ± 0.248 | 0.589 ± 0.199 | -0.065 ± 0.253 | 24 |
| nATFH | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "townWorth": 1, "townAmbush": 1, "fetchByShare": 1, "noChase": 1}` | 24 | 13 | 11 | 0.542 ± 0.204 | -0.109 ± 0.270 | 0.542 ± 0.204 | -0.113 ± 0.268 | 24 |
| nATWC | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "townWorth": 1, "townAmbush": 1, "magicWorth": 1, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 17 | 7 | 0.708 ± 0.186 | +0.058 ± 0.263 | 0.708 ± 0.186 | +0.054 ± 0.259 | 24 |
| nATKP | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "townWorth": 1, "townAmbush": 1, "breakout": 2, "passage": 1, "regionValue": 8000}` | 24 | 13 | 11 | 0.542 ± 0.204 | -0.109 ± 0.261 | 0.542 ± 0.204 | -0.113 ± 0.256 | 24 |
| nATKWFHPC | `{"townHeroes": 1, "secondArmy": 1, "nightWatch": 1, "townWorth": 1, "townAmbush": 1, "breakout": 2, "magicWorth": 1, "fetchByShare": 1, "noChase": 1, "passage": 1, "regionValue": 8000, "guardKillValue": 0.08, "expValueMain": 1.0}` | 24 | 10 | 14 | 0.417 ± 0.201 | -0.234 ± 0.286 | 0.417 ± 0.201 | -0.238 ± 0.281 | 24 |
