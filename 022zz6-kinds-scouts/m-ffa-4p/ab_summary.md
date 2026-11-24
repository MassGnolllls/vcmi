# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 10 | 2 | 0.833 ± 0.220 | - | 0.833 ± 0.220 | - | 12 |
| X | `{"scouts": 2}` | 12 | 7 | 4 | 0.659 ± 0.276 | -0.174 ± 0.326 | 0.653 ± 0.274 | -0.181 ± 0.325 | 12 |
