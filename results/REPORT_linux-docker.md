### Queue handoff latency (linux-docker)

Paced 1,000,000 msg/s, 2,000,000 msgs, median of 5 reps; cache line 64 B; clock tick 41.667 ns; producer: pinned core 2; consumer: pinned core 4

| variant | p50 ns | p99 ns | p99.9 ns | p99.99 ns | max ns | Mmsg/s (unpaced) |
|---|---|---|---|---|---|---|
| spsc-padded | 83 | 1917 | 13709 | 38667 | 59709 | 18.62 |
| spsc-unpadded | 125 | 1917 | 15875 | 41542 | 131375 | 20.33 |
| mutex-poll | 583 | 10833 | 53500 | 179917 | 296958 | 6.37 |
| mutex-condvar | 3208 | 12750 | 48000 | 189500 | 319792 | 6.52 |

### ITCH replay, paced (linux-docker)

2,969,471 book events / 3,000,000 frames; rate 1,000,000 msg/s; median of 3 reps; producer=pinned core 2 | consumer=pinned core 4

| book | mode | ns/msg | p50 | p99 | p99.9 | p99.99 | max | Mmsg/s | checksum |
|---|---|---|---|---|---|---|---|---|---|
| map-heap | single | 84.59 | 84 | 334 | 500 | 4167 | 28916 | 11.822 | 8841099fd73382f2 |
| map-heap | pipeline | 1000.0 | 208 | 666 | 8125 | 16958 | 58125 | 1.0 | 8841099fd73382f2 |
| map-pool | single | 83.45 | 125 | 292 | 375 | 4875 | 47625 | 11.983 | 8841099fd73382f2 |
| map-pool | pipeline | 1000.0 | 250 | 709 | 9250 | 47084 | 156167 | 1.0 | 8841099fd73382f2 |
| flat-heap | single | 43.43 | 42 | 167 | 250 | 1292 | 22458 | 23.026 | 8841099fd73382f2 |
| flat-heap | pipeline | 1000.0 | 167 | 625 | 9083 | 22541 | 49917 | 1.0 | 8841099fd73382f2 |
| flat-pool | single | 54.64 | 125 | 250 | 333 | 4000 | 45500 | 18.303 | 8841099fd73382f2 |
| flat-pool | pipeline | 1000.0 | 208 | 500 | 8916 | 23916 | 49916 | 1.0 | 8841099fd73382f2 |

### ITCH replay, unpaced (linux-docker)

2,969,471 book events / 3,000,000 frames; rate 0 msg/s; median of 3 reps; producer=pinned core 2 | consumer=pinned core 4

| book | mode | ns/msg | p50 | p99 | p99.9 | p99.99 | max | Mmsg/s | checksum |
|---|---|---|---|---|---|---|---|---|---|
| map-heap | pipeline | 132.27 | 2245917 | 2451959 | 2490834 | 2495750 | 2497833 | 7.56 | 8841099fd73382f2 |
| map-pool | pipeline | 180.12 | 3103792 | 3434000 | 3525875 | 3531417 | 3533333 | 5.552 | 8841099fd73382f2 |
| flat-heap | pipeline | 88.8 | 1498334 | 1798542 | 2074417 | 2080500 | 2081125 | 11.261 | 8841099fd73382f2 |
| flat-pool | pipeline | 117.44 | 1993250 | 2122166 | 2282125 | 2287584 | 2289167 | 8.515 | 8841099fd73382f2 |

