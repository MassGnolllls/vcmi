# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 6 | 4 | 0.617 ± 0.265 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 2 | 4 | 0.403 ± 0.222 | -0.215 ± 0.313 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 12 | 1 | 4 | 0.446 ± 0.208 | -0.171 ± 0.244 | 12 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 12 | 5 | 3 | 0.625 ± 0.243 | +0.008 ± 0.196 | 12 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 12 | 3 | 5 | 0.425 ± 0.248 | -0.192 ± 0.291 | 12 |
