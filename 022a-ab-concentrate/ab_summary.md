# A/B test (2 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 10 | 47 | 0.408 ± 0.057 | - | 144 |
| old | `{"concentrate": 0}` | 144 | 17 | 37 | 0.454 ± 0.057 | +0.046 ± 0.051 | 144 |
