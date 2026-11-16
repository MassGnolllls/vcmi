# A/B test (4 configs, seeds 42, 43, 44, 45, 46)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 72 | 15 | 19 | 0.511 ± 0.090 | - | 72 |
| osc0 | `{"antiOscillation": 0}` | 72 | 10 | 22 | 0.455 ± 0.085 | -0.056 ± 0.081 | 72 |
| conc1 | `{"concentrate": 1}` | 72 | 17 | 14 | 0.562 ± 0.086 | +0.051 ± 0.075 | 72 |
| siege1 | `{"siegeCost": 1}` | 72 | 13 | 17 | 0.526 ± 0.087 | +0.015 ± 0.068 | 72 |
