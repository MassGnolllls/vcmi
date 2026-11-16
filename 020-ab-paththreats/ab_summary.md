# A/B test (2 configs, seeds 42, 43)

Paired against `path`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| path | `{}` | 144 | 9 | 46 | 0.407 ± 0.056 | - | 144 |
| eucl | `{"pathThreats": 0}` | 71 | 2 | 27 | 0.366 ± 0.079 | -0.104 ± 0.073 | 71 |
