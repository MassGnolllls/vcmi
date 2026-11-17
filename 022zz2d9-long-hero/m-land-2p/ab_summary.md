# A/B test (9 configs, seeds 44)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 10 | 6 | 0.599 ± 0.166 | - | 0.595 ± 0.176 | - | 24 |
| nCFE | `{"supplyRuns": 1, "fortress": 1, "campaignEdge": 2.0}` | 23 | 15 | 3 | 0.742 ± 0.166 | +0.161 ± 0.175 | 0.745 ± 0.167 | +0.168 ± 0.188 | 23 |
| nMCX | `{"magicFocus": 1, "skillChoice": 1, "supplyRuns": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 24 | 7 | 8 | 0.427 ± 0.174 | -0.172 ± 0.186 | 0.435 ± 0.172 | -0.161 ± 0.184 | 24 |
| nUFX | `{"surrender": 1, "veterans": 1, "fortress": 1, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nUME | `{"surrender": 1, "veterans": 1, "magicFocus": 1, "skillChoice": 1, "campaignEdge": 2.0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSEX | `{"scouts": 2, "campaignEdge": 2.0, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSMF | `{"scouts": 2, "magicFocus": 1, "skillChoice": 1, "fortress": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSUC | `{"scouts": 2, "surrender": 1, "veterans": 1, "supplyRuns": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nSUMCFEX | `{"scouts": 2, "surrender": 1, "veterans": 1, "magicFocus": 1, "skillChoice": 1, "supplyRuns": 1, "fortress": 1, "campaignEdge": 2.0, "fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
