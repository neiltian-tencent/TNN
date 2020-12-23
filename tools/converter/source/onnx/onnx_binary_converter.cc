// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "onnx/onnx_utils.h"
#include "tnn/interpreter/tnn/objseri.h"
#include "tools/converter/source/onnx/onnx_base_converter.h"

namespace TNN_CONVERTER {
DECLARE_OP_CONVERTER(Binary);

std::string OnnxBinaryConverter::TNNOpType(const onnx::NodeProto &node, bool quantized_model) {
    return node.op_type();
}

TNN_NS::ActivationType OnnxBinaryConverter::ActivationType(const onnx::NodeProto &node) {
    return TNN_NS::ActivationType_None;
}

TNN_NS::Status OnnxBinaryConverter::exec(tnn::NetStructure &net_structure, tnn::NetResource &net_resource,
                                         const onnx::NodeProto &node,
                                         std::map<std::string, const onnx::TensorProto *> &proxy_initializers_map,
                                         std::map<std::string, std::shared_ptr<OnnxProxyNode>> &proxy_nodes,
                                         bool &quantized_model) {
    const std::string &onnx_op = node.op_type();
    auto param                 = new TNN_NS::MultidirBroadcastLayerParam;
    auto cur_layer             = net_structure.layers.back();
    cur_layer->param           = std::shared_ptr<TNN_NS::LayerParam>(param);
    param->type                = cur_layer->type_str;
    param->name                = cur_layer->name;
    param->quantized           = false;

    param->weight_input_index = -1;
    const auto &matrix_a_name = node.input(0);
    const auto &matrix_b_name = node.input(1);
    if (proxy_initializers_map.find(matrix_a_name) != proxy_initializers_map.end()) {
        param->weight_input_index        = 0;
        auto layer_resource              = new TNN_NS::EltwiseLayerResource;
        layer_resource->name             = cur_layer->name;
        const auto &tensor_proto         = proxy_initializers_map[matrix_a_name];
        TNN_NS::RawBuffer *weight_handle = nullptr;
        CreateRawBufferFromTensor(*tensor_proto, &weight_handle);
        layer_resource->element_handle             = *weight_handle;
        net_resource.resource_map[cur_layer->name] = std::shared_ptr<TNN_NS::LayerResource>(layer_resource);
        cur_layer->inputs.resize(1);
        cur_layer->inputs[0] = node.input(1);
    } else if (proxy_initializers_map.find(matrix_b_name) != proxy_initializers_map.end()) {
        param->weight_input_index        = 1;
        auto layer_resource              = new TNN_NS::EltwiseLayerResource;
        layer_resource->name             = cur_layer->name;
        const auto &tensor_proto         = proxy_initializers_map[matrix_b_name];
        TNN_NS::RawBuffer *weight_handle = nullptr;
        CreateRawBufferFromTensor(*tensor_proto, &weight_handle);
        layer_resource->element_handle             = *weight_handle;
        net_resource.resource_map[cur_layer->name] = std::shared_ptr<TNN_NS::LayerResource>(layer_resource);
        cur_layer->inputs.resize(1);
        cur_layer->inputs[0] = node.input(0);
    } else {
        cur_layer->inputs[0] = node.input(0);
        cur_layer->inputs[1] = node.input(1);
    }

    return TNN_NS::TNN_CONVERT_OK;
}

REGISTER_CONVERTER(Binary, Add);
REGISTER_CONVERTER(Binary, Sub);
REGISTER_CONVERTER(Binary, Mul);
REGISTER_CONVERTER(Binary, Div);

}  // namespace TNN_CONVERTER