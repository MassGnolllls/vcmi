# A/B test (16 configs, seeds 58)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 13 | 1 | 0.786 ± 0.123 | - | 0.786 ± 0.127 | - | 24 |
| nZHS | `{"mainHorizon": 12, "heroLossPause": 0, "scouts": 3}` | 24 | 12 | 4 | 0.692 ± 0.152 | -0.094 ± 0.121 | 0.685 ± 0.163 | -0.101 ± 0.126 | 24 |
| nFMHS | `{"fetchByShare": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "scouts": 3, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nFZM | `{"fetchByShare": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nUMH | `{"unlockDirect": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nUZMS | `{"unlockDirect": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "scouts": 3, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nUFS | `{"unlockDirect": 1, "fetchByShare": 1, "scouts": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nUFZH | `{"unlockDirect": 1, "fetchByShare": 1, "mainHorizon": 12, "heroLossPause": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPMS | `{"passage": 1, "regionValue": 8000, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "scouts": 3, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPZMH | `{"passage": 1, "regionValue": 8000, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPFH | `{"passage": 1, "regionValue": 8000, "fetchByShare": 1, "heroLossPause": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPFZS | `{"passage": 1, "regionValue": 8000, "fetchByShare": 1, "mainHorizon": 12, "scouts": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUHS | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "heroLossPause": 0, "scouts": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUZ | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUFM | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "fetchByShare": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUFZMHS | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "fetchByShare": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "scouts": 3, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
