# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 1 | 7 | 0.197 ± 0.173 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
