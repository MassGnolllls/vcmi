# A/B test (8 configs, seeds 50)

Paired against `old`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| old | `{"tables": 0}` | 24 | 11 | 3 | 0.629 ± 0.166 | - | 0.619 ± 0.172 | - | 24 |
| nTPWH | `{"passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "simAfterHit": 1, "spellSearch": 2}` | 24 | 11 | 2 | 0.748 ± 0.136 | +0.118 ± 0.192 | 0.750 ± 0.147 | +0.131 ± 0.212 | 24 |
| nCPWA | `{"supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "tables": 0}` | 24 | 13 | 3 | 0.809 ± 0.135 | +0.180 ± 0.236 | 0.798 ± 0.141 | +0.179 ± 0.247 | 24 |
| nCTAH | `{"supplyRuns": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSEPW | `{"scouts": 2, "campaignEdge": 2.0, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSETH | `{"scouts": 2, "campaignEdge": 2.0, "simAfterHit": 1, "spellSearch": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSECA | `{"scouts": 2, "campaignEdge": 2.0, "supplyRuns": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "tables": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSECTPWAH | `{"scouts": 2, "campaignEdge": 2.0, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
