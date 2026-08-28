/**
 * @file H264Encoder.cpp
 * @brief H264视频编码器实现
 * @details 使用x264库将YUV420视频编码为H264
 *          知识点：x264编码器API、YUV格式转换、视频编码参数配置
 */

#include <stdlib.h>
#include <string.h>
#include "H264Encoder.h"

// 静态变量：时间戳计数器（用于PTS）
static int pts_time=0;

/**
 * @brief 初始化H264编码器
 * @details 知识点：x264编码器初始化流程
 *          1. 分配参数和图片结构
 *          2. 设置默认参数（x264_param_default）
 *          3. 应用预设（preset和tune）
 *          4. 配置编码参数（分辨率、帧率、GOP等）
 *          5. 应用编码配置文件（profile：baseline）
 *          6. 打开编码器（x264_encoder_open）
 *          7. 分配图片缓冲区（x264_picture_alloc）
 * @param encoder 编码器结构体指针（输出参数）
 * @param width 视频宽度（像素）
 * @param height 视频高度（像素）
 */
void h264_encoder_init(X264Encoder * encoder, int width, int height)
{
    // 分配编码器参数结构
    encoder->param = (x264_param_t *) malloc(sizeof(x264_param_t));
    // 分配图片结构（用于存储输入YUV数据）
    encoder->picture = (x264_picture_t *) malloc(sizeof(x264_picture_t));
    
    // 知识点：设置默认参数
    // x264_param_default - 用默认值填充参数结构
    x264_param_default(encoder->param);

    // 知识点：应用预设和调优
    // x264_param_default_preset - 应用预设配置
    // "veryfast": 编码速度预设（速度优先，质量稍低）
    // "zerolatency": 零延迟调优（适合实时流媒体）
    //   作用：减少编码延迟，适合直播场景
    x264_param_default_preset(encoder->param, "veryfast", "zerolatency");
    
    // 设置视频分辨率
    encoder->param->i_width = width;
    encoder->param->i_height = height;
    
    // 知识点：编码参数配置
    encoder->param->i_sync_lookahead = 0;      // I帧向前缓冲区（0=禁用，降低延迟）
    encoder->param->i_fps_num = 15;            // 帧率分子：15帧/秒
    encoder->param->i_fps_den = 1;             // 帧率分母：1
    encoder->param->b_annexb = 1;              // 使用Annex-B格式（NALU格式，适合流媒体）
    encoder->param->i_keyint_max=30;           // 最大GOP长度（30帧一个I帧）
    encoder->param->i_keyint_min=15;           // 最小GOP长度
    encoder->param->i_bframe=0;                // B帧数量（0=不使用B帧，降低延迟）
    encoder->param->b_repeat_headers=1;        // 每个GOP重复SPS/PPS头（便于随机访问）
    encoder->param->i_threads = 1;              // 编码线程数（1=单线程）
    encoder->param->i_slice_count = 1;         // 每帧slice数量
    encoder->param->i_slice_count_max = 1;     // 最大slice数量

    // 知识点：应用编码配置文件
    // x264_param_apply_profile - 应用H264配置文件
    // "baseline": Baseline Profile（兼容性最好，适合低端设备）
    //   特点：不支持B帧、CABAC等高级特性，但兼容性最好
    x264_param_apply_profile(encoder->param, "baseline");

    // 知识点：打开编码器
    // x264_encoder_open - 根据参数打开编码器，分配内部资源
    // 返回：编码器句柄，NULL=失败
    if ((encoder->handle = x264_encoder_open(encoder->param)) == 0) {
        printf("x264_encoder_open error!\n");
        return;
    }

    // 知识点：分配图片缓冲区
    // x264_picture_alloc - 为YUV图片分配数据缓冲区
    // X264_CSP_I420: YUV420格式（平面格式，Y/U/V分别存储）
    //   特点：最常用的YUV格式，文件大小适中
    x264_picture_alloc(encoder->picture, X264_CSP_I420, encoder->param->i_width, encoder->param->i_height);
    encoder->picture->img.i_csp = X264_CSP_I420;  // 颜色空间：YUV420
    encoder->picture->img.i_plane = 3;             // 平面数：3（Y、U、V各一个平面）

    return;
}

/**
 * @brief 编码一帧视频
 * @details 知识点：视频编码流程
 *          1. 数据复制/转换：根据inputIsYUV420参数决定
 *             - true: 输入已是YUV420P格式，直接复制到编码器缓冲区（主采集线程已转换）
 *             - false: 输入是YUV422格式，需要转换为YUV420P
 *          2. 设置帧类型（I/P帧）
 *          3. 编码（x264_encoder_encode）
 *          4. 提取NALU数据
 * @param encoder 编码器结构体
 * @param type 帧类型：“-1=自动“   ，0=P帧，1=IDR帧，2=I帧
 * @param in 输入的YUV数据（YUV420P或YUV422格式，取决于inputIsYUV420参数）
 * @param out 输出的H264数据缓冲区（调用者分配）
 * @param inputIsYUV420 输入数据格式：true=YUV420P（已转换），false=YUV422（需要转换）
 * @return 编码后的数据长度（字节），<0=失败
 * @note 主采集线程已调用ConvertYUYV422ToYUV420P完成转换，此处传入inputIsYUV420=true
 */
int h264_compress_frame(X264Encoder * encoder, int type, uint8_t * in, uint8_t * out, bool inputIsYUV420)
{（主采集线程里调用的）
    x264_picture_t pic_out;  // 输出图片结构（编码后）
    int nNal = -1;           // NALU数量（输出参数）
    int result = 0;          // 总数据长度
    uint8_t *p_out = out;   // 输出指针

    unsigned int i, j;
    unsigned int base_h;

    // 知识点：YUV420平面格式
    // plane[0]: Y平面（亮度）
    // plane[1]: U平面（色度）
    // plane[2]: V平面（色度）
    // 注意：YUV420中，U和V的尺寸是Y的1/4（宽高各减半）
    char *y = (char *)encoder->picture->img.plane[0];
    char *u = (char *)encoder->picture->img.plane[1];
    char *v = (char *)encoder->picture->img.plane[2];

    if (inputIsYUV420) {
        size_t ySize = encoder->param->i_width * encoder->param->i_height;
        size_t uvSize = ySize / 4;
        memcpy(y, in, ySize);
        memcpy(u, in + ySize, uvSize);
        memcpy(v, in + ySize + uvSize, uvSize);
    } 
    else {//这是判断他仍为422时转换为420
        int is_y = 1, is_u = 1;
        int y_index = 0, u_index = 0, v_index = 0;
        int yuv422_length = 2 * encoder->param->i_width * encoder->param->i_height;  // YUV422数据总长度

        for(i=0; i<yuv422_length; i+=2){
            *(y+y_index) = *(in+i);
            y_index++;
        }

        for(i=0; i<encoder->param->i_height; i+=2){
            base_h = i*encoder->param->i_width*2;  // 当前行的起始位置
            for(j=base_h; j<base_h+encoder->param->i_width*2; j+=2){
                if(is_u){
                    *(u+u_index) = *(in+j);  // 提取U值（偶数位置）
                    u_index++;
                    is_u = 0;
                } else {
                    *(v+v_index) = *(in+j);  // 提取V值（奇数位置，但这里逻辑有问题）
                    v_index++;
                    is_u = 1;
                }
            }
        }
    }

    // 知识点：设置帧类型
    // H264帧类型：
    //   - I帧（关键帧）：独立编码，不依赖其他帧
    //   - IDR帧：即时解码刷新帧，I帧的一种，GOP的起始
    //   - P帧：预测帧，依赖前一帧
    //   - B帧：双向预测帧（本项目未使用）
    switch (type) {
        case 0:
            encoder->picture->i_type = X264_TYPE_P;      // P帧
            break;
        case 1:
            encoder->picture->i_type = X264_TYPE_IDR;     // IDR帧（关键帧）
            break;
        case 2:
            encoder->picture->i_type = X264_TYPE_I;       // I帧
            break;
        default:
            encoder->picture->i_type = X264_TYPE_AUTO;    // 自动选择
            break;
    }

    // 知识点：设置时间戳（PTS）
    // PTS（Presentation Time Stamp）：显示时间戳
    // x264编码器内部使用PTS来：
    //   1. 控制编码顺序（DTS计算）
    //   2. 生成时间戳信息（如果编码器需要）
    //   3. 用于B帧排序（本项目不使用B帧）
    // 注意：这个PTS是编码器内部使用的，        实际流媒体传输的PTS由主采集线程计算
    // （见CCServerController.cpp第340行：videoPts = frameCount * 1000 / fps）
    encoder->picture->i_pts=pts_time;//他的软件编码器里面有他自己的GOP大小

    // 知识点：执行编码
    // x264_encoder_encode - 编码一帧YUV数据为H264
    // 参数：
    //   - encoder->handle: 编码器句柄
    //   - &(encoder->nal): 输出参数，NALU数组指针
    //   - &nNal: 输出参数，NALU数量
    //   - encoder->picture: 输入图片（YUV数据）
    //   - &pic_out: 输出图片（编码后信息）
    // 返回：编码的字节数，<0=失败
    if (x264_encoder_encode(encoder->handle, &(encoder->nal), &nNal, encoder->picture,&pic_out) < 0) {
        printf("x264_encoder_encode error,type=%08x\n",encoder->picture->img.i_csp);
        return -1;
    }

    // 知识点：提取NALU数据
    // NALU（Network Abstraction Layer Unit）：H264的网络抽象层单元
    // “一帧编码”后可能产生多个NALU（如SPS、PPS、Slice等）
    // 需要将所有NALU的数据复制到输出缓冲区
    for (i = 0; i < nNal; i++) {
        // 复制NALU数据
        // encoder->nal[i].p_payload: NALU数据指针
        // encoder->nal[i].i_payload: NALU数据长度
        memcpy(p_out, encoder->nal[i].p_payload, encoder->nal[i].i_payload);
        p_out += encoder->nal[i].i_payload;  // 移动输出指针
        result += encoder->nal[i].i_payload;  // 累加总长度
    }

    // 更新时间戳计数器（每次编码后递增，用于下次编码的PTS）
    // 注意：这是编码器内部的帧计数，与主采集线程的frameCount独立
    pts_time=pts_time+1;

    return result;
}

/**
 * @brief 反初始化H264编码器
 * @details 释放所有x264资源，防止内存泄漏
 *          知识点：x264资源释放顺序
 */
void h264_encoder_uninit(X264Encoder * encoder)
{
    // 知识点：释放图片缓冲区
    // x264_picture_clean - 释放图片结构中的数据缓冲区
    if (encoder->picture) {
        x264_picture_clean(encoder->picture);
        free(encoder->picture);
        encoder->picture = 0;
    }
    
    // 释放参数结构
    if (encoder->param) {
        free(encoder->param);
        encoder->param = 0;
    }
    
    // 知识点：关闭编码器
    // x264_encoder_close - 关闭编码器，释放内部资源
    if (encoder->handle) {
        x264_encoder_close(encoder->handle);
    }
    return;
}