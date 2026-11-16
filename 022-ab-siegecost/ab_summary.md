# A/B test (2 configs, seeds 42, 43)

Paired against `siege`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| siege | `{}` | 144 | 11 | 54 | 0.355 ± 0.058 | - | 144 |
| nocost | `{"siegeCost": 0}` | 144 | 18 | 51 | 0.410 ± 0.061 | +0.055 ± 0.055 | 144 |
