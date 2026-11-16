# A/B test (2 configs, seeds 42, 43)

Paired against `path`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| path | `{}` | 72 | 7 | 18 | 0.475 ± 0.082 | - | 72 |
| eucl | `{"pathThreats": 0}` | 0 | 0 | 0 | nan ± nan | - | 0 |
