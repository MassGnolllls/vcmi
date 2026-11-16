# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 0 | 8 | 0.064 ± 0.059 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 2 | 2 | 0.510 ± 0.212 | +0.446 ± 0.214 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 12 | 0 | 5 | 0.174 ± 0.173 | +0.110 ± 0.203 | 12 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 12 | 0 | 8 | 0.198 ± 0.194 | +0.134 ± 0.202 | 12 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 12 | 0 | 2 | 0.508 ± 0.218 | +0.444 ± 0.243 | 12 |
