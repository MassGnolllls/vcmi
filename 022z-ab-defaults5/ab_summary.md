# A/B test (4 configs, seeds 42, 43, 44, 45, 46)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 288 | 58 | 76 | 0.511 ± 0.045 | - | 288 |
| osc0 | `{"antiOscillation": 0}` | 287 | 35 | 90 | 0.438 ± 0.042 | -0.073 ± 0.045 | 287 |
| conc1 | `{"concentrate": 1}` | 288 | 58 | 75 | 0.520 ± 0.045 | +0.008 ± 0.037 | 288 |
| siege1 | `{"siegeCost": 1}` | 288 | 56 | 76 | 0.512 ± 0.044 | +0.001 ± 0.038 | 288 |
