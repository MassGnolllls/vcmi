# A/B test (2 configs, seeds 42, 43)

Paired against `guard`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| guard | `{}` | 144 | 15 | 42 | 0.417 ± 0.056 | - | 144 |
| old | `{"townGuard": 0}` | 72 | 3 | 23 | 0.383 ± 0.075 | -0.011 ± 0.070 | 72 |
