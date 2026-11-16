# A/B test (2 configs, seeds 42, 43)

Paired against `guard`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| guard | `{}` | 144 | 15 | 42 | 0.417 ± 0.056 | - | 144 |
| old | `{"townGuard": 0}` | 144 | 5 | 41 | 0.412 ± 0.052 | -0.005 ± 0.050 | 144 |
