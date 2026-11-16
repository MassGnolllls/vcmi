# A/B test (5 configs, seeds 42)

Paired against `base`: mean score difference over the games both played (95% CI).

| config | params | games | won | lost | score | diff vs base | pairs |
|---|---|---|---|---|---|---|---|
| base | `{}` | 12 | 0 | 3 | 0.222 ± 0.115 | - | 12 |
| econ | `{"incomeHorizonDays": 28, "earlyGoldDays": 21}` | 12 | 0 | 0 | 0.475 ± 0.096 | +0.253 ± 0.159 | 12 |
| h3 | `{"heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| ffv | `{"fightForecast": 1, "guardKillValue": 0.3}` | 0 | 0 | 0 | nan ± nan | - | 0 |
| combo | `{"incomeHorizonDays": 28, "earlyGoldDays": 21, "heroesWeek1": 3, "heroesWeek3": 4, "heroesLater": 6, "collectorsRoam": 2, "couriers": 1, "sellSurplus": 1}` | 0 | 0 | 0 | nan ± nan | - | 0 |
