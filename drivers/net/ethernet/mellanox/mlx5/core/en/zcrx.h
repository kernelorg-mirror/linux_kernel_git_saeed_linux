/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef __MLX5E_ZCRX_H__
#define __MLX5E_ZCRX_H__
#include <linux/kernel.h>

struct mlx5e_priv;
struct mlx5e_zcrx_ifq;
struct mlx5e_zcrx;

int mlx5e_zcrx_set_ifq(struct mlx5e_priv *priv, u16 qid, void *iou_ifq);
void *mlx5e_zcrx_get_iou_ifq(struct mlx5e_priv *priv, u16 qid);
bool mlx5e_zcrx_is_active(struct mlx5e_priv *priv);

#endif /* __MLX5E_ZCRX_H__ */