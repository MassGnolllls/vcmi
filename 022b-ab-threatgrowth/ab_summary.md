# A/B test (2 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 13 | 46 | 0.421 ± 0.058 | - | 144 |
| nogrowth | `{"threatGrowth": 0}` | 72 | 9 | 20 | 0.425 ± 0.084 | +0.002 ± 0.070 | 72 |
