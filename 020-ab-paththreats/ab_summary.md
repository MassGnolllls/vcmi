# A/B test (2 configs, seeds 42, 43)

Paired against `path`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| path | `{}` | 144 | 9 | 46 | 0.407 ± 0.056 | - | 144 |
| eucl | `{"pathThreats": 0}` | 142 | 5 | 50 | 0.371 ± 0.054 | -0.035 ± 0.053 | 142 |
