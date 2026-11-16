# A/B test (2 configs, seeds 42, 43)

Paired against `siege`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| siege | `{}` | 72 | 4 | 26 | 0.353 ± 0.077 | - | 72 |
| nocost | `{"siegeCost": 0}` | 72 | 9 | 23 | 0.423 ± 0.086 | +0.069 ± 0.070 | 72 |
