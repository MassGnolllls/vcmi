# A/B test (16 configs, seeds 58)

Paired against `new`: mean difference over the games both played (95% CI). score = army share at the day limit, towns = share of the towns held; both 1 for a win and 0 for a loss.

| config | params | games | won | lost | score | diff vs base | towns | diff vs base | pairs |
|---|---|---|---|---|---|---|---|---|---|
| new | `{}` | 24 | 13 | 1 | 0.786 ± 0.123 | - | 0.786 ± 0.127 | - | 24 |
| nZHS | `{"mainHorizon": 12, "heroLossPause": 0, "scouts": 3}` | 24 | 12 | 4 | 0.692 ± 0.152 | -0.094 ± 0.121 | 0.685 ± 0.163 | -0.101 ± 0.126 | 24 |
| nFMHS | `{"fetchByShare": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "scouts": 3, "collectorsRoam": 3}` | 24 | 11 | 3 | 0.666 ± 0.155 | -0.121 ± 0.157 | 0.655 ± 0.153 | -0.131 ± 0.152 | 24 |
| nFZM | `{"fetchByShare": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "collectorsRoam": 3}` | 24 | 11 | 2 | 0.680 ± 0.144 | -0.106 ± 0.155 | 0.685 ± 0.150 | -0.101 ± 0.167 | 24 |
| nUMH | `{"unlockDirect": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "collectorsRoam": 3}` | 24 | 11 | 5 | 0.651 ± 0.167 | -0.136 ± 0.186 | 0.645 ± 0.184 | -0.140 ± 0.205 | 24 |
| nUZMS | `{"unlockDirect": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "scouts": 3, "collectorsRoam": 3}` | 24 | 13 | 3 | 0.705 ± 0.154 | -0.081 ± 0.199 | 0.679 ± 0.167 | -0.107 ± 0.209 | 24 |
| nUFS | `{"unlockDirect": 1, "fetchByShare": 1, "scouts": 3}` | 24 | 15 | 4 | 0.740 ± 0.156 | -0.047 ± 0.149 | 0.720 ± 0.166 | -0.065 ± 0.171 | 24 |
| nUFZH | `{"unlockDirect": 1, "fetchByShare": 1, "mainHorizon": 12, "heroLossPause": 0}` | 24 | 12 | 4 | 0.686 ± 0.160 | -0.100 ± 0.180 | 0.679 ± 0.161 | -0.107 ± 0.179 | 24 |
| nPMS | `{"passage": 1, "regionValue": 8000, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "scouts": 3, "collectorsRoam": 3}` | 24 | 13 | 1 | 0.707 ± 0.142 | -0.080 ± 0.146 | 0.696 ± 0.154 | -0.089 ± 0.161 | 24 |
| nPZMH | `{"passage": 1, "regionValue": 8000, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "collectorsRoam": 3}` | 24 | 11 | 5 | 0.592 ± 0.173 | -0.195 ± 0.177 | 0.583 ± 0.175 | -0.202 ± 0.197 | 24 |
| nPFH | `{"passage": 1, "regionValue": 8000, "fetchByShare": 1, "heroLossPause": 0}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPFZS | `{"passage": 1, "regionValue": 8000, "fetchByShare": 1, "mainHorizon": 12, "scouts": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUHS | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "heroLossPause": 0, "scouts": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUZ | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "mainHorizon": 12}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUFM | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "fetchByShare": 1, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
| nPUFZMHS | `{"passage": 1, "regionValue": 8000, "unlockDirect": 1, "fetchByShare": 1, "mainHorizon": 12, "heroesWeek1": 3, "heroesWeek3": 5, "heroesLater": 6, "heroLossPause": 0, "scouts": 3, "collectorsRoam": 3}` | 0 | 0 | 0 | nan ± nan | - | nan ± nan | - | 0 |
