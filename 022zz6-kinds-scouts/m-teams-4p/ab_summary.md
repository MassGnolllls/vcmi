# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 24 | 22 | 2 | 0.917 ± 0.113 | - | 0.917 ± 0.113 | - | 24 |
| X | `{"scouts": 2}` | 24 | 19 | 5 | 0.792 ± 0.166 | -0.125 ± 0.215 | 0.792 ± 0.166 | -0.125 ± 0.215 | 24 |
