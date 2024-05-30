// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved

#include <linux/kernel.h>
#include <linux/slab.h>
#include "en.h"

struct mlx5e_zcrx_ifq {
	u16 qid;
	void *iou_ifq;
};

struct mlx5e_zcrx {
	u32 max_nch;
	struct mlx5e_zcrx_ifq *ifqs;
	u16 nifqs;
};

static struct mlx5e_zcrx *mlx5e_zcrx_alloc(u32 max_nch)
{
	struct mlx5e_zcrx *zcrx;
	zcrx = kzalloc(sizeof(*zcrx), GFP_KERNEL);
	if (!zcrx)
		return NULL;
	zcrx->max_nch = max_nch;
	zcrx->ifqs = kcalloc(max_nch, sizeof(*zcrx->ifqs), GFP_KERNEL);
	if (!zcrx->ifqs) {
		kfree(zcrx);
		return NULL;
	}
	return zcrx;

}

static void mlx5e_zcrx_free(struct mlx5e_zcrx *zcrx)
{
	kfree(zcrx->ifqs);
	kfree(zcrx);
}


static void mlx5e_zcrx_clear_ifq(struct mlx5e_priv *priv, u16 qid)
{
	struct mlx5e_zcrx *zcrx = priv->zcrx;

	if (WARN_ONCE(!zcrx, "zcrx is not allocated\n"))
		return;

	zcrx->ifqs[qid].iou_ifq = NULL;

	if (--zcrx->nifqs)
		return;

	mlx5e_zcrx_free(zcrx);
	priv->zcrx = NULL;
}

int mlx5e_zcrx_set_ifq(struct mlx5e_priv *priv, u16 qid, void *iou_ifq) __must_hold(&priv->state_lock)
{
	struct mlx5e_zcrx *zcrx;

	if (WARN_ONCE(!mutex_is_locked(&priv->state_lock), "state_lock is not held\n"))
		return -EINVAL;

	if (!iou_ifq) {
		mlx5e_zcrx_clear_ifq(priv, qid);
		return 0;
	}

	if (!priv->zcrx) {
		priv->zcrx = mlx5e_zcrx_alloc(priv->max_nch);
		if (!priv->zcrx)
			return -ENOMEM;
	}

	zcrx = priv->zcrx;

	if (qid >= zcrx->max_nch)
		return -EINVAL;

	if (zcrx->ifqs[qid].iou_ifq)
		return -EBUSY;

	zcrx->ifqs[qid].qid = qid;
	zcrx->ifqs[qid].iou_ifq = iou_ifq;
	zcrx->nifqs++;
	return 0;
}

void *mlx5e_zcrx_get_iou_ifq(struct mlx5e_priv *priv, u16 qid) __must_hold(&priv->state_lock)
{
	struct mlx5e_zcrx *zcrx = priv->zcrx;

	if (WARN_ONCE(!mutex_is_locked(&priv->state_lock), "state_lock is not held\n"))
		return NULL;

	if (!zcrx || qid >= zcrx->max_nch)
		return NULL;

	return zcrx->ifqs[qid].iou_ifq;
}

bool mlx5e_zcrx_is_active(struct mlx5e_priv *priv) __must_hold(&priv->state_lock)
{
	WARN_ONCE(!mutex_is_locked(&priv->state_lock), "state_lock is not held\n");

	return priv->zcrx && priv->zcrx->nifqs;
}