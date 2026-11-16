# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 0 | 1 | 0.309 ± 0.074 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 0 | 0 | 0.347 ± 0.068 | +0.038 ± 0.049 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 12 | 0 | 0 | 0.328 ± 0.088 | +0.018 ± 0.058 | 12 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 12 | 0 | 1 | 0.266 ± 0.069 | -0.043 ± 0.060 | 12 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 12 | 0 | 0 | 0.386 ± 0.075 | +0.077 ± 0.065 | 12 |
