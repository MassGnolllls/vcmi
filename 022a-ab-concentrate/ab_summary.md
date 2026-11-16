# A/B test (2 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 10 | 47 | 0.408 ± 0.057 | - | 144 |
| old | `{"concentrate": 0}` | 72 | 8 | 18 | 0.463 ± 0.079 | +0.029 ± 0.073 | 72 |
