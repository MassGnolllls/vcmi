# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 23 | 19 | 3 | 0.869 ± 0.141 | - | 0.862 ± 0.140 | - | 23 |
| X | `{"scouts": 2}` | 24 | 16 | 7 | 0.704 ± 0.185 | -0.178 ± 0.200 | 0.701 ± 0.184 | -0.174 ± 0.202 | 23 |
