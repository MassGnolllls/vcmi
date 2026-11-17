# A/B test (8 configs, seeds 51)

Paired against `nEPAH`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| nEPAH | `{"campaignEdge": 2.0, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 24 | 10 | 5 | 0.575 ± 0.181 | - | 0.565 ± 0.183 | - | 24 |
| nETWA | `{"campaignEdge": 2.0, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1}` | 24 | 13 | 2 | 0.729 ± 0.147 | +0.154 ± 0.185 | 0.732 ± 0.153 | +0.167 ± 0.195 | 24 |
| nECWH | `{"campaignEdge": 2.0, "supplyRuns": 1, "fightForecast": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 24 | 8 | 7 | 0.527 ± 0.176 | -0.048 ± 0.192 | 0.494 ± 0.184 | -0.071 ± 0.202 | 24 |
| nECTP | `{"campaignEdge": 2.0, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 24 | 8 | 4 | 0.548 ± 0.158 | -0.027 ± 0.220 | 0.536 ± 0.163 | -0.030 ± 0.222 | 24 |
| nSWAH | `{"scouts": 2, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 24 | 12 | 5 | 0.614 ± 0.172 | +0.040 ± 0.246 | 0.601 ± 0.178 | +0.036 ± 0.246 | 24 |
| nSTPA | `{"scouts": 2, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "armyFit": 1, "sites": 1, "fastKeeper": 1}` | 24 | 9 | 4 | 0.596 ± 0.156 | +0.021 ± 0.214 | 0.577 ± 0.175 | +0.012 ± 0.232 | 24 |
| nSCPH | `{"scouts": 2, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "simAfterHit": 1, "spellSearch": 2, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSCTW | `{"scouts": 2, "supplyRuns": 1, "fightForecast": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
