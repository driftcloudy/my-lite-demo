#include "utility.h"

#define TENSOR_UPDATE_INPUT(op, attr, format, dtype)                    \
  ge::TensorDesc _##op##_input_desc_##attr(ge::Shape(), format, dtype); \
  op.update_input_desc_##attr(_##op##_input_desc_##attr);
#define TENSOR_UPDATE_OUTPUT(op, attr, format, dtype)                    \
  ge::TensorDesc _##op##_output_desc_##attr(ge::Shape(), format, dtype); \
  op.update_output_desc_##attr(_##op##_output_desc_##attr);
#define TENSOR_UPDATE_DYNAMIC_INPUT(op, attr, idx, format, dtype)               \
  ge::TensorDesc _##op##_input_desc_##attr##_##idx(ge::Shape(), format, dtype); \
  op.update_dynamic_input_desc_##attr(idx, _##op##_input_desc_##attr##_##idx);

/*
* 对op名称添加后缀，保证op名称不重复
*/
const char *GetGlobalIndex( const char *str ) {
    static std::string strIndex;
    static uint32_t globalIndex = 0;
    strIndex.erase();
    strIndex.append(str);
    strIndex.append("_" + to_string(globalIndex));
    globalIndex++;
    return strIndex.c_str();
}

/*
* 设置Const Op Data
*/
template <typename T>
bool SetConstTensorFromValue(ge::Tensor &weight, uint32_t len, T value) {
  // len = len * sizeof( ge::DT_FLOAT );
  T *pdata = new( std::nothrow ) T[len];
  for (size_t i = 0; i < len; i ++) {
    pdata[i] = value;
  }
  ATC_CALL(weight.SetData( reinterpret_cast<uint8_t *>( pdata ), len * sizeof(T) ));
  return true;
}

/*
* Const Op封装，同时加载对应Const数据文件
*/
template <typename T>
ge::Operator Const_OP( ge::Shape dataShape, const char *str , T value) {
    ge::TensorDesc descWeight( dataShape, ge::FORMAT_ND, ge::DT_FLOAT );
    ge::Tensor weightTensor( descWeight );
    uint32_t weightLen = dataShape.GetShapeSize();
    SetConstTensorFromValue<T>(weightTensor, weightLen, value);
    auto net = ge::op::Const( GetGlobalIndex( str ) )
               .set_attr_value( weightTensor );
    return net;
}

/*
* 卷积操作，有weight和bias场景
*/
ge::Operator Conv2d_OP( ge::Operator inputNet ) {
    auto filter_shape = ge::Shape( {21L, 1L, 3L, 3L} );
    auto filter_op = Const_OP<float>( filter_shape, "conv_filter", static_cast<float>(1.0f) );
    auto bias_shape = ge::Shape( {21L} );
    auto bias_op = Const_OP<float>( bias_shape, "conv_bias", static_cast<float>(1.0f) );
    auto net = ge::op::Conv2D( GetGlobalIndex( "conv1" ) )
               .set_input_x( inputNet )
               .set_input_filter( filter_op )
               .set_input_bias( bias_op )
               .set_attr_strides( {1L, 1L, 1L, 1L} )
               .set_attr_pads( {0, 0, 0, 0} )
               .set_attr_dilations( {1L, 1L, 1L, 1L} )
               .set_attr_groups( 1 )
               .set_attr_data_format("NCHW");
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT );
    TENSOR_UPDATE_INPUT(net, filter, ge::FORMAT_NCHW, ge::DT_FLOAT );
    TENSOR_UPDATE_INPUT(net, bias, ge::FORMAT_NCHW, ge::DT_FLOAT );
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT );
    return net;
}

/*
* nearest_interp，scale = 2
*/
ge::Operator NearestNeighbor_OP(ge::Operator inputNet, int output_height, int output_width) {
    // create out size const tensor
    auto out_size_shape = ge::Shape( {2L} );
    ge::TensorDesc out_size_desc( out_size_shape, ge::FORMAT_ND, ge::DT_INT32 );
    ge::Tensor out_size_tensor( out_size_desc );
    uint32_t out_size_length = out_size_shape.GetShapeSize();
    int *pdata = new( std::nothrow ) int[2];
    pdata[0] = output_height;
    pdata[1] = output_width;
    ATC_CALL(out_size_tensor.SetData( reinterpret_cast<uint8_t *>( pdata ), out_size_length * sizeof(int) ));
    // create out size const op
    auto out_size_op = ge::op::Const( GetGlobalIndex( "Const/out_size" ) )
                       .set_attr_value( out_size_tensor );

    // NearestNeighbor op
    auto net = ge::op::ResizeNearestNeighborV2( GetGlobalIndex( "ResizeNearestNeighborV2" ) )
               .set_input_x( inputNet )
               .set_input_size( out_size_op )
               .set_attr_align_corners( false );
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, size,  ge::FORMAT_NCHW, ge::DT_INT32);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

ge::Operator YOLO_OP(ge::Operator inputNet) {
    auto net = ge::op::Yolo( GetGlobalIndex( "Yolo" ) )
               .set_input_x(inputNet)
               .set_attr_boxes(3)
               .set_attr_coords(4) // shout be fixed at 4
               .set_attr_classes(2)
               .set_attr_yolo_version("V3");
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, coord_data, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, obj_prob, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, classes_prob, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

ge::Operator YoloV3DetectionOutput_OP(ge::Operator net1, ge::Operator net2, ge::Operator net3, ge::Operator imgInfo) {
    auto net = ge::op::YoloV3DetectionOutput( GetGlobalIndex( "YoloV3DetectionOutput" ) )
               .set_input_coord_data_low(net1, "coord_data")
               .set_input_coord_data_mid(net2, "coord_data")
               .set_input_coord_data_high(net3, "coord_data")
               .set_input_obj_prob_low (net1, "obj_prob")
               .set_input_obj_prob_mid(net2, "obj_prob")
               .set_input_obj_prob_high(net3, "obj_prob")
               .set_input_classes_prob_low(net1, "classes_prob")
               .set_input_classes_prob_mid(net2, "classes_prob")
               .set_input_classes_prob_high(net3, "classes_prob")
               .set_input_img_info(imgInfo)
               .set_attr_biases_low({116, 90, 156, 198, 373, 326})
               .set_attr_biases_mid({30, 61, 62, 45, 59, 119})
               .set_attr_biases_high({10, 13, 16, 30, 33, 23})
               .set_attr_boxes(3)
               .set_attr_coords(4) // shout be fixed at 4
               .set_attr_classes(2)
               .set_attr_obj_threshold(0.5) // yolo_box: conf_thredhold
               .set_attr_post_nms_topn(16) // multiclass_nms: keep_top_k - max_box_number_per_batch should be a multiple of 16
               .set_attr_score_threshold(0.01) // multiclass_nms: score_threshold
               .set_attr_iou_threshold(0.45) // multiclass_nms: nms_threshold
               .set_attr_pre_nms_topn(8); // multiclass_nms: nms_top_k - parameter[pre_nms_topn] should be in the range of [1, 31]
    TENSOR_UPDATE_INPUT(net, coord_data_low, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, coord_data_mid, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, coord_data_high, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, obj_prob_low, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, obj_prob_mid, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, obj_prob_high, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, classes_prob_low, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, classes_prob_mid, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, classes_prob_high, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, img_info, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, box_out,  ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, box_out_num, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

ge::Operator Reshape_OP1(ge::Operator inputNet) {
    // Const Tensor of new shape
    auto new_shape = ge::Shape( {3L} );
    ge::TensorDesc new_shape_desc( new_shape, ge::FORMAT_ND, ge::DT_INT32 );
    ge::Tensor new_shape_tensor( new_shape_desc );
    uint32_t out_size_length = new_shape.GetShapeSize();
    int *pdata = new( std::nothrow ) int[out_size_length];
    pdata[0] = 1; // batch size
    pdata[1] = 6; // 6
    pdata[2] = -1; // keep_top_k
    ATC_CALL(new_shape_tensor.SetData( reinterpret_cast<uint8_t *>( pdata ), out_size_length * sizeof(int) ));
    // Const OP of new shape
    auto new_shape_op = ge::op::Const( GetGlobalIndex( "Const/new_size" ) )
                       .set_attr_value( new_shape_tensor );
    // Reshape OP
    auto net = ge::op::Reshape( GetGlobalIndex( "Reshape" ) )
               .set_input_x(inputNet, "box_out")
               .set_input_shape(new_shape_op)
               .set_attr_axis(0)
               .set_attr_num_axes(-1);
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, shape, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

ge::Operator Transpose_OP(ge::Operator inputNet) {
    auto net = ge::op::TransposeD( GetGlobalIndex( "Transpose" ) )
               .set_input_x(inputNet)
               .set_attr_perm({0,2,1}); // batch (0), 6 (1), keep_top_k (2) => batch (0), keep_top_k (2), 6 (1)
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

ge::Operator Reshape_OP2(ge::Operator inputNet) {
    // Const Tensor of new shape
    auto new_shape = ge::Shape( {2L} );
    ge::TensorDesc new_shape_desc( new_shape, ge::FORMAT_ND, ge::DT_INT32 );
    ge::Tensor new_shape_tensor( new_shape_desc );
    uint32_t out_size_length = new_shape.GetShapeSize();
    int *pdata = new( std::nothrow ) int[out_size_length];
    pdata[0] = -1; // keep_top_k
    pdata[1] = 6; // batch * 6
    ATC_CALL(new_shape_tensor.SetData( reinterpret_cast<uint8_t *>( pdata ), out_size_length * sizeof(int) ));
    // Const OP of new shape
    auto new_shape_op = ge::op::Const( GetGlobalIndex( "Const/new_size" ) )
                       .set_attr_value( new_shape_tensor );
    // Reshape OP
    auto net = ge::op::Reshape( GetGlobalIndex( "Reshape" ) )
               .set_input_x(inputNet, "box_out")
               .set_input_shape(new_shape_op)
               .set_attr_axis(0)
               .set_attr_num_axes(-1);
    TENSOR_UPDATE_INPUT(net, x, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_INPUT(net, shape, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

/*
* Concat三输入
*/
ge::Operator ConcatTrible_OP(ge::Operator net1, ge::Operator net2, ge::Operator net3, ge::Operator axis) {
    auto net = ge::op::Concat( GetGlobalIndex( "Concat" ) )
               .create_dynamic_input_x( 3 )
               .set_dynamic_input_x( 0, net1 )
               .set_dynamic_input_x( 1, net2 )
               .set_dynamic_input_x( 2, net3 )
               .set_input_concat_dim( axis ) // axis
               .set_attr_N(3);
    TENSOR_UPDATE_INPUT(net, concat_dim, ge::FORMAT_NCHW, ge::DT_INT32);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 0, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 1, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 2, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}

/*
* ConcatD三输入
*/
ge::Operator ConcatDTrible_OP(ge::Operator net1, ge::Operator net2, ge::Operator net3) {
    auto net = ge::op::ConcatD( GetGlobalIndex( "ConcatD" ) )
               .create_dynamic_input_x( 3 )
               .set_dynamic_input_x( 0, net1 )
               .set_dynamic_input_x( 1, net2 )
               .set_dynamic_input_x( 2, net3 )
               .set_attr_concat_dim(1) // axis
               .set_attr_N(3);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 0, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 1, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_DYNAMIC_INPUT(net, x, 2, ge::FORMAT_NCHW, ge::DT_FLOAT);
    TENSOR_UPDATE_OUTPUT(net, y, ge::FORMAT_NCHW, ge::DT_FLOAT);
    return net;
}


/*
* ConcatD单输入
*/
ge::Operator ConcatSingle_OP(ge::Operator net1) {
    auto net = ge::op::ConcatD( GetGlobalIndex( "ConcatD" ) )
               .create_dynamic_input_x( 1 )
               .set_dynamic_input_x( 0, net1 )
               .set_attr_concat_dim(1)
               .set_attr_N(1);
    return net;
}

bool GenYoloV3Graph(ge::Graph& graph) {
    // ==========================input data op => format: NCHW==========================
    // input x low: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_low(ge::Shape({ 1L, 21L, 6L, 6L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_low = ge::op::Data("input_x1");//.set_attr_index(0);
    input_x_low.update_input_desc_x(input_desc_low);
    input_x_low.update_output_desc_y(input_desc_low);

    // input x mid: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_mid(ge::Shape({ 1L, 21L, 12L, 12L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_mid = ge::op::Data("input_x2").set_attr_index(1);
    input_x_mid.update_input_desc_x(input_desc_mid);
    input_x_mid.update_output_desc_y(input_desc_mid);

    // input x high: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_high(ge::Shape({ 1L, 21L, 24L, 24L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_high = ge::op::Data("input_x3");//.set_attr_index(2);
    input_x_high.update_input_desc_x(input_desc_high);
    input_x_high.update_output_desc_y(input_desc_high);

    // input imgsize: 2D tensor of shape (batchsize, 2)
    ge::TensorDesc imgsize_desc(ge::Shape({ 1L, 4L}), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_img_size = ge::op::Data("input_imgsize");//.set_attr_index(3);
    input_img_size.update_input_desc_x(imgsize_desc);
    input_img_size.update_output_desc_y(imgsize_desc);

    // // Prepare 3 Yolo Input
    // ge::Operator input_low = Conv2d_OP(input_x); // 1, 21, 6, 6  height[2] multi with width[2]'s size must bigger than 32b
    // ge::Operator input_mid = NearestNeighbor_OP(input_low, 12, 12); // 1, 21, 12, 12
    // ge::Operator input_high = NearestNeighbor_OP(input_mid, 24, 24); // 1, 21, 24, 24

    // Yolo OP
    ge::Operator net_low = YOLO_OP(input_x_low);
    ge::Operator net_mid = YOLO_OP(input_x_mid);
    ge::Operator net_high = YOLO_OP(input_x_high);

    // YoloV3DetectionOutput_OP
    ge::Operator net_detect = YoloV3DetectionOutput_OP(net_low, net_mid, net_high, input_img_size);

    // Reshpae OP [batch, 6 * keep_top_k] => [batch, 6, keep_top_k]
    ge::Operator net_reshape1 = Reshape_OP1(net_detect);

    // Tranpose OP [batch, 6, keep_top_k] => [batch, keep_top_k, 6]
    ge::Operator net_transpose = Transpose_OP(net_reshape1);

    // Reshape OP [batch, keep_top_k, 6] => [ batch * keep_top_k, 6]
    ge::Operator net_reshape2 = Reshape_OP2(net_detect);

    // Set inputs and outputs
    std::vector<ge::Operator> input_nodes{ input_x_low, input_x_mid, input_x_high, input_img_size };
    // std::vector<ge::Operator> outputs{ net_reshape2 };
    std::vector<ge::Operator> output_nodes{ net_detect };

    // set input node attr index is node size > 1
    if (input_nodes.size() > 1) {
      int idx = 0;
      for (auto node : input_nodes) {
        node.SetAttr("index", idx);
        idx++;
      }
    }
    graph.SetInputs(input_nodes).SetOutputs(output_nodes);

    return true;
}

bool GenConcatGraph(ge::Graph& graph) {
    // input x low: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_low(ge::Shape({ 1L, 108L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_low = ge::op::Data("input_x1");//.set_attr_index(0);
    input_x_low.update_input_desc_x(input_desc_low);
    input_x_low.update_output_desc_y(input_desc_low);

    // input x mid: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_mid(ge::Shape({ 1L, 432L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_mid = ge::op::Data("input_x2");//.set_attr_index(1);
    input_x_mid.update_input_desc_x(input_desc_mid);
    input_x_mid.update_output_desc_y(input_desc_mid);

    // input x high: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_high(ge::Shape({ 1L, 1728L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_high = ge::op::Data("input_x3");//.set_attr_index(2);
    input_x_high.update_input_desc_x(input_desc_high);
    input_x_high.update_output_desc_y(input_desc_high);

    // input axis: An int32, or int64. Specifies the dimension along which to concatenate.
    ge::TensorDesc input_desc_axis(ge::Shape({ 1L }), ge::FORMAT_ND, ge::DT_INT32);
    auto input_axis = ge::op::Data("input_axis");//.set_attr_index(2);
    input_axis.update_input_desc_x(input_desc_axis);
    input_axis.update_output_desc_y(input_desc_axis);

    // Concat OP
    ge::Operator concat_output = ConcatTrible_OP(input_x_low, input_x_mid, input_x_high, input_axis);

    // Set inputs and outputs
    std::vector<ge::Operator> input_nodes{ input_x_low, input_x_mid, input_x_high, input_axis };
    // std::vector<ge::Operator> outputs{ net_reshape2 };
    std::vector<ge::Operator> output_nodes{ concat_output };

    // set input node attr index is node size > 1
    if (input_nodes.size() > 1) {
      int idx = 0;
      for (auto node : input_nodes) {
        node.SetAttr("index", idx);
        idx++;
      }
    }
    graph.SetInputs(input_nodes).SetOutputs(output_nodes);

    VLOG(3) << "Getting input node size " << input_nodes.size();
    VLOG(3) << "Getting output node size " << output_nodes.size();

    // for debug
    for (auto node : input_nodes) {
        VLOG(3) << "input ndoe name " << node.GetName();
        VLOG(3) << "input node type " << node.GetOpType();
    }
    for (auto node : output_nodes) {
        VLOG(3) << "output ndoe name " << node.GetName();
        VLOG(3) << "output node type " << node.GetOpType();
    }

    return true;
}

bool GenConcatDGraph(ge::Graph& graph) {
    // input x low: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_low(ge::Shape({ 1L, 108L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_low = ge::op::Data("input_x1");//.set_attr_index(0);
    input_x_low.update_input_desc_x(input_desc_low);
    input_x_low.update_output_desc_y(input_desc_low);

    // input x mid: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_mid(ge::Shape({ 1L, 432L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_mid = ge::op::Data("input_x2");//.set_attr_index(1);
    input_x_mid.update_input_desc_x(input_desc_mid);
    input_x_mid.update_output_desc_y(input_desc_mid);

    // input x high: A 4D tensor of input images - NCHW
    ge::TensorDesc input_desc_high(ge::Shape({ 1L, 1728L }), ge::FORMAT_ND, ge::DT_FLOAT);
    auto input_x_high = ge::op::Data("input_x3");//.set_attr_index(2);
    input_x_high.update_input_desc_x(input_desc_high);
    input_x_high.update_output_desc_y(input_desc_high);

    // Concat OP
    ge::Operator concat_output = ConcatDTrible_OP(input_x_low, input_x_mid, input_x_high);

    // Set inputs and outputs
    std::vector<ge::Operator> input_nodes{ input_x_low, input_x_mid, input_x_high };
    // std::vector<ge::Operator> outputs{ net_reshape2 };
    std::vector<ge::Operator> output_nodes{ concat_output };

    // set input node attr index is node size > 1
    if (input_nodes.size() > 1) {
      int idx = 0;
      for (auto node : input_nodes) {
        node.SetAttr("index", idx);
        idx++;
      }
    }
    graph.SetInputs(input_nodes).SetOutputs(output_nodes);

    VLOG(3) << "Getting input node size " << input_nodes.size();
    VLOG(3) << "Getting output node size " << output_nodes.size();

    // for debug
    for (auto node : input_nodes) {
        VLOG(3) << "input ndoe name " << node.GetName();
        VLOG(3) << "input node type " << node.GetOpType();
    }
    for (auto node : output_nodes) {
        VLOG(3) << "output ndoe name " << node.GetName();
        VLOG(3) << "output node type " << node.GetOpType();
    }

    return true;
}