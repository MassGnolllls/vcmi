# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 1 | 7 | 0.197 ± 0.173 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 0 | 1 | 0.604 ± 0.148 | +0.407 ± 0.203 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 12 | 0 | 8 | 0.137 ± 0.125 | -0.060 ± 0.151 | 12 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 12 | 0 | 6 | 0.197 ± 0.161 | +0.000 ± 0.269 | 12 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 12 | 0 | 0 | 0.554 ± 0.090 | +0.357 ± 0.201 | 12 |
