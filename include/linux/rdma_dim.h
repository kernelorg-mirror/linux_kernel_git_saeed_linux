/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef RDMA_DIM_H
#define RDMA_DIM_H

#include <linux/module.h>
#include <linux/dim.h>

#define RDMA_DIM_PARAMS_NUM_PROFILES 9
#define RDMA_DIM_START_PROFILE 0

static const struct dim_cq_moder
rdma_dim_prof[RDMA_DIM_PARAMS_NUM_PROFILES] = {
	{1,   0, 1,  0},
	{1,   0, 4,  0},
	{2,   0, 4,  0},
	{2,   0, 8,  0},
	{4,   0, 8,  0},
	{16,  0, 8,  0},
	{16,  0, 16, 0},
	{32,  0, 16, 0},
	{32,  0, 32, 0},
};

void rdma_dim(struct dim *dim, u64 completions);

#endif /* RDMA_DIM_H */
