// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 */

#include <linux/rdma_dim.h>

/**
 ** rdma_dim_step: - Moves the moderation profile one step.
 ** @dim: The moderation struct.
 **
 ** Description: Moves the moderation profile of @dim by one step. If we
 ** are at the edge of the profile range returns DIM_ON_EDGE without
 ** moving.
 **/
static int rdma_dim_step(struct dim *dim)
{
	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
		/* fall through */
	case DIM_PARKING_TIRED:
		break;
	case DIM_GOING_RIGHT:
		if (dim->profile_ix == (RDMA_DIM_PARAMS_NUM_PROFILES - 1))
			return DIM_ON_EDGE;
		dim->profile_ix++;
		dim->steps_right++;
		break;
	case DIM_GOING_LEFT:
		if (dim->profile_ix == 0)
			return DIM_ON_EDGE;
		dim->profile_ix--;
		dim->steps_left++;
		break;
	}

	return DIM_STEPPED;
}

/**
 ** rdma_dim_stats_compare - Compares the current stats to the previous stats.
 ** @curr: The current dim stats.
 ** @prev: The previous dim stats.
 **
 ** Description: Checks to see if we have significantly more or less
 ** completions.
 ** If the completions are not greatly changed checks if the completion to
 ** event ratio has significantly changed.
 **/
static int rdma_dim_stats_compare(struct dim_stats *curr,
				  struct dim_stats *prev)
{
	/* first stat */
	if (!prev->cpms)
		return DIM_STATS_SAME;

	if (IS_SIGNIFICANT_DIFF(curr->cpms, prev->cpms))
		return (curr->cpms > prev->cpms) ? DIM_STATS_BETTER :
						DIM_STATS_WORSE;

	if (IS_SIGNIFICANT_DIFF(curr->cpe_ratio, prev->cpe_ratio))
		return (curr->cpe_ratio > prev->cpe_ratio) ? DIM_STATS_BETTER :
						DIM_STATS_WORSE;

	return DIM_STATS_SAME;
}

/**
 ** rdma_dim_decision - Decides the next moderation level.
 ** @curr_stats: The current dim stats.
 ** @dim: The moderation struct.
 **
 ** Description: Uses rdma_dim_stats_compare to decide what the next moderation
 ** level should be. If the completion to event ratio is low compared to the
 ** current level we reset the moderation to keep latency low.
 **/
static bool rdma_dim_decision(struct dim_stats *curr_stats, struct dim *dim)
{
	int prev_ix = dim->profile_ix;
	int stats_res;
	int step_res;

	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
		/* fall through */
	case DIM_PARKING_TIRED:
		break;
	case DIM_GOING_RIGHT:
		/* fall through */
	case DIM_GOING_LEFT:
		stats_res = rdma_dim_stats_compare(curr_stats,
						   &dim->prev_stats);

		switch (stats_res) {
		case DIM_STATS_SAME:
			if (curr_stats->cpe_ratio <= 50 * prev_ix)
				dim->profile_ix = 0;
			break;
		case DIM_STATS_WORSE:
			dim_turn(dim);
			/* fall through */
		case DIM_STATS_BETTER:
			step_res = rdma_dim_step(dim);
			if (step_res == DIM_ON_EDGE)
				dim_turn(dim);
			break;
		}
		break;
	}

	dim->prev_stats = *curr_stats;

	return dim->profile_ix != prev_ix;
}

/**
 ** rdma_dim - Runs the adaptive moderation.
 ** @dim: The moderation struct.
 ** @completions: The number of completions collected in this round.
 **
 ** Description: Each call to rdma_dim takes the latest amount of
 ** completions that have been collected and counts them as a new event.
 ** Once enough events have been collected the algorithm decides a new
 ** moderation level.
 **/
void rdma_dim(struct dim *dim, u64 completions)
{
	struct dim_stats curr_stats;
	u32 nevents;
	struct dim_sample *curr_sample = &dim->measuring_sample;

	dim_update_sample_with_comps(curr_sample->event_ctr + 1,
				     curr_sample->pkt_ctr,
				     curr_sample->byte_ctr,
				     curr_sample->comp_ctr + completions,
				     &dim->measuring_sample);

	switch (dim->state) {
	case DIM_MEASURE_IN_PROGRESS:
		nevents = curr_sample->event_ctr - dim->start_sample.event_ctr;
		if (nevents < DIM_NEVENTS)
			break;
		dim_calc_stats(&dim->start_sample, curr_sample, &curr_stats);
		if (rdma_dim_decision(&curr_stats, dim)) {
			dim->state = DIM_APPLY_NEW_PROFILE;
			schedule_work(&dim->work);
			break;
		}
		/* fall through */
	case DIM_START_MEASURE:
		dim->state = DIM_MEASURE_IN_PROGRESS;
		dim_update_sample_with_comps(curr_sample->event_ctr,
					     curr_sample->pkt_ctr,
					     curr_sample->byte_ctr,
					     curr_sample->comp_ctr,
					     &dim->start_sample);
		break;
	case DIM_APPLY_NEW_PROFILE:
		break;
	}
}
EXPORT_SYMBOL(rdma_dim);
