# A/B test (4 configs, seeds 42, 43, 44, 45, 46)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 360 | 72 | 93 | 0.513 ± 0.040 | - | 360 |
| osc0 | `{"antiOscillation": 0}` | 359 | 43 | 115 | 0.435 ± 0.038 | -0.077 ± 0.042 | 359 |
| conc1 | `{"concentrate": 1}` | 360 | 67 | 95 | 0.510 ± 0.040 | -0.003 ± 0.033 | 360 |
| siege1 | `{"siegeCost": 1}` | 360 | 68 | 97 | 0.503 ± 0.039 | -0.009 ± 0.035 | 360 |
