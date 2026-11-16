# A/B test (4 configs, seeds 42, 43, 44, 45, 46)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 31 | 37 | 0.510 ± 0.063 | - | 144 |
| osc0 | `{"antiOscillation": 0}` | 144 | 19 | 41 | 0.462 ± 0.059 | -0.048 ± 0.059 | 144 |
| conc1 | `{"concentrate": 1}` | 144 | 31 | 34 | 0.537 ± 0.062 | +0.026 ± 0.052 | 144 |
| siege1 | `{"siegeCost": 1}` | 144 | 25 | 37 | 0.505 ± 0.061 | -0.005 ± 0.044 | 144 |
