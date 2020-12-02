/*
 * Copyright (c) 2020 NVIDIA Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MOV_AVG_H
#define _MOV_AVG_H 1

#include <math.h>

/* Moving average helpers. */

/* Cumulative Moving Average.
 *
 * Also called Simple Moving Average.
 * Online equivalent of sum(V) / len(V).
 *
 * As all values have equal weight, this average will
 * be slower to show recent changes in the series.
 *
 */

struct mov_avg_cma {
    unsigned long long int count;
    double mean;
    double sum_dsquared;
};

#define MOV_AVG_CMA_INITIALIZER \
    { .count = 0, .mean = .0, .sum_dsquared = .0 }

static inline void
mov_avg_cma_init(struct mov_avg_cma *cma)
{
    *cma = (struct mov_avg_cma)MOV_AVG_CMA_INITIALIZER;
}

static inline void
mov_avg_cma_update(struct mov_avg_cma *cma, double new_val)
{
    double mean;

    cma->count++;
    mean = cma->mean + (new_val - cma->mean) / cma->count;

    cma->sum_dsquared += (new_val - mean) * (new_val - cma->mean);
    cma->mean = mean;
}

static inline double
mov_avg_cma(struct mov_avg_cma *cma)
{
    return cma->mean;
}

static inline double
mov_avg_cma_std_dev(struct mov_avg_cma *cma)
{
    double variance = 0.0;

    if (cma->count > 1) {
        variance = cma->sum_dsquared / (cma->count - 1);
    }

    return sqrt(variance);
}

/* Exponential Moving Average.
 *
 * Each value in the series has an exponentially decreasing weight,
 * the older they get the less weight they have.
 *
 * The smoothing factor 'alpha' must be within 0 < alpha < 1.
 * The closer this factor to zero, the more equal the weight between
 * recent and older values. As it approaches one, the more recent values
 * will have more weight.
 *
 * The EMA can be thought of as an estimator for the next value when measures
 * are dependent. In that sense, it can make sense to consider the mean square
 * error of the prediction. An 'alpha' minimizing this error would be the
 * better choice to improve the estimation.
 *
 * A common choice for 'alpha' is to derive it from the 'N' past periods that
 * are interesting for the average. The following formula is used
 *
 *   a = 2 / (N + 1)
 *
 * It makes the 'N' previous values weigh approximatively 86% of the average.
 * Using the above formula is common practice but arbitrary. When doing so,
 * it should be noted that the EMA will not forget about past values beyond 'N',
 * only that their weight will be reduced.
 */

struct mov_avg_ema {
    double alpha; /* 'Smoothing' factor. */
    double mean;
    double variance;
    bool initialized;
};

/* Choose alpha explicitly. */
#define MOV_AVG_EMA_INITIALIZER_ALPHA(a) { \
    .initialized = false, \
    .alpha = (a), .variance = .0, .mean = .0 \
}

/* Choose alpha from 'N' past periods. */
#define MOV_AVG_EMA_INITIALIZER(n_elem) \
    MOV_AVG_EMA_INITIALIZER_ALPHA(2. / ((double)(n_elem) + 1.))

static inline void
mov_avg_ema_init_alpha(struct mov_avg_ema *ema,
                       double alpha)
{
    *ema = (struct mov_avg_ema)MOV_AVG_EMA_INITIALIZER_ALPHA(alpha);
}

static inline void
mov_avg_ema_init(struct mov_avg_ema *ema,
                 unsigned long long int n_elem)
{
    *ema = (struct mov_avg_ema)MOV_AVG_EMA_INITIALIZER(n_elem);
}

static inline void
mov_avg_ema_update(struct mov_avg_ema *ema, double new_val)
{
    const double alpha = ema->alpha;
    double diff;

    if (!ema->initialized) {
        ema->initialized = true;
        ema->mean = new_val;
        return;
    }

    diff = new_val - ema->mean;

    ema->variance = (1.0 - alpha) * (ema->variance + alpha * diff * diff);
    ema->mean = ema->mean + alpha * diff;
}

static inline double
mov_avg_ema(struct mov_avg_ema *ema)
{
    return ema->mean;
}

static inline double
mov_avg_ema_std_dev(struct mov_avg_ema *ema)
{
    return sqrt(ema->variance);
}

#endif /* _MOV_AVG_H */
