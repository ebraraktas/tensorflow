/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/grappler/optimizers/data/map_fusion.h"

#include <functional>
#include <memory>

#include "tensorflow/core/framework/attr_value_util.h"
#include "tensorflow/core/framework/function_testlib.h"
#include "tensorflow/core/grappler/grappler_item.h"
#include "tensorflow/core/grappler/optimizers/data/graph_test_utils.h"
#include "tensorflow/core/grappler/optimizers/data/graph_utils.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/test.h"

namespace tensorflow {
namespace grappler {
namespace {

using graph_tests_utils::MakeMapNode;
using graph_tests_utils::MakeParallelMapV2Node;

constexpr char kConstOpName[] = "Const";

NodeDef CreateScalarConstNodeHelper(
    const std::string& node_name, DataType dtype,
    const std::function<void(TensorProto*)>& add_value) {
  NodeDef node;
  node.set_op(kConstOpName);
  node.set_name(node_name);

  (*node.mutable_attr())["dtype"].set_type(dtype);
  auto tensor = std::make_unique<tensorflow::TensorProto>();
  auto tensor_shape = std::make_unique<tensorflow::TensorShapeProto>();
  tensor->set_allocated_tensor_shape(tensor_shape.release());
  tensor->set_dtype(dtype);
  add_value(tensor.get());
  (*node.mutable_attr())["value"].set_allocated_tensor(tensor.release());

  return node;
}

TEST(MapFusionTest, FuseTwoMapNodesIntoOne) {
  using test::function::NDef;
  GrapplerItem item;
  item.graph = test::function::GDef(
      {NDef("start", "Const", {}, {{"value", 0}, {"dtype", DT_INT32}}),
       NDef("stop", "Const", {}, {{"value", 10}, {"dtype", DT_INT32}}),
       NDef("step", "Const", {}, {{"value", 1}, {"dtype", DT_INT32}}),
       NDef("range", "RangeDataset", {"start", "stop", "step"}, {}),
       MakeMapNode("map1", "range"), MakeMapNode("map2", "map1")},
      // FunctionLib
      {
          test::function::XTimesTwo(),
      });

  MapFusion optimizer;
  GraphDef output;
  TF_ASSERT_OK(optimizer.Optimize(nullptr, item, &output));
  EXPECT_TRUE(graph_utils::ContainsNodeWithOp("MapDataset", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map1", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map2", output));
}

TEST(MapFusionTest, FuseThreeNodesIntoOne) {
  using test::function::NDef;
  GrapplerItem item;
  item.graph = test::function::GDef(
      {NDef("start", "Const", {}, {{"value", 0}, {"dtype", DT_INT32}}),
       NDef("stop", "Const", {}, {{"value", 10}, {"dtype", DT_INT32}}),
       NDef("step", "Const", {}, {{"value", 1}, {"dtype", DT_INT32}}),
       NDef("filename", "Const", {}, {{"value", ""}, {"dtype", DT_STRING}}),
       NDef("range", "RangeDataset", {"start", "stop", "step"}, {}),
       MakeMapNode("map1", "range"), MakeMapNode("map2", "map1"),
       MakeMapNode("map3", "map2"),
       NDef("cache", "CacheDataset", {"map3", "filename"}, {})},
      // FunctionLib
      {
          test::function::XTimesTwo(),
      });

  MapFusion optimizer;
  GraphDef output;
  TF_ASSERT_OK(optimizer.Optimize(nullptr, item, &output));
  EXPECT_TRUE(graph_utils::ContainsNodeWithOp("MapDataset", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map1", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map2", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map3", output));
}

TEST(MapFusionTest, FuseTwoParallelMapNodesIntoOne) {
  using test::function::NDef;
  GrapplerItem item;
  NodeDef num_parallel_calls_node = CreateScalarConstNodeHelper(
      "num_parallel_calls", DT_INT64,
      [](TensorProto* proto) { proto->add_int64_val(-1); });
  item.graph = test::function::GDef(
      {NDef("start", "Const", {}, {{"value", 0}, {"dtype", DT_INT32}}),
       NDef("stop", "Const", {}, {{"value", 10}, {"dtype", DT_INT32}}),
       NDef("step", "Const", {}, {{"value", 1}, {"dtype", DT_INT32}}),
       NDef("range", "RangeDataset", {"start", "stop", "step"}, {}),
       num_parallel_calls_node,
       MakeParallelMapV2Node("map1", "range", num_parallel_calls_node.name(),
                             "XTimesTwo", "default"),
       MakeParallelMapV2Node("map2", "map1", num_parallel_calls_node.name(),
                             "XTimesTwo", "default")},
      // FunctionLib
      {
          test::function::XTimesTwo(),
      });

  MapFusion optimizer;
  GraphDef output;
  TF_ASSERT_OK(optimizer.Optimize(nullptr, item, &output));
  EXPECT_TRUE(graph_utils::ContainsNodeWithOp("ParallelMapDatasetV2", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map1", output));
  EXPECT_FALSE(graph_utils::ContainsGraphNodeWithName("map2", output));
}

}  // namespace
}  // namespace grappler
}  // namespace tensorflow
