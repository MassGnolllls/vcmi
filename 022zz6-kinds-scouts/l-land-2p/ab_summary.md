# A/B test (2 configs, seeds 91, 92)

Paired against `base`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| base | `{}` | 23 | 1 | 22 | 0.043 ± 0.085 | - | 0.043 ± 0.085 | - | 23 |
| X | `{"scouts": 2}` | 22 | 4 | 17 | 0.198 ± 0.165 | +0.152 ± 0.196 | 0.188 ± 0.164 | +0.143 ± 0.195 | 22 |
