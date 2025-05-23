/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <executorch/backends/vulkan/runtime/graph/ops/OperatorRegistry.h>

#include <executorch/backends/vulkan/runtime/graph/ops/impl/Copy.h>

#include <executorch/backends/vulkan/runtime/graph/ops/impl/utils/DimUtils.h>
#include <executorch/backends/vulkan/runtime/graph/ops/impl/utils/KernelUtils.h>
#include <executorch/backends/vulkan/runtime/graph/ops/impl/utils/TensorUtils.h>
#include <executorch/backends/vulkan/runtime/graph/ops/utils/ShaderNameUtils.h>

namespace vkcompute {

void add_split_with_sizes_default_node(
    ComputeGraph& graph,
    ValueRef in,
    const std::vector<int64_t>& split_sizes,
    int64_t dim,
    ValueRef out_list_ref) {
  vTensorPtr t_in = graph.get_tensor(in);

  ValueListPtr out_list = graph.get_value_list(out_list_ref);

  DimIndex dim_index = normalize_to_dim_index(*t_in, dim);

  VK_CHECK_COND(out_list->size() == split_sizes.size());

  for (int split_idx = 0; split_idx < split_sizes.size(); split_idx++) {
    int64_t split_size = split_sizes[split_idx];
    ValueRef out_ref = (*out_list)[split_idx];

    vTensorPtr t_out = graph.get_tensor(out_ref);
    VK_CHECK_COND(dim_at(*t_out, dim_index) == split_size);
  }

  const auto packed_dim = t_in->packed_dim();
  const auto packed_dim_index = static_cast<DimIndex>(kWidth4D - packed_dim);

  // Index of dimension to be concatenated in (w, h, c * b) coordinate system
  const auto dim_xyz_index = std::min(2, -dim_index - 1);

  utils::ivec4 src_offset = utils::make_ivec4({0, 0, 0, 0}, false);
  utils::ivec4 dst_offset = utils::make_ivec4({0, 0, 0, 0}, false);

  const bool is_splitting_channel = (dim_index == kChannel4D);

  // if splitting channels
  if (is_splitting_channel) {
    // set source offset w as channel size of the input tensor
    src_offset[3] = dim_at(t_in->sizes(), kChannel4D);
  }

  for (ValueRef out_ref : *out_list) {
    // Doesn't need to use split_size since we have already verified that the
    // output tensor's size matches with the split_size.
    vTensorPtr t_out = graph.get_tensor(out_ref);
    const auto out_channel_size = dim_at(t_out->sizes(), kChannel4D);
    utils::ivec3 range = t_out->logical_limits();

    if (dim_index == packed_dim_index) {
      // if splitting channels, use add_copy_channel_offset_node function as
      // add_copy_packed_dim_offset_node does not support channel packing
      if (is_splitting_channel) {
        add_copy_channel_offset_node(
            graph, in, out_channel_size, src_offset[2], dst_offset[2], out_ref);
        src_offset[dim_xyz_index] += out_channel_size;
      } else {
        // dst_offset[3] is not used now but will be used in the future when
        // add_copy_packed_dim_offset_node will support channel packing
        //
        // set destination offset w as channel size of the output tensor if
        // splitting channel
        dst_offset[3] = is_splitting_channel ? out_channel_size : 0;
        add_copy_packed_dim_offset_node(
            graph, in, range, src_offset, dst_offset, out_ref);
        src_offset[dim_xyz_index] += dim_at(t_out->sizes(), packed_dim_index);
      }
    } else {
      // set destination offset w as channel size of the output tensor if
      // splitting channels
      dst_offset[3] = is_splitting_channel ? out_channel_size : 0;
      add_copy_offset_node(
          graph, in, range, src_offset, dst_offset, out_ref, false, true);
      src_offset[dim_xyz_index] +=
          is_splitting_channel ? out_channel_size : range[dim_xyz_index];
    }
  }
}

void add_split_with_sizes_default_node(
    ComputeGraph& graph,
    ValueRef in,
    ValueRef split_sizes_ref,
    ValueRef dim_ref,
    ValueRef out) {
  int64_t dim = graph.extract_scalar<int64_t>(dim_ref);
  std::vector<int64_t> split_sizes = *(graph.get_int_list(split_sizes_ref));

  add_split_with_sizes_default_node(graph, in, split_sizes, dim, out);
}

void split_with_sizes_copy_default(
    ComputeGraph& graph,
    const std::vector<ValueRef>& args) {
  add_split_with_sizes_default_node(graph, args[0], args[1], args[2], args[3]);
}

void add_split_tensor_node(
    ComputeGraph& graph,
    ValueRef in,
    ValueRef split_size_ref,
    ValueRef dim_ref,
    ValueRef out) {
  int64_t split_size = graph.extract_scalar<int64_t>(split_size_ref);
  int64_t dim = graph.extract_scalar<int64_t>(dim_ref);

  vTensorPtr t_in = graph.get_tensor(in);
  DimIndex dim_index = normalize_to_dim_index(*t_in, dim);
  int64_t size = dim_at(*t_in, dim_index);
  std::vector<int64_t> split_sizes(size / split_size, split_size);

  add_split_with_sizes_default_node(graph, in, split_sizes, dim, out);
}

void split_tensor(ComputeGraph& graph, const std::vector<ValueRef>& args) {
  add_split_tensor_node(graph, args[0], args[1], args[2], args[3]);
}

REGISTER_OPERATORS {
  VK_REGISTER_OP(
      aten.split_with_sizes_copy.default, split_with_sizes_copy_default);
  VK_REGISTER_OP(aten.split.Tensor, split_tensor);
}

} // namespace vkcompute
