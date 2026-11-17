# A/B test (8 configs, seeds 50)

Paired against `old`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| old | `{"tables": 0}` | 24 | 11 | 3 | 0.629 ± 0.166 | - | 0.619 ± 0.172 | - | 24 |
| nTPWH | `{"passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "simAfterHit": 1, "spellSearch": 2}` | 24 | 11 | 2 | 0.748 ± 0.136 | +0.118 ± 0.192 | 0.750 ± 0.147 | +0.131 ± 0.212 | 24 |
| nCPWA | `{"supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "tables": 0}` | 24 | 13 | 3 | 0.809 ± 0.135 | +0.180 ± 0.236 | 0.798 ± 0.141 | +0.179 ± 0.247 | 24 |
| nCTAH | `{"supplyRuns": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2}` | 24 | 12 | 2 | 0.709 ± 0.143 | +0.079 ± 0.152 | 0.667 ± 0.161 | +0.048 ± 0.167 | 24 |
| nSEPW | `{"scouts": 2, "campaignEdge": 2.0, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "tables": 0}` | 24 | 9 | 3 | 0.599 ± 0.158 | -0.031 ± 0.203 | 0.589 ± 0.157 | -0.030 ± 0.211 | 24 |
| nSETH | `{"scouts": 2, "campaignEdge": 2.0, "simAfterHit": 1, "spellSearch": 2}` | 24 | 14 | 2 | 0.759 ± 0.139 | +0.129 ± 0.166 | 0.732 ± 0.155 | +0.113 ± 0.182 | 24 |
| nSECA | `{"scouts": 2, "campaignEdge": 2.0, "supplyRuns": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "tables": 0}` | 24 | 9 | 1 | 0.688 ± 0.123 | +0.058 ± 0.197 | 0.631 ± 0.146 | +0.012 ± 0.228 | 24 |
| nSECTPWAH | `{"scouts": 2, "campaignEdge": 2.0, "supplyRuns": 1, "passage": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "fightForecast": 1, "armyFit": 1, "sites": 1, "fastKeeper": 1, "simAfterHit": 1, "spellSearch": 2}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
