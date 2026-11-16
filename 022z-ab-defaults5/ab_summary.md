# A/B test (4 configs, seeds 42, 43, 44, 45, 46)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 216 | 45 | 56 | 0.520 ± 0.052 | - | 216 |
| osc0 | `{"antiOscillation": 0}` | 216 | 25 | 71 | 0.435 ± 0.049 | -0.085 ± 0.052 | 216 |
| conc1 | `{"concentrate": 1}` | 216 | 46 | 55 | 0.533 ± 0.052 | +0.013 ± 0.043 | 216 |
| siege1 | `{"siegeCost": 1}` | 216 | 41 | 59 | 0.512 ± 0.051 | -0.008 ± 0.043 | 216 |
