# A/B test (8 configs, seeds 51)

Paired against `nEPAH`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| nEPAH | `{"campaignEdge": 2.0, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 24 | 10 | 5 | 0.575 ± 0.181 | - | 0.565 ± 0.183 | - | 24 |
| nETWA | `{"campaignEdge": 2.0, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nECWH | `{"campaignEdge": 2.0, "supplyRuns": 1, "fightForecast": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nECTP | `{"campaignEdge": 2.0, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSWAH | `{"scouts": 2, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSTPA | `{"scouts": 2, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "armyFit": 1, "sites": 1, "fastKeeper": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSCPH | `{"scouts": 2, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSCTW | `{"scouts": 2, "supplyRuns": 1, "fightForecast": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
