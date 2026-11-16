# A/B test (6 configs, seeds 42)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 6 | 0.525 ± 0.139 | - | 0.351 ± 0.095 | - | 24 |
| g | `{"collectorsTakeGarrison": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| s | `{"supplyRuns": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| se | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| ses | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "supplyRuns": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| sesx | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "supplyRuns": 1, "exploreRegion": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
