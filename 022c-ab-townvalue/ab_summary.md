# A/B test (2 configs, seeds 42, 43)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 144 | 12 | 45 | 0.426 ± 0.058 | - | 144 |
| towns25 | `{"neutralTownValue": 25000, "enemyTownValue": 35000}` | 144 | 16 | 40 | 0.438 ± 0.057 | +0.013 ± 0.058 | 144 |
