# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 0 | 0 | 0.377 ± 0.100 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 0 | 0 | 0.398 ± 0.100 | +0.021 ± 0.122 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 12 | 0 | 0 | 0.308 ± 0.089 | -0.069 ± 0.093 | 12 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 12 | 0 | 1 | 0.320 ± 0.102 | -0.057 ± 0.103 | 12 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 12 | 0 | 0 | 0.510 ± 0.051 | +0.134 ± 0.101 | 12 |
