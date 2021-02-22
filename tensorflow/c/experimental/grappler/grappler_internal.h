/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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
// Classes and utilities that work with Graph C API for internal use.
// This includes functions used for optimizer registration and interfaces needed
// for testing.

#ifndef TENSORFLOW_C_EXPERIMENTAL_GRAPPLER_GRAPPLER_INTERNAL_H_
#define TENSORFLOW_C_EXPERIMENTAL_GRAPPLER_GRAPPLER_INTERNAL_H_

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "tensorflow/c/c_api.h"
#include "tensorflow/c/experimental/grappler/grappler.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/grappler/optimizers/custom_graph_optimizer.h"
#include "tensorflow/core/grappler/optimizers/custom_graph_optimizer_registry.h"
#include "tensorflow/core/platform/status.h"
#include "tensorflow/core/protobuf/rewriter_config.pb.h"

namespace tensorflow {
namespace grappler {

// Plugin initialization function that a device plugin
// must define.
typedef void (*TFInitGraphPluginFn)(TP_OptimizerRegistrationParams* const,
                                    TF_Status* const);

// Registers Graph optimizers.
Status InitGraphPlugin(void* dso_handle);

// Allow registering a graph optimizer using a function (used for
// testing).
Status InitGraphPlugin(TFInitGraphPluginFn init_fn);

struct GrapplerItem;
class Cluster;

struct TFStatusDeleter {
  void operator()(TF_Status* s) const { TF_DeleteStatus(s); }
};
using OwnedTFStatus = std::unique_ptr<TF_Status, TFStatusDeleter>;

struct TFBufferDeleter {
  void operator()(TF_Buffer* buf) const { TF_DeleteBuffer(buf); }
};
using OwnedTFBuffer = std::unique_ptr<TF_Buffer, TFBufferDeleter>;

class CGraphOptimizer : public CustomGraphOptimizer {
 public:
  explicit CGraphOptimizer(TP_Optimizer optimizer, const char* device_type)
      : optimizer_(optimizer), device_type_(device_type) {
    if (optimizer.create_func != nullptr) {
      c_optimizer_ = (*optimizer_.create_func)();
    } else {
      c_optimizer_ = nullptr;
    }
  }
  std::string name() const { return "PluggableGraphOptimizer"; }
  bool UsesFunctionLibrary() const { return false; }
  void Feedback(Cluster* cluster, const GrapplerItem& item,
                const GraphDef& optimized_graph, double result) {}
  Status Init(
      const tensorflow::RewriterConfig_CustomGraphOptimizer* config = nullptr) {
    return Status::OK();
  }
  Status Optimize(Cluster* cluster, const GrapplerItem& item,
                  GraphDef* optimized_graph_def);

  ~CGraphOptimizer() {
    if (optimizer_.destroy_func != nullptr) {
      (*optimizer_.destroy_func)(c_optimizer_);
    }
  }

 private:
  TP_Optimizer optimizer_;
  std::string device_type_;
  void* c_optimizer_;
};

// Configs are turned on by default.
#define CONFIG_TOGGLE(optimizer)                             \
  if (tp_configs.optimizer == TF_TriState_Off)               \
    configs.toggle_config[#optimizer] = RewriterConfig::OFF; \
  else                                                       \
    configs.toggle_config[#optimizer] = RewriterConfig::ON;

void CGraphOptimizerRegister(
    const PluginGraphOptimizerRegistry::Creator& creator,
    const TP_OptimizerConfigs tp_configs, const char* device_type) {
  ConfigsList configs;
  // disable_model_pruning is turned off by default.
  if (tp_configs.disable_model_pruning == TF_TriState_On)
    configs.disable_model_pruning = true;
  else
    configs.disable_model_pruning = false;
  CONFIG_TOGGLE(implementation_selector);
  CONFIG_TOGGLE(function_optimization);
  CONFIG_TOGGLE(common_subgraph_elimination);
  CONFIG_TOGGLE(arithmetic_optimization);
  CONFIG_TOGGLE(debug_stripper);
  CONFIG_TOGGLE(constant_folding);
  CONFIG_TOGGLE(shape_optimization);
  CONFIG_TOGGLE(auto_mixed_precision);
  CONFIG_TOGGLE(auto_mixed_precision_mkl);
  CONFIG_TOGGLE(pin_to_host_optimization);
  CONFIG_TOGGLE(layout_optimizer);
  CONFIG_TOGGLE(remapping);
  CONFIG_TOGGLE(loop_optimization);
  CONFIG_TOGGLE(dependency_optimization);
  CONFIG_TOGGLE(auto_parallel);
  CONFIG_TOGGLE(memory_optimization);
  CONFIG_TOGGLE(scoped_allocator_optimization);
  PluginGraphOptimizerRegistry::RegisterPluginOptimizerOrDie(
      creator, device_type, configs);
}

#undef CONFIG_TOGGLE

}  // namespace grappler
}  // namespace tensorflow

#endif  // TENSORFLOW_C_EXPERIMENTAL_GRAPPLER_GRAPPLER_INTERNAL_H_
