# A/B test (6 configs, seeds 42)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 0 | 6 | 0.525 ± 0.139 | - | 0.351 ± 0.095 | - | 24 |
| g | `{"collectorsTakeGarrison": 0}` | 24 | 0 | 3 | 0.499 ± 0.114 | -0.026 ± 0.146 | 0.352 ± 0.082 | +0.001 ± 0.121 | 24 |
| s | `{"supplyRuns": 1}` | 23 | 0 | 4 | 0.593 ± 0.133 | +0.081 ± 0.158 | 0.364 ± 0.090 | +0.023 ± 0.123 | 23 |
| se | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| ses | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "supplyRuns": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| sesx | `{"fetchByShare": 1, "unlockDirect": 1, "regionValue": 8000, "mainHorizon": 12, "supplyRuns": 1, "exploreRegion": 1}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
