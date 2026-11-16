# A/B test (2 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 13 | 46 | 0.421 ± 0.058 | - | 144 |
| nogrowth | `{"threatGrowth": 0}` | 144 | 16 | 50 | 0.394 ± 0.060 | -0.026 ± 0.049 | 144 |
